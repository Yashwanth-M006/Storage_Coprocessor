#include "benchmark.h"
#include "storage_main.h"
#include "parse.h"
#include "ftl.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

// Global metrics
uint32_t bench_dropped_packets = 0;
uint32_t bench_successful_packets = 0;
uint32_t bench_start_time = 0;
uint32_t bench_end_time = 0;

void Benchmark_Run(void)
{
    // 1. Send Configuration Packet to initialize FTL
    master_frame_t config_frame;
    config_frame.header.cmd = 0x01;
    config_frame.header.payload_len = sizeof(config_payload_t);
    
    config_payload_t cfg;
    cfg.partition_map = (5 << 0) | (2 << 3) | (2 << 6) | (1 << 9); // 50%, 20%, 20%, 10%
    cfg.erase_policy = 0; // Lazy Erase for speed
    cfg.storage_mode = 0; // Stop Mode
    
    memcpy(config_frame.payload, &cfg, sizeof(config_payload_t));
    Queue_Load_Config(&config_frame);
    
    // 2. Generate Burst Traffic Stress Test
    // We will attempt to push 500 high-priority Burst logs as fast as possible.
    // The SPI flash writes slower than CPU execution, so the Static Memory Pool (64 nodes)
    // will fill up. Once 64 nodes + static ring buffer (32 nodes) = 96 packets are queued,
    // the system will start dropping packets unless the background loop clears them.
    
    bench_start_time = HAL_GetTick();
    
    master_frame_t log_frame;
    log_frame.header.cmd = 0x02;
    log_frame.header.payload_len = 64; // 64 byte payload
    
    write_log_header_t log_hdr;
    log_hdr.log_type = 0; // Burst
    log_hdr.priority = 0; // Highest priority
    memcpy(log_frame.payload, &log_hdr, sizeof(write_log_header_t));
    
    // Fill dummy data
    for (int i = 0; i < 64 - sizeof(write_log_header_t); i++) {
        log_frame.payload[sizeof(write_log_header_t) + i] = (uint8_t)i;
    }

    for (int i = 0; i < 500; i++)
    {
        uint8_t res = Queue_Push(&log_frame);
        if (res == 1) {
            bench_successful_packets++;
        } else {
            bench_dropped_packets++;
        }
        
        // Let the background processor run a little bit to simulate parallel processing
        // In real hardware, Storage_Process_Command runs in the main loop while DMA interrupts push data.
        Storage_Process_Command();
    }
    
    // 3. Wait for queues to fully drain to Flash
    while (1) {
        Storage_Process_Command();
        // Check if all queues are empty
        uint8_t all_empty = 1;
        for (int type = 0; type < 4; type++) {
            for (int prio = 0; prio < 8; prio++) {
                if (log_queues[type][prio].count > 0 || log_queues[type][prio].overflow_head != NULL) {
                    all_empty = 0;
                }
            }
        }
        if (all_empty) break;
    }
    
    bench_end_time = HAL_GetTick();
    
    // 4. Metrics ready for inspection via Renode GDB or debugger!
    // Total Time = bench_end_time - bench_start_time
    // Throughput = (bench_successful_packets * 64) / (Total Time)
}
