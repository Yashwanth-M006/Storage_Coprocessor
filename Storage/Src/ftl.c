/*
 * ftl.c
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#include "ftl.h"
#include "parse.h"
#include "spi_flash.h"


/* Flash driver prototypes (implement separately) */
//extern int Flash_Write(uint32_t addr, uint8_t *data, uint32_t len);
//extern int Flash_Read(uint32_t addr, uint8_t *data, uint32_t len);
//extern int Flash_Erase_Sector(uint32_t addr);

/*********************************************** States ***************************************************/

ftl_partition_t partitions[PARTITION_MAX];
ftl_superblock_handle_t superblock_handle;
uint32_t active_sb_sector = SUPERBLOCK_SECTOR_0_ADDR;

uint32_t Calculate_Superblock_CRC32(ftl_superblock_handle_t *handle)
{
    uint16_t data_len = sizeof(ftl_superblock_t) - sizeof(uint32_t);
    return Hardware_CRC32((uint8_t*)&handle->ftl_super, data_len);
}

void FTL_Sync_Superblock(void)
{
    superblock_handle.ftl_super.version++;
    superblock_handle.ftl_super.crc = Calculate_Superblock_CRC32(&superblock_handle);

    uint32_t inactive_sector = (active_sb_sector == SUPERBLOCK_SECTOR_0_ADDR) ? 
                                SUPERBLOCK_SECTOR_1_ADDR : SUPERBLOCK_SECTOR_0_ADDR;

    Flash_Erase_Sector(inactive_sector);
    Flash_Write(inactive_sector, (uint8_t*)&superblock_handle, sizeof(ftl_superblock_handle_t));

    active_sb_sector = inactive_sector;
}

/************************************************ Internal helpers **********************************************/
static uint32_t align_sector(uint32_t addr)
{
    return (addr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
}


static int space_available(ftl_partition_t *p, uint32_t size)
{
    if (p->write_ptr >= p->oldest_ptr)
    {
        return (p->write_ptr + size < p->end_addr) ||
               (p->start_addr + size < p->oldest_ptr);
    }
    else
    {
        return (p->write_ptr + size < p->oldest_ptr);
    }
}


static void erase_oldest_sector(ftl_partition_t *p)
{
    uint32_t sector_addr = align_sector(p->oldest_ptr);

    Flash_Erase_Sector(sector_addr);

    p->oldest_ptr = sector_addr + FLASH_SECTOR_SIZE;

    if (p->oldest_ptr >= p->end_addr)
        p->oldest_ptr = p->start_addr;
}

void extract_partition_modes()
{
    /* Check global circular override (bit 4) */
    if (superblock_handle.storage_mode & (1 << 4))
    {
        for (partition_id_t p = PARTITION_BURST; p < PARTITION_MAX; p++)
        {
            partitions[p].ftl_mode = FTL_MODE_CIRCULAR;
        }
        return;
    }

    /* Otherwise set per-partition mode */
    for (partition_id_t p = PARTITION_BURST;  p < PARTITION_MAX; p++)
    {
        if (superblock_handle.storage_mode & (1 << p))
            partitions[p].ftl_mode = FTL_MODE_CIRCULAR;
        else
            partitions[p].ftl_mode = FTL_MODE_STOP;
    }
}

static void build_partitions()
{
    uint32_t base = FLASH_LOG_START;
    uint32_t total_log_size = FLASH_LOG_SIZE;

    // extract_partition_percent
    for (partition_id_t p = PARTITION_BURST; p < PARTITION_MAX; p++)
    {
        superblock_handle.ftl_super.partition_percent[p] = (superblock_handle.partition_map >> (p * 3)) & 0x07;
    }

    extract_partition_modes();

    for (int i = 0; i < PARTITION_MAX; i++)
    {
        uint32_t percent = superblock_handle.ftl_super.partition_percent[i] * 10;
        uint32_t size = (total_log_size * percent) / 100;

        size = (size / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

        partitions[i].start_addr = base;
        partitions[i].end_addr   = base + size;
        partitions[i].sector_count = size / FLASH_SECTOR_SIZE;

        partitions[i].write_ptr  = base;
        partitions[i].oldest_ptr = base;

        base += size;
    }
}

/********************************************** Public API ******************************************************/

//Load previously saved flash metadata and rebuild runtime state.
int FTL_Init(void)
{
    ftl_superblock_handle_t sb_0, sb_1;
    
    Flash_Read(SUPERBLOCK_SECTOR_0_ADDR, (uint8_t*)&sb_0, sizeof(ftl_superblock_handle_t));
    Flash_Read(SUPERBLOCK_SECTOR_1_ADDR, (uint8_t*)&sb_1, sizeof(ftl_superblock_handle_t));

    uint8_t sb_0_valid = (sb_0.ftl_super.magic == FTL_MAGIC) && 
                         (Calculate_Superblock_CRC32(&sb_0) == sb_0.ftl_super.crc);
    
    uint8_t sb_1_valid = (sb_1.ftl_super.magic == FTL_MAGIC) && 
                         (Calculate_Superblock_CRC32(&sb_1) == sb_1.ftl_super.crc);

    if (sb_0_valid && sb_1_valid)
    {
        if (sb_0.ftl_super.version >= sb_1.ftl_super.version)
        {
            active_sb_sector = SUPERBLOCK_SECTOR_0_ADDR;
            superblock_handle = sb_0;
        }
        else
        {
            active_sb_sector = SUPERBLOCK_SECTOR_1_ADDR;
            superblock_handle = sb_1;
        }
    }
    else if (sb_0_valid)
    {
        active_sb_sector = SUPERBLOCK_SECTOR_0_ADDR;
        superblock_handle = sb_0;
    }
    else if (sb_1_valid)
    {
        active_sb_sector = SUPERBLOCK_SECTOR_1_ADDR;
        superblock_handle = sb_1;
    }
    else
    {
        return -1; // Both superblocks invalid
    }

    build_partitions();

    /* Restore pointers */
    for (partition_id_t p = PARTITION_BURST; p < PARTITION_MAX; p++)
    {
        partitions[p].write_ptr  = superblock_handle.ftl_super.write_ptrs[p];
        partitions[p].oldest_ptr = superblock_handle.ftl_super.oldest_ptrs[p];
    }

    return 0;
}

int FTL_Config(config_payload_t *config)
{
    memset(&superblock_handle.ftl_super, 0, sizeof(ftl_superblock_t));

    superblock_handle.ftl_super.magic = FTL_MAGIC;
    superblock_handle.ftl_super.erase_policy = config->erase_policy;
    superblock_handle.partition_map = config->partition_map;
    superblock_handle.storage_mode  = config->storage_mode;

    build_partitions();   // Build from NEW config


    /* Initialize pointers to zero */
    for (partition_id_t p = PARTITION_BURST; p < PARTITION_MAX; p++)
    {
        superblock_handle.ftl_super.write_ptrs[p]  = partitions[p].start_addr;
        superblock_handle.ftl_super.oldest_ptrs[p] = partitions[p].start_addr;
    }

    /* Pre-Erase implementation */
    if (config->erase_policy == 1)
    {
        uint32_t end_limit = FLASH_LOG_START;
        for (int i = 0; i < PARTITION_MAX; i++) 
        {
            if (partitions[i].end_addr > end_limit)
                end_limit = partitions[i].end_addr;
        }
        for (uint32_t addr = FLASH_LOG_START; addr < end_limit; addr += FLASH_SECTOR_SIZE)
        {
            Flash_Erase_Sector(addr);
        }
    }

    superblock_handle.ftl_super.version = 0;

    FTL_Sync_Superblock();

    return 0;
}


//Writes one log record into the correct flash partition.
int FTL_Append(uint8_t log_type, uint8_t *data, uint16_t len)
{
    if (log_type >= LOG_TYPE_MAX)
        return -1;

    ftl_partition_t *p = &partitions[log_type];

    uint32_t record_size = sizeof(ftl_record_header_t) + len;

    if (!space_available(p, record_size))
    {
        if ( p ->ftl_mode == FTL_MODE_STOP)
            return -2;

        erase_oldest_sector(p);
    }

    /* Wrap Boundary Check */
    if (p->write_ptr + record_size > p->end_addr)
    {
        /* Write 0xFF padding if there is space */
        uint32_t remaining = p->end_addr - p->write_ptr;
        if (remaining > 0)
        {
            uint8_t padding = 0xFF;
            for (uint32_t i = 0; i < remaining; i++)
            {
                Flash_Write(p->write_ptr + i, &padding, 1);
            }
        }
        /* Wrap back to start */
        p->write_ptr = p->start_addr;
    }

    /* Lazy Erase Logic */
    if (superblock_handle.ftl_super.erase_policy == 0)
    {
        uint32_t current_sector = align_sector(p->write_ptr);
        uint32_t next_sector = align_sector(p->write_ptr + record_size - 1);
        
        // If we are at the exact beginning of a sector, it needs to be erased
        if (p->write_ptr == current_sector)
        {
            Flash_Erase_Sector(current_sector);
        }
        
        // If the record crosses into the next sector, erase it too
        if (next_sector > current_sector)
        {
            Flash_Erase_Sector(next_sector);
        }
    }

    ftl_record_header_t header;
    header.log_type = log_type;
    header.length = len;
    header.crc = 0;  // Add real CRC if needed

    Flash_Write(p->write_ptr, (uint8_t*)&header, sizeof(header));
    Flash_Write(p->write_ptr + sizeof(header), data, len);

    p->write_ptr += record_size;

    superblock_handle.ftl_super.write_ptrs[log_type]  = p->write_ptr;
    superblock_handle.ftl_super.oldest_ptrs[log_type] = p->oldest_ptr;

    // update super block handle
    FTL_Sync_Superblock();

    return 0;
}

int FTL_Read_By_Type(uint8_t log_type, uint8_t *buffer, uint16_t buffer_size)
{
    if (log_type >= LOG_TYPE_MAX)
        return -1;

    ftl_partition_t *p = &partitions[log_type];

    uint32_t addr = p->oldest_ptr;

    while (addr != p->write_ptr)
    {
        ftl_record_header_t header;

        Flash_Read(addr, (uint8_t*)&header, sizeof(header));
        
        if (header.log_type == 0xFF)
        {
            // Pad record, jump to start
            addr = p->start_addr;
            continue;
        }

        if (header.log_type == log_type)
        {
            if (header.length > buffer_size)
                return -3;

            Flash_Read(addr + sizeof(header), buffer, header.length);
            return header.length;
        }

        addr += sizeof(header) + header.length;

        if (addr >= p->end_addr)
            addr = p->start_addr;
    }

    return 0;
}
