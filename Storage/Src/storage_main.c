/*
 * storage_main.c
 *
 *  Created on: 27-Feb-2026
 *      Author: Yashwanth
 */

#include "storage_main.h"

encryption_t encryption;
compression_t compression;

void Storage_System_Init(config_payload_t *cfg)
{
    if (cfg == NULL)
        return;

    uint8_t enc_comp_mode = cfg->enc_comp_mode;
    encryption = (enc_comp_mode & 0x01) ? ENCRYPTION_ENABLE : ENCRYPTION_DISABLE;

    compression = (enc_comp_mode & 0x02) ? COMPRESSION_ENABLE : COMPRESSION_DISABLE;

    int  Flash_Init(void);


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
    if (encryption)
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

        memcpy(process_buffer, encrypted_buffer, process_len);

        /* NOTE:
           You must store nonce along with record header
           in FTL layer
        */
    }

    /* ---------------------------------
       3️⃣ Write to FTL
    ----------------------------------*/
    int ret = FTL_Append(log_type, process_buffer, process_len);

    return ret;
}
