#ifndef SEMG_CONFIG_H
#define SEMG_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"
#include "arm_math.h"
#include <stdint.h>

/* System Configuration Constants - Based on PRP Requirements */
#define SEMG_CHANNELS                    8
#define SEMG_SAMPLE_RATE                 1000
#define SEMG_WINDOW_SIZE                 200
#define SEMG_WINDOW_OVERLAP              100
#define SEMG_PROCESSING_DEADLINE_MS      2

/* Hardware Configuration - STM32H7RS Specific */
#define ADS1299_SPI_SPEED                10000000  // 10MHz max
#define DMA_BUFFER_SIZE                  (SEMG_WINDOW_SIZE * SEMG_CHANNELS)
#define SYSTEM_CLOCK_FREQUENCY           600000000  // 600MHz STM32H7RS
#define PROCESSING_DEADLINE_CYCLES       (SYSTEM_CLOCK_FREQUENCY / 1000 * SEMG_PROCESSING_DEADLINE_MS)

/* Memory Section Definitions - STM32H7RS Optimized */
#define AXI_SRAM_SECTION                 __attribute__((section(".AXI_SRAM")))
#define SRAM1_SECTION                    __attribute__((section(".SRAM1")))
#define SRAM2_SECTION                    __attribute__((section(".SRAM2")))

/* DSP Configuration */
#define FFT_SIZE                         256
#define BIQUAD_STAGES_HP                 3    // 10Hz highpass
#define BIQUAD_STAGES_BP                 3    // 450Hz lowpass
#define AR_MODEL_ORDER                   4    // AR(4) model
#define WAVELET_LEVELS                   3    // 3-level decomposition

/* FreeRTOS Task Priorities - Based on Real-Time Requirements */
#define ACQUISITION_TASK_PRIORITY        6    // Highest - data ready handling
#define PROCESSING_TASK_PRIORITY         5    // High - DSP pipeline
#define STREAMING_TASK_PRIORITY          4    // Medium - communication
#define MONITOR_TASK_PRIORITY            3    // Low - diagnostics

/* Task Stack Sizes (in words) */
#define ACQUISITION_STACK_SIZE           512
#define PROCESSING_STACK_SIZE            1024
#define STREAMING_STACK_SIZE             512
#define MONITOR_STACK_SIZE               256

/* Error Codes */
#define SEMG_OK                          0
#define SEMG_ERROR_TIMEOUT               1
#define SEMG_ERROR_DMA                   2
#define SEMG_ERROR_SPI                   3
#define SEMG_ERROR_PROCESSING_DEADLINE   4
#define SEMG_ERROR_INVALID_PARAMETER     5

/* Performance Monitoring */
#define ENABLE_PERFORMANCE_MONITORING    1
#define ENABLE_STACK_MONITORING          1
#define ENABLE_UART_DEBUG                1

/* Core Data Structures */
typedef struct {
    float32_t channels[SEMG_CHANNELS][SEMG_WINDOW_SIZE];
    uint32_t timestamp;
    uint8_t valid_channels;
    uint32_t sequence_number;
} semg_window_t;

typedef struct {
    float32_t rms;
    float32_t mav; 
    float32_t wl;
    uint16_t zc;
    uint16_t ssc;
} time_features_t;

typedef struct {
    float32_t mdf;  // Median frequency
    float32_t mnf;  // Mean frequency
    float32_t total_power;
} freq_features_t;

typedef struct {
    float32_t coefficients[AR_MODEL_ORDER];
    float32_t prediction_error;
} ar_features_t;

typedef struct {
    float32_t band_energies[4];  // 3-level decomposition = 4 bands
    float32_t total_energy;
} wavelet_features_t;

typedef struct {
    time_features_t time_domain[SEMG_CHANNELS];
    freq_features_t freq_domain[SEMG_CHANNELS];
    ar_features_t parametric[SEMG_CHANNELS];
    wavelet_features_t wavelet[SEMG_CHANNELS];
    uint32_t processing_time_cycles;
    uint32_t timestamp;
} semg_features_t;

/* Performance Monitoring Structure */
typedef struct {
    uint32_t processing_cycles_min;
    uint32_t processing_cycles_max;
    uint32_t processing_cycles_avg;
    uint32_t deadline_violations;
    uint32_t total_windows_processed;
    uint32_t cpu_utilization_percent;
    uint32_t memory_usage_bytes;
} performance_metrics_t;

/* Global Configuration Variables */
extern volatile uint32_t g_semg_system_enabled;
extern volatile uint32_t g_streaming_enabled;
extern performance_metrics_t g_performance_metrics;

/* Timing Macros for Performance Monitoring */
#if ENABLE_PERFORMANCE_MONITORING
#define START_TIMING()     uint32_t start_cycles = DWT->CYCCNT
#define END_TIMING(var)    (var) = DWT->CYCCNT - start_cycles
#else
#define START_TIMING()     do {} while(0)
#define END_TIMING(var)    do {} while(0)
#endif

/* Debug Macros */
#if ENABLE_UART_DEBUG
#define DEBUG_PRINT(msg)   UART_Transmit(msg)
#else
#define DEBUG_PRINT(msg)   do {} while(0)
#endif

/* Memory Alignment for SIMD Operations */
#define SIMD_ALIGN_32      __attribute__((aligned(32)))

/* Cache Management Macros for STM32H7RS */
#define CACHE_CLEAN_DCACHE()              SCB_CleanDCache()
#define CACHE_INVALIDATE_DCACHE()         SCB_InvalidateDCache()
#define CACHE_CLEAN_INVALIDATE_DCACHE()   SCB_CleanInvalidateDCache()
#define CACHE_CLEAN_BY_ADDR(addr, size)   SCB_CleanDCache_by_Addr((uint32_t*)(addr), (size))
#define CACHE_INVALIDATE_BY_ADDR(addr, size) SCB_InvalidateDCache_by_Addr((uint32_t*)(addr), (size))

#ifdef __cplusplus
}
#endif

#endif /* SEMG_CONFIG_H */