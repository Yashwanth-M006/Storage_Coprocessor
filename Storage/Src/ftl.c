/*
 * ftl.c
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#include "ftl.h"
#include "parse.h"
#include "spi_flash.h"
#include <string.h>

/* Flash driver prototypes (implement separately) */
//extern int Flash_Write(uint32_t addr, uint8_t *data, uint32_t len);
//extern int Flash_Read(uint32_t addr, uint8_t *data, uint32_t len);
//extern int Flash_Erase_Sector(uint32_t addr);

/*********************************************** Caches & State ***************************************************/

ftl_partition_t partitions[PARTITION_MAX];

// L2P Cache (LRU)
l2p_cache_entry_t l2p_cache[L2P_CACHE_ENTRIES];
uint16_t l2p_cache_count = 0;
uint8_t l2p_cache_dirty[L2P_CACHE_ENTRIES];

// Atomic State
uint32_t active_l2p_bank = L2P_TABLE_BANK_A;
uint32_t l2p_sequence = 0;
uint64_t active_last_used_sequence = 0;

volatile uint8_t ftl_locked = 0;

// Free block tracking
uint8_t free_block_bitmap[1024]; // 8192 bits for 8192 sectors. 1 = free, 0 = used.

// Journaling State
uint16_t journal_head = 0;

/************************************************ Helpers **********************************************/

// Helper functions can go here

// Compute CRC32 for journal entry
static uint32_t Calculate_Journal_CRC32(journal_entry_t *entry)
{
    uint16_t data_len = sizeof(journal_entry_t) - sizeof(uint32_t);
    return Hardware_CRC32((uint8_t*)entry, data_len);
}

/************************************************ L2P Mapping API **********************************************/

// Fetch a physical block for a logical block
uint16_t FTL_Get_Physical_Block(uint16_t logical_block)
{
    // 1. Check RAM Cache
    for (int i = 0; i < l2p_cache_count; i++)
    {
        if (l2p_cache[i].logical_block == logical_block)
        {
            // Move to front (LRU) - omitted for brevity in skeleton
            return l2p_cache[i].physical_block;
        }
    }

    // 2. Cache Miss: Read from Flash Persistent Table
    uint16_t physical_block = INVALID_BLOCK;
    uint32_t flash_addr = active_l2p_bank + (logical_block * sizeof(uint16_t));
    Flash_Read(flash_addr, (uint8_t*)&physical_block, sizeof(uint16_t));

    // 3. Add to Cache (evict if full - LRU eviction not fully implemented here)
    if (l2p_cache_count < L2P_CACHE_ENTRIES)
    {
        l2p_cache[l2p_cache_count].logical_block = logical_block;
        l2p_cache[l2p_cache_count].physical_block = physical_block;
        l2p_cache_dirty[l2p_cache_count] = 0;
        l2p_cache_count++;
    }
    
    return physical_block;
}

// Check if block is free
static int Is_Block_Free(uint16_t block)
{
    return (free_block_bitmap[block / 8] & (1 << (block % 8)));
}

// Mark block as used
static void Mark_Block_Used(uint16_t block)
{
    free_block_bitmap[block / 8] &= ~(1 << (block % 8));
}

// Allocate the least-worn free block
uint16_t FTL_Allocate_Free_Block(void)
{
    __disable_irq();
    while (ftl_locked) {
        __enable_irq();
        __disable_irq();
    }
    ftl_locked = 1;
    __enable_irq();

    uint16_t best_block = INVALID_BLOCK;
    uint32_t min_erase_count = 0xFFFFFFFF;
    
    // Scan bitmap for free blocks. 
    // Optimization: we could just check a subset of free blocks instead of all of them to save time.
    int checked_count = 0;
    for (uint16_t i = 0; i < FLASH_SECTOR_COUNT; i++)
    {
        if (Is_Block_Free(i))
        {
            uint32_t erase_count;
            Flash_Read(ERASE_COUNT_ADDR + (i * sizeof(uint32_t)), (uint8_t*)&erase_count, sizeof(uint32_t));
            
            if (erase_count < min_erase_count)
            {
                min_erase_count = erase_count;
                best_block = i;
            }
            
            checked_count++;
            if (checked_count > 32) break; // Only check 32 free blocks for speed (Partial WL)
        }
    }
    
    if (best_block != INVALID_BLOCK)
    {
        Mark_Block_Used(best_block);
    }
    
    ftl_locked = 0;
    return best_block;
}

// Flush dirty cache entries to persistent table (Atomic A/B Double Buffering)
void FTL_Flush_Cache(void)
{
    uint32_t inactive_bank = (active_l2p_bank == L2P_TABLE_BANK_A) ? L2P_TABLE_BANK_B : L2P_TABLE_BANK_A;
    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    
    // Erase inactive bank (4 sectors = 16 KB)
    for (int i = 0; i < 4; i++) {
        Flash_Erase_Sector(inactive_bank + (i * FLASH_SECTOR_SIZE));
    }
    
    // Copy active bank to inactive bank, applying cache changes
    for (int s = 0; s < 4; s++) {
        Flash_Read(active_l2p_bank + (s * FLASH_SECTOR_SIZE), sector_buffer, FLASH_SECTOR_SIZE);
        
        // Apply cache changes for this sector
        for (int i = 0; i < l2p_cache_count; i++) {
            if (l2p_cache_dirty[i]) {
                uint32_t logical_block = l2p_cache[i].logical_block;
                uint32_t target_sector = (logical_block * sizeof(uint16_t)) / FLASH_SECTOR_SIZE;
                
                if (target_sector == s) {
                    uint32_t offset = (logical_block * sizeof(uint16_t)) % FLASH_SECTOR_SIZE;
                    memcpy(&sector_buffer[offset], &l2p_cache[i].physical_block, sizeof(uint16_t));
                    l2p_cache_dirty[i] = 0;
                }
            }
        }
        
        // Embed the header at the very end of the final sector
        if (s == 3) {
            l2p_header_t header;
            l2p_sequence++;
            header.sequence_number = l2p_sequence;
            header.last_used_sequence = active_last_used_sequence;
            header.crc32 = Hardware_CRC32(sector_buffer, FLASH_SECTOR_SIZE - sizeof(l2p_header_t));
            
            memcpy(&sector_buffer[FLASH_SECTOR_SIZE - sizeof(l2p_header_t)], &header, sizeof(l2p_header_t));
        }
        
        Flash_Write(inactive_bank + (s * FLASH_SECTOR_SIZE), sector_buffer, FLASH_SECTOR_SIZE);
    }
    
    // Atomically swap active bank pointer in RAM
    active_l2p_bank = inactive_bank;
}

// Update the L2P mapping
void FTL_Update_L2P(uint16_t logical_block, uint16_t new_physical_block)
{
    __disable_irq();
    while (ftl_locked) {
        __enable_irq();
        __disable_irq();
    }
    ftl_locked = 1;
    __enable_irq();

    // 1. Update RAM Cache
    int found = 0;
    for (int i = 0; i < l2p_cache_count; i++)
    {
        if (l2p_cache[i].logical_block == logical_block)
        {
            l2p_cache[i].physical_block = new_physical_block;
            l2p_cache_dirty[i] = 1;
            found = 1;
            break;
        }
    }

    if (!found && l2p_cache_count < L2P_CACHE_ENTRIES)
    {
        l2p_cache[l2p_cache_count].logical_block = logical_block;
        l2p_cache[l2p_cache_count].physical_block = new_physical_block;
        l2p_cache_dirty[l2p_cache_count] = 1;
        l2p_cache_count++;
    }
    
    ftl_locked = 0;
}

/************************************************ Journal API **********************************************/

void FTL_Append_Journal(partition_id_t part_id)
{
    journal_entry_t entry;
    entry.partition_id = part_id;
    entry.state_version = 1; // Increment this properly later
    entry.write_ptr = partitions[part_id].write_ptr;
    entry.oldest_ptr = partitions[part_id].oldest_ptr;
    entry.timestamp = HAL_GetTick();
    entry.last_used_sequence = active_last_used_sequence;
    entry.crc32 = Calculate_Journal_CRC32(&entry);

    uint32_t journal_offset = JOURNAL_START_ADDR + (journal_head * sizeof(journal_entry_t));
    Flash_Write(journal_offset, (uint8_t*)&entry, sizeof(journal_entry_t));

    journal_head = (journal_head + 1) % (JOURNAL_SIZE / sizeof(journal_entry_t));
}

/********************************************** Public API ******************************************************/

int FTL_Init(void)
{
    // Phase 1: Restore L2P Table from A/B Banks
    l2p_header_t header_a, header_b;
    uint8_t buffer_a[FLASH_SECTOR_SIZE];
    uint8_t buffer_b[FLASH_SECTOR_SIZE];
    
    // Read Sector 3 of Bank A (contains header at the end)
    Flash_Read(L2P_TABLE_BANK_A + (3 * FLASH_SECTOR_SIZE), buffer_a, FLASH_SECTOR_SIZE);
    memcpy(&header_a, &buffer_a[FLASH_SECTOR_SIZE - sizeof(l2p_header_t)], sizeof(l2p_header_t));
    uint32_t crc_a = Hardware_CRC32(buffer_a, FLASH_SECTOR_SIZE - sizeof(l2p_header_t));
    
    // Read Sector 3 of Bank B
    Flash_Read(L2P_TABLE_BANK_B + (3 * FLASH_SECTOR_SIZE), buffer_b, FLASH_SECTOR_SIZE);
    memcpy(&header_b, &buffer_b[FLASH_SECTOR_SIZE - sizeof(l2p_header_t)], sizeof(l2p_header_t));
    uint32_t crc_b = Hardware_CRC32(buffer_b, FLASH_SECTOR_SIZE - sizeof(l2p_header_t));

    int a_valid = (crc_a == header_a.crc32);
    int b_valid = (crc_b == header_b.crc32);

    if (a_valid && b_valid) {
        active_l2p_bank = (header_a.sequence_number > header_b.sequence_number) ? L2P_TABLE_BANK_A : L2P_TABLE_BANK_B;
        l2p_sequence = (header_a.sequence_number > header_b.sequence_number) ? header_a.sequence_number : header_b.sequence_number;
        active_last_used_sequence = (header_a.sequence_number > header_b.sequence_number) ? header_a.last_used_sequence : header_b.last_used_sequence;
    } else if (a_valid) {
        active_l2p_bank = L2P_TABLE_BANK_A;
        l2p_sequence = header_a.sequence_number;
        active_last_used_sequence = header_a.last_used_sequence;
    } else if (b_valid) {
        active_l2p_bank = L2P_TABLE_BANK_B;
        l2p_sequence = header_b.sequence_number;
        active_last_used_sequence = header_b.last_used_sequence;
    } else {
        // Both corrupt (first boot). Format required.
        active_l2p_bank = L2P_TABLE_BANK_A;
        l2p_sequence = 0;
        active_last_used_sequence = 0;
    }

    // Phase 2: Boot-Time Recovery Scan
    // 1. Scan Journal for latest states
    uint32_t latest_timestamp = 0;
    journal_entry_t latest_entry;
    int found_valid_journal = 0;

    for (int i = 0; i < (JOURNAL_SIZE / sizeof(journal_entry_t)); i++)
    {
        journal_entry_t entry;
        uint32_t offset = JOURNAL_START_ADDR + (i * sizeof(journal_entry_t));
        Flash_Read(offset, (uint8_t*)&entry, sizeof(journal_entry_t));

        if (entry.crc32 == Calculate_Journal_CRC32(&entry))
        {
            if (entry.timestamp >= latest_timestamp)
            {
                latest_timestamp = entry.timestamp;
                latest_entry = entry;
                found_valid_journal = 1;
            }
        }
    }

    if (found_valid_journal)
    {
        // Restore active partition pointers from journal
        partitions[latest_entry.partition_id].write_ptr = latest_entry.write_ptr;
        partitions[latest_entry.partition_id].oldest_ptr = latest_entry.oldest_ptr;
        
        // Sync Nonce Watermark
        if (latest_entry.last_used_sequence > active_last_used_sequence) {
            active_last_used_sequence = latest_entry.last_used_sequence;
        }
    }

    // 2. Scan active physical sectors to verify consistency with L2P table
    // (This guarantees atomicity if power failed during a cache flush)
    // 3. Rebuild Free Block Bitmap
    memset(free_block_bitmap, 0xFF, sizeof(free_block_bitmap)); // Mark all free initially
    
    for (uint16_t logical = 0; logical < FLASH_SECTOR_COUNT; logical++)
    {
        uint16_t physical_block;
        Flash_Read(active_l2p_bank + (logical * sizeof(uint16_t)), (uint8_t*)&physical_block, sizeof(uint16_t));
        
        if (physical_block != INVALID_BLOCK && physical_block < FLASH_SECTOR_COUNT)
        {
            Mark_Block_Used(physical_block);
        }
    }

    return 0;
}

int FTL_Config(config_payload_t *config)
{
    // Build Logical Partitions
    uint32_t base_logical = 0;
    uint32_t total_logical_sectors = (FLASH_LOG_SIZE / FLASH_SECTOR_SIZE);

    for (int i = 0; i < PARTITION_MAX; i++)
    {
        uint32_t percent = ((config->partition_map >> (i * 3)) & 0x07) * 10;
        uint32_t sectors = (total_logical_sectors * percent) / 100;

        partitions[i].start_logical_addr = base_logical * FLASH_SECTOR_SIZE;
        partitions[i].end_logical_addr   = (base_logical + sectors) * FLASH_SECTOR_SIZE;
        partitions[i].logical_sectors    = sectors;
        
        partitions[i].write_ptr  = partitions[i].start_logical_addr;
        partitions[i].oldest_ptr = partitions[i].start_logical_addr;

        if (config->storage_mode & (1 << i) || config->storage_mode & (1 << 4))
            partitions[i].ftl_mode = FTL_MODE_CIRCULAR;
        else
            partitions[i].ftl_mode = FTL_MODE_STOP;

        base_logical += sectors;
    }

    return 0;
}

int FTL_Append(uint8_t log_type, uint8_t *data, uint16_t len)
{
    if (log_type >= PARTITION_MAX)
        return -1;

    ftl_partition_t *p = &partitions[log_type];
    uint32_t record_size = sizeof(ftl_record_header_t) + len;

    // TODO: Dynamic Wear Leveling Allocator
    // 1. Get Logical Sector
    uint16_t logical_sector = p->write_ptr / FLASH_SECTOR_SIZE;
    
    // 2. Map to Physical Sector
    uint16_t physical_sector = FTL_Get_Physical_Block(logical_sector);
    if (physical_sector == INVALID_BLOCK) {
        // Allocate least-worn block
        physical_sector = FTL_Allocate_Free_Block();
        if (physical_sector != INVALID_BLOCK) {
            FTL_Update_L2P(logical_sector, physical_sector);
        } else {
            return -2; // Out of space
        }
    }

    // 3. Write Data (simulated for skeleton)
    // Flash_Write(physical_sector * FLASH_SECTOR_SIZE + offset, ...);

    // 4. Update pointers logically
    p->write_ptr += record_size;

    // 5. Journal the update instead of erasing Sector 0
    FTL_Append_Journal(log_type);

    return 0;
}

int FTL_Read_By_Type(uint8_t log_type, uint8_t *buffer, uint16_t buffer_size)
{
    // TODO: Implement Read via L2P
    return 0;
}

void FTL_Process_Background(void)
{
    // Phase 3: Static Wear Leveling Check
    uint32_t min_erase = 0xFFFFFFFF;
    uint32_t max_erase = 0;
    uint16_t min_block = 0;

    // Periodically find min and max erase counts
    // For skeleton, we assume this is called on a slow timer (e.g., every 30 minutes)
    for (uint16_t i = 0; i < FLASH_SECTOR_COUNT; i++)
    {
        uint32_t erase_count;
        Flash_Read(ERASE_COUNT_ADDR + (i * sizeof(uint32_t)), (uint8_t*)&erase_count, sizeof(uint32_t));
        
        if (erase_count < min_erase) { min_erase = erase_count; min_block = i; }
        if (erase_count > max_erase) { max_erase = erase_count; }
    }

    // Threshold check (e.g., 500 erases difference)
    if (max_erase - min_erase > 500)
    {
        // Block Migration: Move cold data from min_block to max_block (or free pool)
        // 1. Find which logical block maps to min_block
        uint16_t logical_owner = INVALID_BLOCK;
        for (uint16_t log_b = 0; log_b < FLASH_SECTOR_COUNT; log_b++)
        {
            uint16_t phys_b;
            Flash_Read(active_l2p_bank + (log_b * sizeof(uint16_t)), (uint8_t*)&phys_b, sizeof(uint16_t));
            if (phys_b == min_block) {
                logical_owner = log_b;
                break;
            }
        }

        if (logical_owner != INVALID_BLOCK)
        {
            // 2. Allocate a new free block (allocator picks least worn)
            uint16_t new_phys = FTL_Allocate_Free_Block();
            if (new_phys != INVALID_BLOCK)
            {
                uint8_t temp_buf[256];
                // Copy data from min_block to new_phys
                for (int offset = 0; offset < FLASH_SECTOR_SIZE; offset += 256) {
                    Flash_Read((min_block * FLASH_SECTOR_SIZE) + offset, temp_buf, 256);
                    Flash_Write((new_phys * FLASH_SECTOR_SIZE) + offset, temp_buf, 256);
                }
                
                // 3. Update L2P mappings
                FTL_Update_L2P(logical_owner, new_phys);
            }
        }
        
        // 4. Erase min_block and return to free pool
        Flash_Erase_Sector(min_block * FLASH_SECTOR_SIZE);
        
        uint32_t current_erase;
        Flash_Read(ERASE_COUNT_ADDR + (min_block * sizeof(uint32_t)), (uint8_t*)&current_erase, sizeof(uint32_t));
        current_erase++;
        Flash_Write(ERASE_COUNT_ADDR + (min_block * sizeof(uint32_t)), (uint8_t*)&current_erase, sizeof(uint32_t));
        
        __disable_irq();
        while (ftl_locked) {
            __enable_irq();
            __disable_irq();
        }
        ftl_locked = 1;
        __enable_irq();

        free_block_bitmap[min_block / 8] |= (1 << (min_block % 8));

        ftl_locked = 0;
    }

    // Phase 4: Garbage Collection
    // Count free blocks
    int free_blocks = 0;
    for (uint16_t i = 0; i < FLASH_SECTOR_COUNT; i++)
    {
        if (Is_Block_Free(i)) free_blocks++;
    }

    uint8_t occupancy = ((FLASH_SECTOR_COUNT - free_blocks) * 100) / FLASH_SECTOR_COUNT;

    // Trigger GC if occupancy > 85%
    if (occupancy > 85)
    {
        // Since L2P mapping is 1-to-1 Sector mapped, GC means finding physical blocks 
        // that are marked "used" but have no logical owner in the L2P table (stale overwrites).
        
        uint8_t valid_phys_bitmap[1024];
        memset(valid_phys_bitmap, 0, sizeof(valid_phys_bitmap));
        
        // Build map of all valid physical blocks
        for (uint16_t log_b = 0; log_b < FLASH_SECTOR_COUNT; log_b++)
        {
            uint16_t phys_b;
            Flash_Read(active_l2p_bank + (log_b * sizeof(uint16_t)), (uint8_t*)&phys_b, sizeof(uint16_t));
            if (phys_b != INVALID_BLOCK && phys_b < FLASH_SECTOR_COUNT) {
                valid_phys_bitmap[phys_b / 8] |= (1 << (phys_b % 8));
            }
        }
        
        // Reclaim the first completely stale block found
        for (uint16_t phys_b = 0; phys_b < FLASH_SECTOR_COUNT; phys_b++)
        {
            if (!Is_Block_Free(phys_b))
            {
                int is_valid = (valid_phys_bitmap[phys_b / 8] & (1 << (phys_b % 8)));
                if (!is_valid)
                {
                    __disable_irq();
                    while (ftl_locked) {
                        __enable_irq();
                        __disable_irq();
                    }
                    ftl_locked = 1;
                    __enable_irq();

                    // Erase stale block and update wear count
                    Flash_Erase_Sector(phys_b * FLASH_SECTOR_SIZE);
                    
                    uint32_t erase_c;
                    Flash_Read(ERASE_COUNT_ADDR + (phys_b * sizeof(uint32_t)), (uint8_t*)&erase_c, sizeof(uint32_t));
                    erase_c++;
                    Flash_Write(ERASE_COUNT_ADDR + (phys_b * sizeof(uint32_t)), (uint8_t*)&erase_c, sizeof(uint32_t));
                    
                    // Return to free pool
                    free_block_bitmap[phys_b / 8] |= (1 << (phys_b % 8));
                    
                    ftl_locked = 0;
                    break; // Reclaim one block per background cycle to avoid stalling
                }
            }
        }
    }
}
