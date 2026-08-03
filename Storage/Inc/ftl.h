/*
 * ftl.h
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */
#ifndef FTL_H
#define FTL_H

#include "Config.h"

// Flash characteristics
#define FLASH_TOTAL_SIZE      (32UL * 1024 * 1024)
#define FLASH_SECTOR_SIZE     4096
#define FLASH_SECTOR_COUNT    (FLASH_TOTAL_SIZE / FLASH_SECTOR_SIZE)  // 8192

// Storage zones
#define FLASH_META_SIZE       (128 * 1024)  // 128 KB reserved at the end of flash
#define FLASH_META_START      (FLASH_TOTAL_SIZE - FLASH_META_SIZE)
#define FLASH_LOG_START       0
#define FLASH_LOG_SIZE        FLASH_META_START

// Persistent mapping table (in the meta zone)
#define L2P_TABLE_BANK_A      (FLASH_META_START)                              // 16 KB (8192 * 2 bytes)
#define L2P_TABLE_BANK_B      (FLASH_META_START + 16 * 1024)                  // 16 KB (Alternate Bank)
#define ERASE_COUNT_ADDR      (FLASH_META_START + 32 * 1024)                  // 32 KB (8192 * 4 bytes)
#define JOURNAL_START_ADDR    (FLASH_META_START + 64 * 1024)                  // 12 KB Journal
#define JOURNAL_SIZE          (12 * 1024)

#define INVALID_BLOCK         0xFFFF

// Caches (RAM)
#define L2P_CACHE_ENTRIES     2500 // ~10 KB in RAM (LRU)

typedef struct {
    uint16_t logical_block;
    uint16_t physical_block;
} l2p_cache_entry_t;

// Logical partitions
typedef enum
{
    FTL_MODE_STOP = 0,
    FTL_MODE_CIRCULAR
} ftl_mode_t;

typedef struct
{
    uint8_t  ftl_mode;
    uint32_t start_logical_addr;
    uint32_t end_logical_addr;
    uint32_t write_ptr;     // Logical write pointer
    uint32_t oldest_ptr;    // Logical oldest pointer
    uint32_t logical_sectors;
} ftl_partition_t;

// stored in the partition followed by the block header and payload
typedef struct __attribute__((packed))
{
    log_type_t  log_type;
    uint16_t    length;
    uint32_t    crc;
} ftl_record_header_t;

typedef struct __attribute__((packed))
{
    uint32_t partition_id;
    uint32_t state_version;
    uint32_t write_ptr;
    uint32_t oldest_ptr;
    uint32_t timestamp;
    uint64_t last_used_sequence; // Nonce watermark
    uint32_t crc32;
} journal_entry_t;

typedef struct __attribute__((packed))
{
    uint32_t sequence_number;
    uint64_t last_used_sequence;
    uint32_t crc32;
} l2p_header_t;

extern ftl_partition_t partitions[PARTITION_MAX];

// Thread Safety (Mutex equivalent)
extern volatile uint8_t ftl_locked;

/************************************************ API *************************************************************/

int FTL_Init(void);
int FTL_Config(config_payload_t *config);
int FTL_Append(uint8_t log_type, uint8_t *data, uint16_t len);
int FTL_Read_By_Type(uint8_t log_type, uint8_t *buffer, uint16_t buffer_size);
void FTL_Process_Background(void); // Periodic GC and WL check

#endif
