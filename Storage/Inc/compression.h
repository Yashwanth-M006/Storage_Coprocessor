/*
 * compression.h
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#ifndef INC_COMPRESSION_H_
#define INC_COMPRESSION_H_


#include "Config.h"

#define COMPRESSION_NONE        0
#define COMPRESSION_DELTA_RLE   1



// Compression
uint16_t delta_encode_16(uint16_t *input, int16_t  *delta_out, uint16_t count, uint16_t *base);

uint16_t rle_encode( int16_t *delta, uint8_t *output, uint16_t count);

uint16_t compress_block(uint8_t *input, uint16_t input_size, uint8_t *output);


// Decompression
uint16_t rle_decode( uint8_t *input, int16_t *delta_out, uint16_t compressed_size);

void delta_decode_16(uint16_t base, int16_t *delta, uint16_t *output, uint16_t delta_count);

uint16_t decompress_block(uint8_t *input, uint8_t *output);


#endif /* INC_COMPRESSION_H_ */


