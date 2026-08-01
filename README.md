# Storage Coprocessor (BlackBox)

## Overview
Firmware for an STM32-based (STM32F411) Storage Coprocessor designed to act as a secure, reliable "BlackBox" data logger. It receives log records and commands from a host controller via SPI, applies optional data compression and encryption, and stores them reliably in SPI Flash using a custom Flash Translation Layer (FTL).

## Key Features
- **High-Speed Communication:** SPI interface utilizing DMA for efficient data reception without blocking the CPU.
- **Robust Frame Protocol:** Custom packet parser with a state machine, supporting Start-of-Frame (SOF) synchronization, and CRC16-CCITT validation for both Header and Payload.
- **Priority-Based Queuing:** In-RAM queuing of incoming log messages categorized by log type and priority levels.
- **Custom Flash Translation Layer (FTL):**
  - Sector-level erase and write management.
  - Configurable storage partitions (dynamically adjustable via master command).
  - Configurable storage policies (e.g., Circular overwrite or Stop-on-full).
  - Superblock architecture to retain configuration and write-pointers across power cycles.
- **Security & Efficiency:**
  - **Encryption:** AES-CTR Mode payload encryption utilizing dynamically generated nonces.
  - **Compression:** Built-in payload compression to maximize the utilization of external SPI flash memory.

## Directory Structure
- `Core/`: STM32 HAL library setup, interrupt service routines, System Clock configuration, SPI and DMA initialization (`main.c`).
- `Storage/`: Contains the core logic for the coprocessor's functionality.
  - `Src/parse.c`: Frame parsing state machine and priority queue management.
  - `Src/ftl.c`: Flash Translation Layer implementation (partitions, wear, writing, and reading).
  - `Src/storage_main.c`: Orchestrates data flow, calling encryption, compression, and then writing to FTL.
  - `Src/encryption.c` & `Src/compression.c`: Data security and size reduction wrappers.
  - `Src/spi_flash.c`: Low-level SPI Flash memory driver.
  - `Inc/`: Header files for the Storage subsystem.
  - `crypto/`: Low-level cryptographic primitives.
- `.ioc`: STM32CubeMX configuration file.

## Protocol Summary
Data is sent from the host master to the coprocessor using the following packet format:
1. **SOF** (Start of Frame: `0xA5`)
2. **Header** (Command ID, Payload Length, Sequence ID, Header CRC16)
3. **Payload** (Depends on Command ID)
4. **Payload CRC** (CRC16-CCITT of the payload)

### Supported Commands
- **`0x01` (Load Configuration):** Sets up the FTL partitions, max log sizes, storage modes, and enables/disables encryption and compression.
- **`0x02` (Write Request):** Appends a log entry to a specific partition based on the log type.
- **`0x03` (Read Request):** Requests stored logs back from the coprocessor based on type, priority filters, and limits.

## Hardware Target
- **Microcontroller:** STM32F411CEUx
- **External Storage:** SPI Flash Memory
- **Host Interface:** SPI1 (Slave Mode, DMA Enabled)

## Building the Project
This project is set up for **STM32CubeIDE**.
1. Open STM32CubeIDE.
2. Select `File -> Import... -> General -> Existing Projects into Workspace`.
3. Browse to this directory and select the `Storage_Coprocessor` project.
4. Click **Build** (hammer icon) to compile the firmware.
5. Use an ST-LINK or compatible debugger to flash the target device.
