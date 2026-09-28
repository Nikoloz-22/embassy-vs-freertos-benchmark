#include "bench.h"
#include "crc32.h"
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"


#if configUSE_PREEMPTION != 1
#error Enable preemptive scheduling
#endif
#if configSUPPORT_STATIC_ALLOCATION != 1
#error Enable static allocation
#endif
#if configUSE_TIME_SLICING != 0
#error Disable equal-priority time slicing for this controlled experiment
#endif
#if INCLUDE_uxTaskGetStackHighWaterMark != 1
#error Enable INCLUDE_uxTaskGetStackHighWaterMark
#endif

BenchSample bench_samples[BENCH_N];
volatile uint32_t bench_done, bench_crc_result;
volatile uint32_t bench_rcc_cfgr, bench_flash_acr;
volatile uint32_t bench_controller_free_words, bench_worker_free_words;
static uint8_t data[256];
static TaskHandle_t controller_handle, worker_handle;
static StaticTask_t controller_tcb, worker_tcb;
static StackType_t controller_stack[512], worker_stack[256];

static inline uint32_t stamp(void) {
    __asm volatile ("" ::: "memory");
    __DSB();
    __ISB();
    uint32_t t = DWT->CYCCNT;
    __asm volatile ("" ::: "memory");
    return t;
}

static void enable_dwt(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    uint32_t a = stamp();
    for (volatile uint32_t i = 0; i < 100; ++i) { __NOP(); }
    configASSERT(stamp() - a > 0);
}

__attribute__((weak))
void Bench_Emit(uint32_t index, const BenchSample *sample) {
    (void)index;
    (void)sample;
    // The full dataset remains available as bench_samples in the debugger.
}

static void worker(void *unused) {
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTaskNotifyGive(controller_handle);
    }
}

static void controller(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < sizeof data; ++i) {
        data[i] = (uint8_t)(i * 17u + 3u);
    }
    // Never put required actions inside configASSERT: it can be disabled.
    uint32_t check = bench_crc32((const uint8_t *)"123456789", 9);
    configASSERT(check == UINT32_C(0xCBF43926));
    uint32_t expected = bench_crc32(data, sizeof data);
    configASSERT(expected == UINT32_C(0x8C70078C));
    for (unsigned i = 0; i < 100; ++i) {
        uint32_t value = bench_crc32(data, sizeof data);
        __asm volatile ("" : "+r" (value) :: "memory");
        xTaskNotifyGive(worker_handle);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    for (unsigned i = 0; i < BENCH_N; ++i) {
        uint32_t start = stamp();
        bench_samples[i].empty = stamp() - start;

        start = stamp();
        uint32_t value = bench_crc32(data, sizeof data);
        __asm volatile ("" : "+r" (value) :: "memory");
        bench_samples[i].crc = stamp() - start;
        configASSERT(value == expected);

        start = stamp();
        xTaskNotifyGive(worker_handle);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bench_samples[i].roundtrip = stamp() - start;
    }
    bench_crc_result = expected;
    bench_controller_free_words = uxTaskGetStackHighWaterMark(NULL);
    bench_worker_free_words = uxTaskGetStackHighWaterMark(worker_handle);
    __DMB();
    bench_done = 1; // Collection complete: debugger may halt now.
    for (unsigned i = 0; i < BENCH_N; ++i) Bench_Emit(i, &bench_samples[i]);
    // Include optional logger stack consumption in this second observation.
    bench_controller_free_words = uxTaskGetStackHighWaterMark(NULL);
    vTaskSuspend(NULL);
    for (;;) {}
}

void Bench_CreateTasks(void) {
    // Call once after clock setup and BEFORE vTaskStartScheduler/osKernelStart.
    // Use HSI SYSCLK 16 MHz; AHB/APB1/APB2 /1.
    bench_rcc_cfgr = RCC->CFGR;
    bench_flash_acr = FLASH->ACR;
    configASSERT((bench_rcc_cfgr & UINT32_C(0xFCFC)) == 0);
    SystemCoreClockUpdate();
    configASSERT(SystemCoreClock == 16000000u);
    enable_dwt();
    worker_handle = xTaskCreateStatic(worker, "worker", 256, NULL, 2,
                                     worker_stack, &worker_tcb);
    controller_handle = xTaskCreateStatic(controller, "control", 512, NULL, 2,
                                         controller_stack, &controller_tcb);
    configASSERT(worker_handle != NULL && controller_handle != NULL);
}
