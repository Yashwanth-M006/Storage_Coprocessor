# Storage Coprocessor (BlackBox)

## Overview
Firmware for an STM32-based (STM32F411) Storage Coprocessor designed to act as a secure, reliable "BlackBox" data logger. It receives log records and commands from a host controller via SPI, applies optional data compression and encryption, and stores them reliably in SPI Flash using a custom Flash Translation Layer (FTL).

## Key Features & Configurations

### 1. Robust Frame Protocol & SPI DMA
Data is received continuously via SPI using DMA (Circular mode into a 1KB RAM buffer). The system implements a resilient state-machine packet parser:
- **SOF Synchronization:** Start of Frame (`0xA5`).
- **Dual CRC16-CCITT:** Header and payload are independently verified to ensure no corrupted lengths or data are processed.

### 2. Log Categorization & Priority Queuing
Incoming data is categorized dynamically. The coprocessor manages 4 dedicated log types, each mapped to its own FTL partition and RAM queue:
- **`LOG_TYPE_BURST` (0):** High-frequency data (e.g., IMU/Sensor dumps).
- **`LOG_TYPE_INTERRUPT` (1):** Critical hardware interrupts or faults.
- **`LOG_TYPE_EVENT` (2):** System state changes or standard events.
- **`LOG_TYPE_TIME` (3):** Periodic heartbeat or RTC syncs.

Each log type supports **8 Priority Levels (0–7)**, allowing the queuing system to intelligently manage RAM buffering when incoming data exceeds the SPI Flash write speed.

### 3. Custom Flash Translation Layer (FTL)
The FTL acts as a lightweight file system specifically optimized for SPI Flash logging.
- **Dynamic Partitioning:** The physical flash memory is divided into 4 partitions (one for each Log Type). The percentage size of each partition is dynamically configurable via the host master (3 bits per partition, representing multiples of 10%).
- **Storage Policies (Modes):**
  - **`FTL_MODE_STOP`:** Halts logging for a specific partition when it becomes completely full.
  - **`FTL_MODE_CIRCULAR`:** Operates as a ring buffer. When the partition fills up, it actively erases the oldest 4KB sector and overwrites it with new data.
- **Superblock Architecture:** Sector 0 of the flash is dedicated to the Superblock. It persistently stores the partition configuration, storage modes, and current Read/Write/Oldest pointers.

### 4. Data Security & Efficiency
- **Encryption (AES-CTR):** Payloads can be optionally encrypted using AES in Counter (CTR) mode. CTR mode is chosen to avoid data padding (saving space) and utilizes a dynamically generated Nonce.
  - **Master Key Storage:** The 128-bit (16-byte) AES key is loaded into RAM in the global context variable `g_enc_ctx.master_key` when `ENC_SetKey()` is invoked. The system designates a dedicated Flash location at `KEY_FLASH_ADDR` (`0x0807F000U` - Sector 7 of the STM32F411 internal flash) for persistent key storage.
  - **Nonce Generation:** The 12-byte cryptographic nonce is constructed dynamically to guarantee uniqueness:
    - **Bytes 0–7:** Derived from the 64-bit transaction sequence number (`parser.frame.header.seq`) to ensure sequence uniqueness.
    - **Bytes 8–11:** Computed as a 32-bit XOR hash of the STM32F4 microcontroller's unique 96-bit hardware UID registers (located at `0x1FFF7A10U`) to ensure device uniqueness.
- **Compression:** An optional compression layer (Delta + Run-Length Encoding) can be enabled to squeeze maximum data into the SPI Flash.
- *Both are toggled via the Configuration Command (`enc_comp_mode`).*

## Directory Structure
- `Core/`: STM32 HAL library setup, System Clock configuration, SPI and DMA initialization (`main.c`).
- `Storage/`: Contains the core logic for the coprocessor's functionality.
  - `Src/parse.c`: Frame parsing state machine and priority queue management.
  - `Src/ftl.c`: Flash Translation Layer implementation (partitions, pointers, writing, and reading).
  - `Src/storage_main.c`: High-level data flow, orchestrating encryption, compression, and FTL.
  - `Src/encryption.c` & `Src/compression.c`: Data security and size reduction wrappers.
  - `Src/spi_flash.c`: Low-level SPI Flash memory driver (Page Program, Sector Erase).
  - `Inc/`: Header files for the Storage subsystem (including `Config.h`).
  - `crypto/`: Low-level cryptographic primitives (e.g., AES).
- `.ioc`: STM32CubeMX configuration file.

## SPI Protocol Summary
Data is sent from the host master to the coprocessor using the following packet format:
1. **SOF** (`0xA5`)
2. **Header** (Command ID, Payload Length, Sequence ID, Header CRC16)
3. **Payload** (Depends on Command ID)
4. **Payload CRC** (CRC16-CCITT)

### Supported Commands
- **`0x01` (Load Configuration):** 
  - Sets up the `partition_map` (sizes).
  - Sets `storage_mode` (Circular vs Stop policies).
  - Sets `enc_comp_mode` (Encryption/Compression toggles).
  - Sets `max_log_size` constraints.
- **`0x02` (Write Request):** 
  - Submits a `write_log_header_t` containing Log Type, Priority, Subtype, Timestamp, and the actual log data.
- **`0x03` (Read Request):** 
  - Retrieves logs from a specific `log_type` partition.
  - Supports filtering by `priority_filter`.
  - Supports multiple read modes: Index, Time, Latest N, or From Pointer.

## Hardware Target
- **Microcontroller:** STM32F411CEUx
- **External Storage:** SPI Flash Memory (e.g., W25Q series)
- **Host Interface:** SPI1 (Slave Mode, DMA Enabled)

## Building the Project
This project is built using **STM32CubeIDE**.
1. Open STM32CubeIDE.
2. Select `File -> Import... -> General -> Existing Projects into Workspace`.
3. Browse to this directory and select the `Storage_Coprocessor` project.
4. Click **Build** (hammer icon) to compile the firmware.
5. Use an ST-LINK or compatible debugger to flash the target device.
