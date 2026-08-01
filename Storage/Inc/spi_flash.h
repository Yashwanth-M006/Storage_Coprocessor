/*
 * spi_flash.h
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include "Config.h"

/* ==============================
   Flash Configuration
============================== */

#define FLASH_PAGE_SIZE        256
#define FLASH_SECTOR_SIZE      4096
#define FLASH_TOTAL_SIZE       (32UL * 1024 * 1024)

/* ==============================
   Public API
============================== */

int  Flash_Init(void);

int  Flash_Read(uint32_t addr, uint8_t *data, uint32_t len);
int  Flash_Write(uint32_t addr, uint8_t *data, uint32_t len);
int  Flash_Erase_Sector(uint32_t addr);
int  Flash_Chip_Erase(void);



#endif /* INC_SPI_FLASH_H_ */
