/*
 * compression.c
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */


#include "compression.h"

/****************************************** Compression *****************************************************/
// Delta Encoding
uint16_t delta_encode_16(uint16_t *input, int16_t  *delta_out, uint16_t count, uint16_t *base)
{
    if(count == 0) return 0;

    *base = input[0];

    for(uint16_t i = 1; i < count; i++)
    {
        delta_out[i-1] = input[i] - input[i-1];
    }

    return count - 1;  // number of deltas
}

// RLE Encoding
uint16_t rle_encode( int16_t *delta, uint8_t *output, uint16_t count)
{
    uint16_t i = 0;
    uint16_t out_index = 0;

    while(i < count)
    {
        int16_t value = delta[i];
        uint8_t run = 1;

        while((i + run < count) &&
              (delta[i + run] == value) &&
              (run < 255))
        {
            run++;
        }

        output[out_index++] = run;
        *(int16_t*)&output[out_index] = value;
        out_index += 2;

        i += run;
    }

    return out_index;
}


// ======================================================
//                MAIN COMPRESSION API
// ======================================================
uint16_t compress_block(uint8_t *input, uint16_t input_size, uint8_t *output)
{
    store_block_header_t *header = (store_block_header_t *)output;

    uint16_t element_count = input_size / 2;

    uint16_t *input16 = (uint16_t *)input;

    // Temporary buffers (STATIC → no stack explosion)
    static int16_t delta_buffer[256];
    static uint8_t rle_buffer[768];

    uint16_t base_value = 0;

    // ---------- DELTA ----------
    uint16_t delta_count = delta_encode_16(input16, delta_buffer, element_count, &base_value);

    // ---------- RLE ----------
    uint16_t rle_size = rle_encode(delta_buffer, rle_buffer, delta_count);

    // ---------- ADAPTIVE DECISION ----------
    if(rle_size >= input_size)
    {
        // Store raw
        header->original_size    = input_size;
        header->compressed_size  = input_size;
        header->compression_type = COMPRESSION_NONE;
        header->element_size     = 2;
        header->base_value       = 0;

        memcpy(output + sizeof(store_block_header_t), input, input_size);

        return sizeof(store_block_header_t) + input_size;
    }
    else
    {
        // Store compressed
        header->original_size    = input_size;
        header->compressed_size  = rle_size;
        header->compression_type = COMPRESSION_DELTA_RLE;
        header->element_size     = 2;
        header->base_value       = base_value;

        memcpy(output + sizeof(store_block_header_t), rle_buffer, rle_size);

        return sizeof(store_block_header_t) + rle_size;
    }
}



/**************************************  Decompression *********************************************************/
uint16_t rle_decode( uint8_t *input, int16_t *delta_out, uint16_t compressed_size)
{
    uint16_t i = 0;
    uint16_t out_index = 0;

    while(i < compressed_size)
    {
        uint8_t run = input[i++];
        int16_t value = *(int16_t*)&input[i];
        i += 2;

        for(uint8_t j = 0; j < run; j++)
        {
            delta_out[out_index++] = value;
        }
    }

    return out_index;
}

void delta_decode_16(uint16_t base, int16_t *delta, uint16_t *output, uint16_t delta_count)
{
    output[0] = base;

    for(uint16_t i = 1; i <= delta_count; i++)
    {
        output[i] = output[i-1] + delta[i-1];
    }
}


// ======================================================
//             MAIN DECOMPRESSION API
// ======================================================
uint16_t decompress_block(uint8_t *input, uint8_t *output)
{
    store_block_header_t *header = (store_block_header_t *)input;

    uint8_t *payload = input + sizeof(store_block_header_t);

    // If no compression
    if(header->compression_type == COMPRESSION_NONE)
    {
        memcpy(output, payload, header->original_size);

        return header->original_size;
    }

    // If delta + RLE
    else if(header->compression_type == COMPRESSION_DELTA_RLE)
    {
        static int16_t delta_buffer[256];
        static uint16_t reconstructed[256];

        uint16_t delta_count = rle_decode(payload, delta_buffer, header->compressed_size);

        delta_decode_16( header->base_value, delta_buffer, reconstructed, delta_count);

        memcpy(output, reconstructed, header->original_size);

        return header->original_size;
    }

    return 0; // safety
}
