/*
 * parse.h
 *
 *  Created on: 24-Feb-2026
 *      Author: Yashwanth
 */

#ifndef INC_PARSE_H_
#define INC_PARSE_H_

#include "Config.h"
#include <string.h>


extern frame_parser_t parser;

/***********************************************  DMA RX Buffer **************************************************/
#define SPI_RX_BUF_SIZE 1024

extern uint8_t spi_rx_buffer[SPI_RX_BUF_SIZE];
extern uint8_t spi_tx_buffer[SPI_RX_BUF_SIZE];
extern volatile uint16_t spi_rx_old_pos;

/******************************************  Static Memory Pool  *************************************************/
#define MAX_OVERFLOW_NODES 64
void Init_Overflow_Pool(void);
void Free_Overflow_Node(log_node_t *node);

/********************************************  APIs  **********************************************************/

void Start_Recieve_DMA(void);

// Backpressure Status
void Set_SPI_Status_Busy(void);
void Set_SPI_Status_Ready(void);

// Poll DMA Pointer
void SPI1_ProcessBytes(void);

// Implement Frame Parser
void Frame_ParseByte(uint8_t byte);

// Push into ram_queue_t
uint8_t Queue_Push(master_frame_t *frame);

uint8_t Queue_Load_Config(master_frame_t *frame);

uint8_t Storage_Parse_Read_Request(master_frame_t *frame);

/*************************************  Miscellaneous  ******************************************************/
uint32_t Hardware_CRC32(const uint8_t *data, uint16_t length);

uint8_t Payload_CRC_OK(master_frame_t *frame);

uint8_t Header_CRC_OK(master_header_t *header);

#endif /* INC_PARSE_H_ */
