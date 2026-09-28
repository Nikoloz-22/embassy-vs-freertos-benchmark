#ifndef BLACKPILL_BENCH_H
#define BLACKPILL_BENCH_H
#include <stdint.h>
#define BENCH_N 1000u
typedef struct {
    uint32_t empty, crc, roundtrip;
} BenchSample;
extern BenchSample bench_samples[BENCH_N];
extern volatile uint32_t bench_done;
extern volatile uint32_t bench_crc_result;
extern volatile uint32_t bench_rcc_cfgr, bench_flash_acr;
extern volatile uint32_t bench_controller_free_words, bench_worker_free_words;
void Bench_CreateTasks(void);
// Optional override: emit after collection, never while samples are taken.
void Bench_Emit(uint32_t index, const BenchSample *sample);
#endif

