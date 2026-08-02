/*
 * storage_main.c
 *
 *  Created on: 27-Feb-2026
 *      Author: Yashwanth
 */

#include "storage_main.h"
#include <stdlib.h>
#include "ftl.h"
#include "spi_flash.h"
#include "encryption.h"
#include "compression.h"
#include "parse.h"

encryption_t encryption;
compression_t compression;

void Storage_System_Init(config_payload_t *cfg)
{
    if (cfg == NULL)
        return;

    uint8_t enc_comp_mode = cfg->enc_comp_mode;
    encryption = (enc_comp_mode & 0x01) ? ENCRYPTION_ENABLE : ENCRYPTION_DISABLE;

    compression = (enc_comp_mode & 0x02) ? COMPRESSION_ENABLE : COMPRESSION_DISABLE;

    Flash_Init();

    if (FTL_Init() != 0)
	{
		/* No valid superblock → configure new */

		/* Apply configuration */
		FTL_Config(cfg);

		/* Reinitialize after config */
		FTL_Init();
	}

}

void Storage_Process_Command()
{
	Start_Recieve_DMA();

	SPI1_ProcessBytes();

    for (int type = 0; type < QUEUE_TYPES; type++) {
        for (int prio = 0; prio < PRIORITY_LEVELS; prio++) {
            ram_queue_t *q = &log_queues[type][prio];
            if (q->count > 0) {
                log_record_t *rec = &q->records[q->tail];
                Storage_Write(rec->log_type, rec->data, rec->length);
                q->tail = (q->tail + 1) % MAX_RECORDS;
                q->count--;
            } else if (q->overflow_head != NULL) {
                log_node_t *node = q->overflow_head;
                Storage_Write(node->record.log_type, node->record.data, node->record.length);
                q->overflow_head = node->next;
                if (q->overflow_head == NULL) {
                    q->overflow_tail = NULL;
                }
                free(node);
            }
        }
    }
}

int Storage_Write(uint8_t log_type, uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
        return -1;

    if (log_type >= LOG_TYPE_MAX)
        return -2;

    if (len > MAX_PAYLOAD_SIZE)
        return -3;

    uint8_t  process_buffer[MAX_LOG_SIZE + 32];
    uint16_t process_len = len;

    memcpy(process_buffer, data, len);

    /* ---------------------------------
     Compression (if enabled)
    ----------------------------------*/
    if (compression)
    {
        uint16_t compressed_len = compress_block(process_buffer, len, process_buffer);

        if (compressed_len > 0 && compressed_len < len)
        {
            process_len = compressed_len;
        }
        else
        {
            /* Compression not effective */
            process_len = len;
        }
    }

    /* ---------------------------------
     Encryption (if enabled)
    ----------------------------------*/
    if (encryption)
    {
    	uint8_t nonce[NONCE_SIZE];

        generate_nonce(parser.frame.header.seq,nonce);

        uint8_t encrypted_buffer[MAX_LOG_SIZE + 32];

        AES_CTR_encrypt(process_buffer, process_len, nonce, encrypted_buffer);

        // Prepend nonce to the process_buffer to store it alongside the ciphertext
        memcpy(process_buffer, nonce, NONCE_SIZE);
        memcpy(process_buffer + NONCE_SIZE, encrypted_buffer, process_len);
        
        process_len += NONCE_SIZE;
    }

    /* ---------------------------------
       3️⃣ Write to FTL
    ----------------------------------*/
    int ret = FTL_Append(log_type, process_buffer, process_len);

    return ret;
}
