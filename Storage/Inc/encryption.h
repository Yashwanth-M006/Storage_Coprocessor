/*
 * encryption.h
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#ifndef INC_ENCRYPTION_H_
#define INC_ENCRYPTION_H_

#include "aes.h"          // tiny-AES

#include <stdint.h>
#include <string.h>

#define AES_BLOCK_SIZE     16
#define DEVICE_UID_BASE    0x1FFF7A10U   // STM32F4 UID address
#define NONCE_SIZE         12

#define KEY_FLASH_ADDR  0x0807F000U
#define KEY_MAGIC       0xDEADBEEF


/* ================================
   Encryption Context
================================= */

typedef struct
{
    uint8_t master_key[AES_BLOCK_SIZE];
    uint8_t key_set;

} enc_ctx_t;

extern enc_ctx_t g_enc_ctx;
extern struct AES_ctx aes_ctx;

/* ================================
   Flash Block Header
================================= */
/*
typedef struct
{
    uint32_t magic;
    uint64_t sequence;        // Make it 64-bit (important!)
    uint32_t original_len;
    uint32_t compressed_len;
    uint8_t  nonce[NONCE_SIZE];

} flash_block_header_t;
*/

/* ================================
   Public API
================================= */

void ENC_SetKey(uint8_t *key);

void generate_nonce(uint64_t sequence, uint8_t nonce[NONCE_SIZE]);


void AES_CTR_encrypt(uint8_t *input, uint32_t length,uint8_t nonce[NONCE_SIZE],uint8_t *output);

void AES_CTR_decrypt(uint8_t *input,uint32_t length,uint8_t nonce[NONCE_SIZE], uint8_t *output);


#endif /* INC_ENCRYPTION_H_ */
