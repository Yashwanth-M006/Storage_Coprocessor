#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>

extern uint32_t bench_dropped_packets;
extern uint32_t bench_successful_packets;
extern uint32_t bench_start_time;
extern uint32_t bench_end_time;

void Benchmark_Run(void);

#endif
