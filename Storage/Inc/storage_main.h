/*
 * storage_main.h
 *
 *  Created on: 27-Feb-2026
 *      Author: Yashwanth
 */

#ifndef INC_STORAGE_MAIN_H_
#define INC_STORAGE_MAIN_H_

#include "Config.h"


/* =========================================
   APIs
========================================= */

void Storage_System_Init(config_payload_t *cfg);

int  Storage_Write(uint8_t log_type, uint8_t *data, uint16_t len);

int  Storage_Read(uint8_t log_type, uint8_t *out_buf, uint16_t *out_len);

void getStatus(void);

void Storage_Process_Command(void);

#endif /* INC_STORAGE_MAIN_H_ */
