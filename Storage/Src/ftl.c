/*
 * ftl.c
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#include "ftl.h"


/* Flash driver prototypes (implement separately) */
//extern int Flash_Write(uint32_t addr, uint8_t *data, uint32_t len);
//extern int Flash_Read(uint32_t addr, uint8_t *data, uint32_t len);
//extern int Flash_Erase_Sector(uint32_t addr);

/*********************************************** States ***************************************************/

ftl_partition_t partitions[PARTITION_MAX];
//ftl_superblock_t superblock;
ftl_superblock_handle_t *psuperblock_handle;
//ftl_mode_t ftl_mode;

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
    if (psuperblock_handle->storage_mode & (1 << 4))
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
        if (psuperblock_handle->storage_mode & (1 << p))
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
        psuperblock_handle->ftl_super->partition_percent[p] = (psuperblock_handle->partition_map >> (p * 3)) & 0x07;
    }

    extract_partition_modes();

    for (int i = 0; i < PARTITION_MAX; i++)
    {
        uint32_t percent = psuperblock_handle->ftl_super->partition_percent[i] * 10;
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
    Flash_Read(0, (uint8_t*)psuperblock_handle, sizeof(ftl_superblock_handle_t));

    if (psuperblock_handle->ftl_super->magic != FTL_MAGIC)
        return -1;

    build_partitions();

    /* Restore pointers */
    for (partition_id_t p = PARTITION_BURST; p < PARTITION_MAX; p++)
    {
        partitions[p].write_ptr  = psuperblock_handle->ftl_super->write_ptrs[p];
        partitions[p].oldest_ptr = psuperblock_handle->ftl_super->oldest_ptrs[p];
    }

    return 0;
}

int FTL_Config(config_payload_t *config)
{
    memset(psuperblock_handle->ftl_super, 0, sizeof(psuperblock_handle));

    psuperblock_handle->ftl_super->magic = FTL_MAGIC;
    psuperblock_handle->partition_map = config->partition_map;
    psuperblock_handle->storage_mode  = config->storage_mode;
    //superblock.storage_limit = config->storage_limit;

    build_partitions();   // Build from NEW config


    /* Initialize pointers to zero */
    for (partition_id_t p = PARTITION_BURST; p < PARTITION_MAX; p++)
    {
        psuperblock_handle->ftl_super->write_ptrs[p]  = 0;
        psuperblock_handle->ftl_super->oldest_ptrs[p] = 0;
    }

    Flash_Erase_Sector(0);
    Flash_Write(0, (uint8_t*)psuperblock_handle, sizeof(ftl_superblock_handle_t));

    return 0;
}


//Writes one log record into the correct flash partition.
int FTL_Append(uint8_t log_type, uint8_t *data, uint16_t len)
{
    if (log_type >= LOG_TYPE_MAX)
        return -1;

    //uint8_t partition_id = psuperblock_handle->ftl_super->log_to_partition[log_type];

    if (log_type >= PARTITION_MAX)
        return -1;

    ftl_partition_t *p = &partitions[log_type];

    uint32_t record_size = sizeof(ftl_record_header_t) + len;

    ftl_record_header_t header;

    if (!space_available(p, record_size))
    {
        if ( p ->ftl_mode == FTL_MODE_STOP)
            return -2;

        erase_oldest_sector(p);
    }


    //ftl_record_header_t header;
    header.log_type = log_type;
    header.length = len;
    header.crc = 0;  // Add real CRC if needed

    Flash_Write(p->write_ptr, (uint8_t*)&header, sizeof(header));
    Flash_Write(p->write_ptr + sizeof(header), data, len);

    p->write_ptr += record_size;

    if (p->ftl_mode == FTL_MODE_CIRCULAR)
    {
        if (p->write_ptr >= p->end_addr)
            p->write_ptr = p->start_addr;
    }
    psuperblock_handle->ftl_super->write_ptrs[log_type]  = p->write_ptr;
    psuperblock_handle->ftl_super->oldest_ptrs[log_type] = p->oldest_ptr;

    // update super block handle
    Flash_Write(0, (uint8_t*)psuperblock_handle, sizeof(ftl_superblock_handle_t));

    return 0;
}

int FTL_Read_By_Type(uint8_t log_type, uint8_t *buffer, uint16_t buffer_size)
{
    if (log_type >= LOG_TYPE_MAX)
        return -1;

    //uint8_t partition_id = psuperblock_handle->ftl_super->log_to_partition[log_type];
    ftl_partition_t *p = &partitions[log_type];

    uint32_t addr = p->oldest_ptr;

    while (addr != p->write_ptr)
    {
        ftl_record_header_t header;

        Flash_Read(addr, (uint8_t*)&header, sizeof(header));


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
