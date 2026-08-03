/*
 * parse.c
 *
 *  Created on: 24-Feb-2026
 *      Author: Yashwanth
 */

#include "parse.h"
#include <stdlib.h>
#include "storage_main.h"
#include "ftl.h"

uint8_t type;

/* Static Memory Pool for Overflow Nodes */
static log_node_t overflow_pool[MAX_OVERFLOW_NODES];
static log_node_t *overflow_free_list = NULL;
static uint8_t pool_initialized = 0;

void Init_Overflow_Pool(void)
{
    for (int i = 0; i < MAX_OVERFLOW_NODES - 1; i++)
    {
        overflow_pool[i].next = &overflow_pool[i + 1];
    }
    overflow_pool[MAX_OVERFLOW_NODES - 1].next = NULL;
    overflow_free_list = &overflow_pool[0];
    pool_initialized = 1;
}

void Set_SPI_Status_Busy(void)
{
    memset(spi_tx_buffer, 0xFF, SPI_RX_BUF_SIZE);
}

void Set_SPI_Status_Ready(void)
{
    memset(spi_tx_buffer, 0x00, SPI_RX_BUF_SIZE);
}

log_node_t *Alloc_Overflow_Node(void)
{
    if (overflow_free_list == NULL) {
        Set_SPI_Status_Busy();
        return NULL;
    }
    
    log_node_t *node = overflow_free_list;
    overflow_free_list = overflow_free_list->next;
    
    if (overflow_free_list == NULL) {
        Set_SPI_Status_Busy();
    }
    return node;
}

void Free_Overflow_Node(log_node_t *node)
{
    if (node == NULL) return;
    
    int was_empty = (overflow_free_list == NULL);
    
    node->next = overflow_free_list;
    overflow_free_list = node;
    
    if (was_empty) {
        Set_SPI_Status_Ready();
    }
}

frame_parser_t parser;

extern SPI_HandleTypeDef hspi1;

uint8_t spi_rx_buffer[SPI_RX_BUF_SIZE];

uint8_t spi_tx_buffer[SPI_RX_BUF_SIZE] = {0};

volatile uint16_t spi_rx_old_pos = 0;

void Start_Recieve_DMA(void)
{
    Set_SPI_Status_Ready();
    HAL_SPI_TransmitReceive_DMA(&hspi1, spi_tx_buffer, spi_rx_buffer, SPI_RX_BUF_SIZE);
}

void SPI1_ProcessBytes(void)
{
    uint16_t current_pos = SPI_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(hspi1.hdmarx);

    if (current_pos != spi_rx_old_pos)
    {
        if (current_pos > spi_rx_old_pos)
        {
            for (uint16_t i = spi_rx_old_pos; i < current_pos; i++)
                Frame_ParseByte(spi_rx_buffer[i]);
        }
        else
        {
            for (uint16_t i = spi_rx_old_pos; i < SPI_RX_BUF_SIZE; i++)
                Frame_ParseByte(spi_rx_buffer[i]);

            for (uint16_t i = 0; i < current_pos; i++)
                Frame_ParseByte(spi_rx_buffer[i]);
        }

        spi_rx_old_pos = current_pos;
    }
}

void Frame_ParseByte(uint8_t byte)
{
    switch(parser.state)
    {
        case FRAME_WAIT_SOF:

            if (byte == 0xA5)
            {
                memset(&parser.frame, 0, sizeof(master_frame_t));
                parser.frame.header.sof = byte;

                parser.header_index = 1; // sof already read
                parser.state = FRAME_READ_HEADER;
            }
            break;

        case FRAME_READ_HEADER:

            ((uint8_t*)&parser.frame.header)[parser.header_index++] = byte;

            if (parser.header_index == sizeof(master_header_t))
            {
                // Validate header CRC here
                if (!Header_CRC_OK(&parser.frame.header))
                {
                    parser.state = FRAME_WAIT_SOF;
                    break;
                }

                if (parser.frame.header.payload_len > MAX_PAYLOAD_SIZE)
                {
                    parser.state = FRAME_WAIT_SOF;
                    break;
                }

                parser.payload_index = 0;
                parser.state = FRAME_READ_PAYLOAD;
            }
            break;

        case FRAME_READ_PAYLOAD:

            parser.frame.payload[parser.payload_index++] = byte;

            if (parser.payload_index == parser.frame.header.payload_len)
            {
                parser.crc_index = 0;
                parser.frame.payload_crc = 0;
                parser.state = FRAME_READ_PAYLOAD_CRC;
            }
            break;

        case FRAME_READ_PAYLOAD_CRC:

            parser.frame.payload_crc = (parser.frame.payload_crc << 8) | byte;

            if (++parser.crc_index == 4)
            {
                if (Payload_CRC_OK(&parser.frame))
                {
                    parser.frame.frame_valid = 1;
                    if(parser.frame.header.cmd == 0x02U)
                    {
                    	Queue_Push(&parser.frame);
                    }else if(parser.frame.header.cmd == 0x01U)
                    {
                    	Queue_Load_Config(&parser.frame);
                    	Storage_System_Init(out_config);

                    }else if (parser.frame.header.cmd == 0x03U)
                    {
                        if (Storage_Parse_Read_Request(&parser.frame))
                        {
                            // Request parsed successfully, out_req is populated.
                            Storage_Process_Read(out_req);
                        }
                    }
                }
                parser.state = FRAME_WAIT_SOF;
            }
            break;

        default:
            parser.state = FRAME_WAIT_SOF;
            break;
    }
}


uint8_t Queue_Push(master_frame_t *frame)
{
    if (frame->header.payload_len < sizeof(write_log_header_t))
        return 0;

    write_log_header_t log_hdr;
    memcpy(&log_hdr, frame->payload, sizeof(write_log_header_t));

    type     = log_hdr.log_type;
    uint8_t priority = log_hdr.priority & 0x07;

    if (type >= QUEUE_TYPES)
        return 0;

    if (priority >= PRIORITY_LEVELS)
        return 0;

    ram_queue_t *q = &log_queues[type][priority];

    uint16_t data_len = log_hdr.data_length;

    if (data_len > MAX_LOG_SIZE)
        return 0;

    if ((sizeof(write_log_header_t) + data_len) > frame->header.payload_len)
        return 0;

    if (q->count >= MAX_RECORDS)
    {
        if (!pool_initialized) {
            Init_Overflow_Pool();
        }

        log_node_t *node = Alloc_Overflow_Node();
        if (node == NULL) return 0; // Drop packet (Option A)
        
        node->record.log_type  = log_hdr.log_type;
        node->record.priority  = priority;
        node->record.subtype   = log_hdr.subtype;
        node->record.timestamp = log_hdr.timestamp;
        node->record.length = data_len;
        memcpy(node->record.data, frame->payload + sizeof(write_log_header_t), data_len);
        node->next = NULL;
        
        if (q->overflow_tail == NULL) {
            q->overflow_head = node;
            q->overflow_tail = node;
        } else {
            q->overflow_tail->next = node;
            q->overflow_tail = node;
        }
        return 1;
    }

    log_record_t *rec = &q->records[q->head];

    // Store metadata
    rec->log_type  = log_hdr.log_type;
    rec->priority  = priority;
    rec->subtype   = log_hdr.subtype;
    rec->timestamp = log_hdr.timestamp;

    // Store payload
    rec->length = data_len;

    memcpy(rec->data, frame->payload + sizeof(write_log_header_t), data_len);

    q->head = (q->head + 1) % MAX_RECORDS;
    q->count++;

    return 1;
}

uint8_t Queue_Load_Config(master_frame_t *frame)
{
    if (frame == NULL || out_config == NULL)
        return 0;

    /* Validate payload length */
    if (frame->header.payload_len < sizeof(config_payload_t))
        return 0;

    /* Extract payload into struct */
    memcpy(out_config, frame->payload, sizeof(config_payload_t));

    /* ---------------- Validation ---------------- */

    /* Validate partition_map */
    uint16_t map = out_config->partition_map;
    uint8_t total_percent_units = 0;
    uint32_t total_calculated_size = 0;

    for (int i = 0; i < QUEUE_TYPES; i++)
    {
        uint8_t percent = (map >> (i * 3)) & 0x07;
        total_percent_units += percent;
        
        // Calculate the physical size this partition would take
        uint32_t size = (FLASH_LOG_SIZE * (percent * 10)) / 100;
        size = (size / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
        total_calculated_size += size;
    }

    /* 1. Sum of partition percentages must equal exactly 100% (10 units of 10%) */
    if (total_percent_units != 10)
        return 0;

    /* 2. Start addr + total calculated size must not exceed the physical flash limit */
    if (FLASH_LOG_START + total_calculated_size > FLASH_TOTAL_SIZE)
        return 0;

    /* Validate max_log_size */
    if (out_config->max_log_size == 0 || out_config->max_log_size > MAX_LOG_SIZE)
        return 0;

    /* Optional: mask reserved bits */
    out_config->enc_comp_mode &= 0x03;   // only bit0, bit1 valid
    out_config->storage_mode  &= 0x1F;   // bits 0–4 valid

    return 1;
}

uint8_t Storage_Parse_Read_Request(master_frame_t *frame)
{
    if (frame == NULL || out_req == NULL)
        return 0;

    /* Validate payload length */
    if (frame->header.payload_len < sizeof(read_request_t))
        return 0;

    /* Extract payload */
    memcpy(out_req, frame->payload, sizeof(read_request_t));

    /* ---------------- Validation ---------------- */

    /* Validate log type */
    if (out_req->log_type >= LOG_TYPE_MAX)
        return 0;

    /* Validate priority filter */
    if ((out_req->priority_filter >= PRIORITY_LEVELS) && (out_req->priority_filter != 0xFF))
        return 0;

    /* Validate read mode */
    if (out_req->read_mode > 3)
        return 0;

    /* Validate max_records */
    if (out_req->max_records == 0 || out_req->max_records > MAX_RECORDS)
        return 0;

    return 1;
}

/*******************************   CRC   *************************************************************/
uint32_t Hardware_CRC32(const uint8_t *data, uint16_t length)
{
    // Reset CRC calculation
    CRC->CR = CRC_CR_RESET;

    uint32_t *p32 = (uint32_t*)data;
    for (int i = 0; i < length / 4; i++)
    {
        CRC->DR = p32[i];
    }
    
    // Handle remaining bytes by padding with 0s
    uint8_t rem = length % 4;
    if (rem > 0)
    {
        uint32_t temp = 0;
        memcpy(&temp, data + (length / 4) * 4, rem);
        CRC->DR = temp;
    }
    
    return CRC->DR;
}

uint8_t Payload_CRC_OK(master_frame_t *frame)
{
    uint32_t calculated_crc =
        Hardware_CRC32(frame->payload,
                    frame->header.payload_len);

    if (calculated_crc == frame->payload_crc)
        return 1;
    else
        return 0;
}

uint8_t Header_CRC_OK(master_header_t *header)
{
    // Calculate CRC over first 12 bytes (excluding header_crc)
    uint32_t calculated_crc =
        Hardware_CRC32((uint8_t*)header,
                    sizeof(master_header_t) - sizeof(uint32_t));

    if (calculated_crc == header->header_crc)
        return 1;
    else
        return 0;
}
