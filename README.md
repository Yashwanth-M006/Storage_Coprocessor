# Storage Coprocessor (BlackBox)

## Overview
Firmware for an STM32-based (STM32F411) Storage Coprocessor designed to act as a secure, reliable "BlackBox" data logger. It receives log records and commands from a host controller via SPI, applies optional data compression and encryption, and stores them reliably in SPI Flash using a custom Flash Translation Layer (FTL).

## Key Features & Configurations

### 1. Robust Frame Protocol & SPI DMA
Data is received continuously via SPI using DMA (Circular mode into a 1KB RAM buffer). The system implements a resilient state-machine packet parser:
- **SOF Synchronization:** Start of Frame (`0xA5`).
- **Hardware-Accelerated CRC32:** Both the header and payload are validated independently using the STM32's built-in Hardware CRC peripheral. If either checksum fails, the frame is rejected to prevent processing corrupted length/data packets.

### 2. Log Categorization & Priority Queuing
Incoming data is categorized dynamically. The coprocessor manages 4 dedicated log types, each mapped to its own FTL partition and RAM queue:
- **`LOG_TYPE_BURST` (0):** High-frequency data (e.g., IMU/Sensor dumps).
- **`LOG_TYPE_INTERRUPT` (1):** Critical hardware interrupts or faults.
- **`LOG_TYPE_EVENT` (2):** System state changes or standard events.
- **`LOG_TYPE_TIME` (3):** Periodic heartbeat or RTC syncs.

Each log type supports **8 Priority Levels (0–7)**.
- **Dynamic Queue Allocation (Overflow Handling):** If a priority queue's static capacity (`MAX_RECORDS`) is exceeded, the coprocessor automatically allocates a temporary queue node from a **Static Memory Pool** (capacity: 64 nodes). These are safely processed and deallocated in a FIFO manner once the flash writer catches up, preventing data loss.

### System Limitations
- **Overflow Pool Exhaustion:** The coprocessor avoids using `malloc()` to prevent heap fragmentation and hard faults. It is hardcoded to a static overflow pool of `MAX_OVERFLOW_NODES` (64 nodes, utilizing ~17 KB of SRAM). If all 64 overflow nodes are currently in use because the Flash FTL is too slow to catch up with burst SPI traffic, **the system will drop any incoming SPI packets** and return a failure (Option A). Ensure your SPI host respects this backpressure constraint.

### 3. Custom Flash Translation Layer (FTL)
The FTL acts as a lightweight file system specifically optimized for SPI Flash logging.
- **Dynamic Partitioning:** The physical flash memory is divided into 4 partitions (one for each Log Type). The percentage size of each partition is dynamically configurable via the host master (3 bits per partition, representing multiples of 10%).
- **Storage Policies (Modes):**
  - **`FTL_MODE_STOP`:** Halts logging for a specific partition when it becomes completely full.
  - **`FTL_MODE_CIRCULAR`:** Operates as a ring buffer. When the partition fills up, it actively erases the oldest 4KB sector and overwrites it with new data.
- **Partition Boundary Wrapping:** To prevent a log record (header + payload) from being split across the physical boundaries of a partition, the FTL calculates remaining space before writing. If the record exceeds the space, the FTL pads the remainder of the partition with `0xFF`, resets the write pointer (`write_ptr`) back to the partition's `start_addr`, and writes the record sequentially.
- **Fail-Safe Dual-Bank Superblock (A/B Toggle):** To prevent corruption of system metadata in the event of a power failure while updating pointers, the FTL uses a dual-bank configuration strategy. 
  - Two dedicated sectors (**Sector 0** and **Sector 1**) are allocated for storing the superblock.
  - Each superblock contains an incrementing `version` number and a `crc` checksum validated via the Hardware CRC peripheral.
  - When saving configuration updates or pointer progress, the system writes to the inactive sector and verifies the checksum. Only after verification does it logically swap the active sector pointer.
  - Upon boot, `FTL_Init()` reads both sectors, checks the CRC of both, and runs from the bank with the highest valid version number.

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
2. **Header** (Command ID, Payload Length, Sequence ID, Header CRC32)
3. **Payload** (Depends on Command ID)
4. **Payload CRC** (Hardware-calculated CRC32)

### Supported Commands
### Supported Commands
- **`0x01` (Load Configuration):** 
  - Loads a `config_payload_t` struct to configure partition sizes, FTL storage policies, and hardware options.
  
  #### Configuration Payload Specification (`config_payload_t`):
  
  | Offset | Field Name | Type | Description |
  |--------|------------|------|-------------|
  | 0x00 | `enc_comp_mode` | `uint8_t` | **Encryption & Compression Mode Control:**<br>• **Bit 0:** Encryption Enable (1 = AES-CTR enabled, 0 = disabled)<br>• **Bit 1:** Compression Enable (1 = Delta+RLE compression enabled, 0 = disabled)<br>• **Bits 2-7:** Reserved (must be 0) |
  | 0x01 | `storage_mode` | `uint8_t` | **Storage FTL Policies (Circular vs. Stop):**<br>• **Bit 0:** `BURST` partition Circular flag (1 = Circular, 0 = Stop)<br>• **Bit 1:** `INTERRUPT` partition Circular flag (1 = Circular, 0 = Stop)<br>• **Bit 2:** `EVENT` partition Circular flag (1 = Circular, 0 = Stop)<br>• **Bit 3:** `TIME` partition Circular flag (1 = Circular, 0 = Stop)<br>• **Bit 4:** Global Circular Override (1 = Force all partitions to Circular, overrides bits 0-3)<br>• **Bits 5-7:** Reserved (must be 0) |
  | 0x02 | `erase_policy` | `uint8_t` | **Erase Policy Control:**<br>• 0 = Lazy Erase (erase sector dynamically on write)<br>• 1 = Pre-Erase (prepare sectors in advance) |
  | 0x03 | `reserved1` | `uint8_t` | Reserved padding (must be 0) |
  | 0x04 | `partition_map` | `uint16_t` | **Partition Size Allocations:**<br>This field maps 3-bit values for each partition, where each unit equals 10% of the available flash log memory. **The sum of these units must equal exactly 10 (100%).**<br>• **Bits 0–2:** `BURST` partition percentage unit (0-7)<br>• **Bits 3–5:** `INTERRUPT` partition percentage unit (0-7)<br>• **Bits 6–8:** `EVENT` partition percentage unit (0-7)<br>• **Bits 9–11:** `TIME` partition percentage unit (0-7)<br>• **Bits 12–15:** Reserved (must be 0) |
  | 0x06 | `max_log_size` | `uint16_t` | **Maximum Log Size Constraint:**<br>Specifies the maximum allowable byte size for a single log record. Must be greater than 0 and less than or equal to `MAX_LOG_SIZE` (256 bytes). |

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
