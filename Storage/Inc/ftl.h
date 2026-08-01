/*
 * ftl.h
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */
#ifndef FTL_H
#define FTL_H

#include "Config.h"


#define FLASH_TOTAL_SIZE      (32UL * 1024 * 1024)
#define FLASH_SECTOR_SIZE     4096
#define FLASH_META_SECTORS    16

#define FLASH_META_SIZE       (FLASH_META_SECTORS * FLASH_SECTOR_SIZE)
#define FLASH_LOG_START       FLASH_META_SIZE
#define FLASH_LOG_SIZE        (FLASH_TOTAL_SIZE - FLASH_META_SIZE)

#define FTL_MAGIC             0xA5A5F1F1
#define FTL_RECORD_MAGIC      0xDEADBEEF

typedef enum
{
    FTL_MODE_STOP = 0,
    FTL_MODE_CIRCULAR

} ftl_mode_t;

typedef struct
{
	uint8_t  ftl_mode;
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t write_ptr;
    uint32_t oldest_ptr;    // Used only in circular mode.
    uint32_t sector_count;

} ftl_partition_t;

// stored in the partition followed by the block header and payload
typedef struct __attribute__((packed))
{
    log_type_t 	log_type;
    uint16_t 	length;
    uint32_t 	crc;

} ftl_record_header_t;

//  stored  at beginning of flash
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t  partition_percent[PARTITION_MAX];   // How much flash memory each partition gets
    uint8_t  log_to_partition[LOG_TYPE_MAX];	 // Which partition each log type should go into.
    uint8_t  partiton_mode[PARTITION_MAX];		// The storage mode of each partition.
    uint32_t write_ptrs[PARTITION_MAX];
    uint32_t oldest_ptrs[PARTITION_MAX];
    uint32_t crc;

} ftl_superblock_t;

// just a handle structure
typedef struct{
	ftl_superblock_t *ftl_super;
	uint8_t  storage_mode;        // bitmask
    uint16_t partition_map;       // 4 bits per partition
    //uint32_t storage_limit_bytes; // total usable bytes

}ftl_superblock_handle_t ;



extern ftl_partition_t partitions[PARTITION_MAX];
//extern ftl_superblock_t superblock;
extern ftl_superblock_handle_t superblock_handle ;
//extern ftl_mode_t ftl_mode;



/************************************************ API *************************************************************/

int FTL_Init(void);
int FTL_Config(config_payload_t *config);
int  FTL_Append(uint8_t log_type, uint8_t *data, uint16_t len);
int  FTL_Read_By_Type(uint8_t log_type, uint8_t *buffer, uint16_t buffer_size);

//void FTL_SetMode(ftl_mode_t mode);

#endif
