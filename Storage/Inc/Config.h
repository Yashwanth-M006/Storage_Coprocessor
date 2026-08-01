/*
 * Config.h
 *
 *  Created on: 23-Feb-2026
 *      Author: Yashwanth
 */

#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_



#include "stm32f4xx_hal.h"
#include "main.h"

#include <stdint.h>
#include <string.h>

/****************************************  Header   *********************************************************/
#define SOF_BYTE              0xA5

#define MAX_PAYLOAD_SIZE      1024
#define MASTER_HEADER_SIZE    8      // SOF + CMD + SEQ + LEN + CRC
#define MASTER_OVERHEAD_SIZE  10     // Header + Payload CRC


/************************************* Commands  ******************************************************/

#define INIT_CONFIG			0x01U;
#define WRITE_LOG			0x02U;
#define READ_REQUEST		0x03U;
#define GET_STATUS 			0x04U;


/*************************************  Queue  **********************************************************/

#define QUEUE_TYPES        4
#define PRIORITY_LEVELS    8
#define MAX_LOG_SIZE       256   // bytes
#define MAX_RECORDS		   8

/****************************************** Master Frame **********************************************************/

typedef struct __attribute__((packed))
{
    uint8_t  sof;            // 0xA5
    //uint8_t version;
    uint8_t  cmd;			// Command ID
    //uint16_t flags;
    uint64_t seq;            // Sequence number
    uint16_t payload_len;    // Length of payload
    uint16_t header_crc;     // CRC of above fields

} master_header_t;


typedef struct
{
    master_header_t header;
    uint8_t  payload[MAX_PAYLOAD_SIZE];
    uint16_t payload_crc;
    uint8_t  frame_valid;

} master_frame_t;


/********************************************* Pay load Structures *************************************************/

// Configuration
typedef struct __attribute__((packed))
{
    uint8_t  enc_comp_mode;      // Bit0: Encryption, Bit1: Compression
    uint8_t  storage_mode;       // Bit0-3: Circular per queue
    uint8_t  erase_policy;       // 0: Lazy erase, 1: Pre-erase
    uint8_t  reserved1;
    uint16_t partition_map;      // 3 bits per log type
    uint16_t max_log_size;       // Max allowed data length
    //uint32_t storage_limit_bytes;

} config_payload_t;

extern config_payload_t *out_config;

// Write Log
typedef struct __attribute__((packed))
{
    uint8_t  log_type;       // 0: Burst, 1: Interrupt, 2: Event, 3: Time
    uint8_t  priority;       // Lower 3 bits used (0–7)
    uint8_t  subtype;        // Log classification
    uint32_t timestamp;      // System uptime (ms)
    uint16_t data_length;    // Length of data[]
    // uint8_t data[data_length];  // Variable length
    // uint16_t data_crc;          // CRC of metadata + data

} write_log_header_t;

// Read request
typedef struct __attribute__((packed))
{
    uint8_t  log_type;          // Queue to read
    uint8_t  priority_filter;   // 0–7 or 0xFF (all)
    uint8_t  read_mode;         // 0=index,1=time,2=latestN,3=from_ptr
    uint8_t  reserved;
    uint32_t start_param;
    uint32_t end_param;
    uint16_t max_records;

} read_request_t;

extern read_request_t *out_req;
// Status



/***************************************  Frame Parsing   ****************************************************/

// RX State machine
typedef enum
{
    FRAME_WAIT_SOF = 0,     // Byte driven
    FRAME_READ_HEADER,
    FRAME_READ_PAYLOAD,
    FRAME_READ_PAYLOAD_CRC,
    FRAME_COMPLETE,
    FRAME_ERROR

} frame_state_t;

typedef struct
{
    frame_state_t state;
    uint16_t header_index;
    uint16_t payload_index;
    uint8_t  crc_index;
    master_frame_t frame;

} frame_parser_t;



/***********************************************  Queues *****************************************************/


typedef struct
{
    uint8_t  log_type;
    uint8_t  priority;
    uint8_t  subtype;
    uint32_t timestamp;
    uint16_t length;
    uint8_t  data[MAX_LOG_SIZE];

} log_record_t;

typedef struct
{
    log_record_t records[MAX_RECORDS];
    uint16_t head;
    uint16_t tail;
    uint16_t count;

} ram_queue_t;

extern ram_queue_t log_queues[QUEUE_TYPES][PRIORITY_LEVELS];// log_queues[type][priority] is a separate independent queue
//log_queues[1][7].head -- ACCESSING

extern log_record_t congif_record;

/**********************************************  Storing Header  *************************************************************/
// Compression block header
typedef struct
{
    uint16_t original_size;
    uint16_t compressed_size;
    uint8_t  compression_type;   // 0 = none, 1 = delta+rle
    uint8_t  element_size;       // 1,2,4 bytes
    uint32_t base_value;         // first element

    uint8_t  nonce[12];
    uint32_t crc;

} store_block_header_t;

/************************************************** Log types and partitions  *************************************/
typedef enum
{
    LOG_TYPE_BURST = 0,
    LOG_TYPE_INTERRUPT,
    LOG_TYPE_EVENT,
    LOG_TYPE_TIME,

	LOG_TYPE_MAX

} log_type_t;

typedef enum
{
    PARTITION_BURST = 0,
    PARTITION_INTERRUPT,
    PARTITION_EVENT,
    PARTITION_TIME,

    PARTITION_MAX
} partition_id_t;


typedef enum
{
	ENCRYPTION_DISABLE = 0,
	ENCRYPTION_ENABLE

}encryption_t;

typedef enum
{
	COMPRESSION_DISABLE = 0,
	COMPRESSION_ENABLE = 1

}compression_t;


/********************************************** Includes *********************************************************/

#include "parse.h"
#include "compression.h"
#include "encryption.h"
#include "ftl.h"
#include "spi_flash.h"
#include "storage_main.h"

#endif /* INC_CONFIG_H_ */
