#ifndef MAX78000_INTERFACE_H
#define MAX78000_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"
#include "arm_math.h"
#include "semg_config.h"

/* MAX78000 Interface Configuration */
#define MAX78000_SPI_TIMEOUT_MS          100
#define MAX78000_RETRY_COUNT             3
#define MAX78000_SYNC_BYTE               0xAA
#define MAX78000_ACK_BYTE                0x55

/* Feature Selection Configuration - Optimized for MAX78000 */
#define FEATURES_PER_CHANNEL             8       // Reduced from 16 to 8
#define TOTAL_OPTIMIZED_FEATURES         (FEATURES_PER_CHANNEL * SEMG_CHANNELS) // 64 total

/* Feature Indices for Optimized Selection */
typedef enum {
    FEAT_RMS = 0,
    FEAT_MAV = 1,
    FEAT_MDF = 2,
    FEAT_MNF = 3,
    FEAT_AR_COEFF1 = 4,
    FEAT_AR_COEFF2 = 5,
    FEAT_WAVELET_BAND1 = 6,
    FEAT_WAVELET_BAND2 = 7
} feature_index_t;

/* Global Variables */
extern SPI_HandleTypeDef hspi2; // SPI2 for MAX78000 communication
extern feature_stats_t g_feature_stats;
extern max78000_packet_t g_tx_packet AXI_SRAM_SECTION SIMD_ALIGN_32;
extern uint16_t g_packet_sequence;

/* Function Prototypes */

/**
 * @brief Initialize MAX78000 interface and SPI communication
 * @param spi_handle: SPI handle for MAX78000 communication
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef MAX78000_Interface_Init(SPI_HandleTypeDef *spi_handle);

/**
 * @brief Extract and quantize optimized feature set for MAX78000
 * @param features: Input full feature structure from DSP pipeline
 * @param output_packet: Output quantized packet for MAX78000
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef MAX78000_ExtractOptimizedFeatures(
    const semg_features_t *features, 
    max78000_packet_t *output_packet);

/**
 * @brief Send feature packet to MAX78000 via SPI with cache coherency
 * @param packet: Packet to transmit
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef MAX78000_TransmitPacket(const max78000_packet_t *packet);

/**
 * @brief Receive response from MAX78000 (inference results)
 * @param response_buffer: Buffer for response data
 * @param buffer_size: Size of response buffer
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef MAX78000_ReceiveResponse(uint8_t *response_buffer, uint16_t buffer_size);

/**
 * @brief Update feature statistics for adaptive quantization
 * @param features: Current feature vector
 */
void MAX78000_UpdateFeatureStats(const semg_features_t *features);

/**
 * @brief Perform adaptive quantization based on statistics
 * @param feature_value: Input float32 feature value
 * @param feature_index: Feature index for statistics lookup
 * @return int8_t: Quantized value [-128, +127]
 */
int8_t MAX78000_AdaptiveQuantize(float32_t feature_value, uint8_t feature_index);

/**
 * @brief Calculate packet checksum
 * @param packet: Input packet
 * @return uint8_t: XOR checksum
 */
uint8_t MAX78000_CalculateChecksum(const max78000_packet_t *packet);

/**
 * @brief Validate packet integrity
 * @param packet: Packet to validate
 * @return HAL_StatusTypeDef: HAL_OK if valid
 */
HAL_StatusTypeDef MAX78000_ValidatePacket(const max78000_packet_t *packet);

/**
 * @brief Reset feature statistics (for system restart)
 */
void MAX78000_ResetStats(void);

/**
 * @brief Get interface performance metrics
 * @param tx_count: Output transmission count
 * @param error_count: Output error count
 * @param last_latency_us: Output last transmission latency
 */
void MAX78000_GetPerformanceMetrics(uint32_t *tx_count, uint32_t *error_count, uint32_t *last_latency_us);

/* Inline optimization functions for critical path */
static inline void MAX78000_PrepareCache(void *addr, uint32_t size) {
    SCB_CleanDCache_by_Addr((uint32_t*)addr, size);
}

static inline void MAX78000_InvalidateCache(void *addr, uint32_t size) {
    SCB_InvalidateDCache_by_Addr((uint32_t*)addr, size);
}

#ifdef __cplusplus
}
#endif

#endif /* MAX78000_INTERFACE_H */