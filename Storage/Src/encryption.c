/*
 * encryption.c
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#include "encryption.h"



/* ======================================
   Private Global Context
====================================== */

enc_ctx_t g_enc_ctx;
struct AES_ctx aes_ctx;   //Create an AES internal working context used by the AES algorithm. from .aes lib


/* ======================================
   Key Setup
====================================== */

void ENC_SetKey(uint8_t *key)
{
    memcpy(g_enc_ctx.master_key, key, AES_BLOCK_SIZE);
    g_enc_ctx.key_set = 1;

    AES_init_ctx(&aes_ctx, key);
}


/* ======================================
   Device UID Hash (Private)
====================================== */

static uint32_t device_uid_hash(void)
{
    uint32_t *uid = (uint32_t *)DEVICE_UID_BASE;
    return uid[0] ^ uid[1] ^ uid[2];
}


/* ======================================
   Nonce Generator
====================================== */

void generate_nonce(uint64_t sequence, uint8_t nonce[NONCE_SIZE])
{
    uint32_t uid_hash = device_uid_hash();

    for(int i = 0; i < 8; i++)
    {
        nonce[7 - i] = (sequence >> (8 * i)) & 0xFF;
    }

    nonce[8]  = (uid_hash >> 24) & 0xFF;
    nonce[9]  = (uid_hash >> 16) & 0xFF;
    nonce[10] = (uid_hash >> 8)  & 0xFF;
    nonce[11] = uid_hash & 0xFF;
}


/* ======================================
   AES CTR Encrypt / Decrypt
====================================== */

static void AES_CTR_crypt(uint8_t *input, uint32_t length, uint8_t nonce[NONCE_SIZE], uint8_t *output)
{
    uint8_t counter_block[16];
    uint8_t keystream[16];

    uint32_t counter = 0;
    uint32_t offset  = 0;

    while(length > 0)
    {
        memcpy(counter_block, nonce, 12);

        counter_block[12] = (counter >> 24) & 0xFF;
        counter_block[13] = (counter >> 16) & 0xFF;
        counter_block[14] = (counter >> 8)  & 0xFF;
        counter_block[15] = counter & 0xFF;

        memcpy(keystream, counter_block, 16);
        AES_ECB_encrypt(&aes_ctx, keystream);

        uint32_t block_size = (length > AES_BLOCK_SIZE) ? AES_BLOCK_SIZE : length;

        for(uint32_t i = 0; i < block_size; i++)
        {
            output[offset + i] = input[offset + i] ^ keystream[i];
        }

        offset  += block_size;
        length  -= block_size;
        counter++;
    }
}


// wrapper functions for encrypt and decrypt as they are the same functions

void AES_CTR_encrypt(uint8_t *input, uint32_t length,uint8_t nonce[NONCE_SIZE],uint8_t *output)
{
    AES_CTR_crypt(input, length, nonce, output);
}

void AES_CTR_decrypt(uint8_t *input,uint32_t length,uint8_t nonce[NONCE_SIZE], uint8_t *output)
{
    AES_CTR_crypt(input, length, nonce, output);
}
