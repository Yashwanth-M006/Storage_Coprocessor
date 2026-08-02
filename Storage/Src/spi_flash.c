/*
 * spi_flash.c
 *
 *  Created on: 25-Feb-2026
 *      Author: Yashwanth
 */

#include "spi_flash.h"
//#include "stm32f4xx_hal.h"
//#include <string.h>

/* ==============================
   External SPI Handle
============================== */

extern SPI_HandleTypeDef hspi2;

/* ==============================
   Flash CS Control
   (Change GPIO as per CubeMX)
============================== */

#define FLASH_CS_PORT     GPIOA
#define FLASH_CS_PIN      GPIO_PIN_4

#define FLASH_CS_LOW()    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET)
#define FLASH_CS_HIGH()   HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET)

/* ==============================
   Flash Commands (W25Q256 Compatible)
============================== */

#define CMD_WRITE_ENABLE      0x06
#define CMD_READ_DATA         0x03
#define CMD_PAGE_PROGRAM      0x02		//Used to write data (max 256 bytes per page).
#define CMD_SECTOR_ERASE      0x20
#define CMD_CHIP_ERASE        0xC7
#define CMD_READ_STATUS       0x05
#define CMD_JEDEC_ID          0x9F		//Used to verify correct chip. for winbond EF 40 19

/* ==============================
   DMA Flags
============================== */

static volatile uint8_t spi_tx_done = 0;
static volatile uint8_t spi_rx_done = 0;

/* ==============================
   DMA Callbacks
============================== */

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI2)
        spi_tx_done = 1;
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI2)
        spi_rx_done = 1;
}

/* ==============================
   Internal Helpers
============================== */

static void SPI_Wait_TX(void)
{
    while (!spi_tx_done);
    spi_tx_done = 0;
}

static void SPI_Wait_RX(void)
{
    while (!spi_rx_done);
    spi_rx_done = 0;
}

static void Flash_Write_Enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;

    FLASH_CS_LOW();
    HAL_SPI_Transmit_DMA(&hspi2, &cmd, 1);
    SPI_Wait_TX();
    FLASH_CS_HIGH();
}

static uint8_t Flash_Read_Status(void)
{
    uint8_t cmd = CMD_READ_STATUS;
    uint8_t status = 0;

    FLASH_CS_LOW();
    HAL_SPI_Transmit_DMA(&hspi2, &cmd, 1);
    SPI_Wait_TX();

    HAL_SPI_Receive_DMA(&hspi2, &status, 1);
    SPI_Wait_RX();
    FLASH_CS_HIGH();

    return status;
}

static void Flash_Wait_Busy(void)
{
    while (Flash_Read_Status() & 0x01);
}

/* ==============================
   Public Functions
============================== */

int Flash_Init(void)
{
    uint8_t cmd = CMD_JEDEC_ID;
    uint8_t id[3];

    FLASH_CS_LOW();
    HAL_SPI_Transmit_DMA(&hspi2, &cmd, 1);
    SPI_Wait_TX();

    HAL_SPI_Receive_DMA(&hspi2, id, 3);
    SPI_Wait_RX();
    FLASH_CS_HIGH();

    return 0;
}

int Flash_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];

    cmd[0] = CMD_READ_DATA;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8)  & 0xFF;
    cmd[3] = addr & 0xFF;

    FLASH_CS_LOW();

    HAL_SPI_Transmit_DMA(&hspi2, cmd, 4);
    SPI_Wait_TX();

    HAL_SPI_Receive_DMA(&hspi2, data, len);
    SPI_Wait_RX();

    FLASH_CS_HIGH();

    return 0;
}

int Flash_Write(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint32_t remaining = len;
    uint32_t current_addr = addr;
    uint8_t *current_data = data;

    while (remaining > 0)
    {
        uint32_t page_offset = current_addr % FLASH_PAGE_SIZE;
        uint32_t chunk = FLASH_PAGE_SIZE - page_offset;

        if (chunk > remaining)
            chunk = remaining;

        Flash_Write_Enable();

        uint8_t cmd[4];
        cmd[0] = CMD_PAGE_PROGRAM;
        cmd[1] = (current_addr >> 16) & 0xFF;
        cmd[2] = (current_addr >> 8)  & 0xFF;
        cmd[3] = current_addr & 0xFF;

        FLASH_CS_LOW();

        HAL_SPI_Transmit_DMA(&hspi2, cmd, 4);
        SPI_Wait_TX();

        HAL_SPI_Transmit_DMA(&hspi2, current_data, chunk);
        SPI_Wait_TX();

        FLASH_CS_HIGH();

        Flash_Wait_Busy();

        current_addr += chunk;
        current_data += chunk;
        remaining -= chunk;
    }

    return 0;
}

int Flash_Erase_Sector(uint32_t addr)
{
    Flash_Write_Enable();

    uint8_t cmd[4];
    cmd[0] = CMD_SECTOR_ERASE;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8)  & 0xFF;
    cmd[3] = addr & 0xFF;

    FLASH_CS_LOW();

    HAL_SPI_Transmit_DMA(&hspi2, cmd, 4);
    SPI_Wait_TX();

    FLASH_CS_HIGH();

    Flash_Wait_Busy();

    return 0;
}

int Flash_Chip_Erase(void)
{
    Flash_Write_Enable();

    uint8_t cmd = CMD_CHIP_ERASE;

    FLASH_CS_LOW();
    HAL_SPI_Transmit_DMA(&hspi2, &cmd, 1);
    SPI_Wait_TX();
    FLASH_CS_HIGH();

    Flash_Wait_Busy();

    return 0;
}
