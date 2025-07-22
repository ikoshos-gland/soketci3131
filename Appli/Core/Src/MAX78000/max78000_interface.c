#include "max78000_interface.h"
#include <string.h>
#include <math.h>

/* Global Variables */
feature_stats_t g_feature_stats AXI_SRAM_SECTION;
max78000_packet_t g_tx_packet AXI_SRAM_SECTION SIMD_ALIGN_32;
uint16_t g_packet_sequence = 0;

/* Performance Metrics */
static struct {
    uint32_t tx_count;
    uint32_t error_count;
    uint32_t last_latency_us;
} interface_metrics = {0};

/* Feature scaling constants - Optimized for sEMG characteristics */
static const float32_t INITIAL_SCALES[TOTAL_OPTIMIZED_FEATURES] = {
    // Channel 0-7: RMS scaling (typical range 0-0.1V)
    1270.0f, 1270.0f, 1270.0f, 1270.0f, 1270.0f, 1270.0f, 1270.0f, 1270.0f,
    // Channel 0-7: MAV scaling (typical range 0-0.05V) 
    2540.0f, 2540.0f, 2540.0f, 2540.0f, 2540.0f, 2540.0f, 2540.0f, 2540.0f,
    // Channel 0-7: MDF scaling (typical range 0-200Hz)
    0.635f, 0.635f, 0.635f, 0.635f, 0.635f, 0.635f, 0.635f, 0.635f,
    // Channel 0-7: MNF scaling (typical range 0-250Hz)
    0.508f, 0.508f, 0.508f, 0.508f, 0.508f, 0.508f, 0.508f, 0.508f,
    // Channel 0-7: AR Coeff1 scaling (typical range -2.0 to +2.0)
    63.5f, 63.5f, 63.5f, 63.5f, 63.5f, 63.5f, 63.5f, 63.5f,
    // Channel 0-7: AR Coeff2 scaling (typical range -2.0 to +2.0)
    63.5f, 63.5f, 63.5f, 63.5f, 63.5f, 63.5f, 63.5f, 63.5f,
    // Channel 0-7: Wavelet Band1 (typically 0-1.0)
    127.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.0f,
    // Channel 0-7: Wavelet Band2 (typically 0-1.0)
    127.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.0f
};

HAL_StatusTypeDef MAX78000_Interface_Init(SPI_HandleTypeDef *spi_handle) {
    if (spi_handle == NULL) {
        return HAL_ERROR;
    }
    
    // Initialize feature statistics
    for (int i = 0; i < TOTAL_OPTIMIZED_FEATURES; i++) {
        g_feature_stats.scale_factors[i] = INITIAL_SCALES[i];
        g_feature_stats.min_values[i] = 0.0f;
        g_feature_stats.max_values[i] = 0.0f;
    }
    g_feature_stats.update_count = 0;
    
    // Initialize packet header
    g_tx_packet.header = MAX78000_SYNC_BYTE;
    g_packet_sequence = 0;
    
    // Reset performance metrics
    interface_metrics.tx_count = 0;
    interface_metrics.error_count = 0;
    interface_metrics.last_latency_us = 0;
    
    return HAL_OK;
}

HAL_StatusTypeDef MAX78000_ExtractOptimizedFeatures(
    const semg_features_t *features, 
    max78000_packet_t *output_packet) {
    
    if (features == NULL || output_packet == NULL) {
        return HAL_ERROR;
    }
    
    START_TIMING();
    
    // Pack optimized features (8 features per channel * 8 channels = 64 features)
    uint8_t feature_idx = 0;
    
    for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        // Feature 0: RMS
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->time_domain[ch].rms, 
            ch * FEATURES_PER_CHANNEL + FEAT_RMS);
        feature_idx++;
        
        // Feature 1: MAV
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->time_domain[ch].mav, 
            ch * FEATURES_PER_CHANNEL + FEAT_MAV);
        feature_idx++;
        
        // Feature 2: MDF
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->freq_domain[ch].mdf, 
            ch * FEATURES_PER_CHANNEL + FEAT_MDF);
        feature_idx++;
        
        // Feature 3: MNF
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->freq_domain[ch].mnf, 
            ch * FEATURES_PER_CHANNEL + FEAT_MNF);
        feature_idx++;
        
        // Feature 4: AR Coefficient 1 (most significant)
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->parametric[ch].coefficients[0], 
            ch * FEATURES_PER_CHANNEL + FEAT_AR_COEFF1);
        feature_idx++;
        
        // Feature 5: AR Coefficient 2
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->parametric[ch].coefficients[1], 
            ch * FEATURES_PER_CHANNEL + FEAT_AR_COEFF2);
        feature_idx++;
        
        // Feature 6: Wavelet Band 1 (dominant band)
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->wavelet[ch].band_energies[0], 
            ch * FEATURES_PER_CHANNEL + FEAT_WAVELET_BAND1);
        feature_idx++;
        
        // Feature 7: Wavelet Band 2
        output_packet->features[feature_idx] = MAX78000_AdaptiveQuantize(
            features->wavelet[ch].band_energies[1], 
            ch * FEATURES_PER_CHANNEL + FEAT_WAVELET_BAND2);
        feature_idx++;
    }
    
    // Update packet metadata
    output_packet->header = MAX78000_SYNC_BYTE;
    output_packet->sequence = ++g_packet_sequence;
    output_packet->checksum = MAX78000_CalculateChecksum(output_packet);
    
    uint32_t cycles;
    END_TIMING(cycles);
    
    return HAL_OK;
}

HAL_StatusTypeDef MAX78000_TransmitPacket(const max78000_packet_t *packet) {
    if (packet == NULL) {
        return HAL_ERROR;
    }
    
    uint32_t start_time = HAL_GetTick();
    HAL_StatusTypeDef result;
    
    // Cache management for STM32H7RS - ensure data coherency
    MAX78000_PrepareCache((void*)packet, sizeof(max78000_packet_t));
    
    // Transmit packet with DMA for optimal performance
    result = HAL_SPI_Transmit_DMA(&hspi2, (uint8_t*)packet, sizeof(max78000_packet_t));
    
    if (result == HAL_OK) {
        // Wait for transmission complete with timeout
        uint32_t timeout = HAL_GetTick() + MAX78000_SPI_TIMEOUT_MS;
        while (HAL_SPI_GetState(&hspi2) != HAL_SPI_STATE_READY) {
            if (HAL_GetTick() > timeout) {
                interface_metrics.error_count++;
                return HAL_TIMEOUT;
            }
        }
        
        interface_metrics.tx_count++;
        interface_metrics.last_latency_us = (HAL_GetTick() - start_time) * 1000;
    } else {
        interface_metrics.error_count++;
    }
    
    return result;
}

HAL_StatusTypeDef MAX78000_ReceiveResponse(uint8_t *response_buffer, uint16_t buffer_size) {
    if (response_buffer == NULL || buffer_size == 0) {
        return HAL_ERROR;
    }
    
    // Prepare cache for DMA reception
    MAX78000_InvalidateCache(response_buffer, buffer_size);
    
    // Receive response via DMA
    HAL_StatusTypeDef result = HAL_SPI_Receive_DMA(&hspi2, response_buffer, buffer_size);
    
    if (result == HAL_OK) {
        // Wait for reception complete
        uint32_t timeout = HAL_GetTick() + MAX78000_SPI_TIMEOUT_MS;
        while (HAL_SPI_GetState(&hspi2) != HAL_SPI_STATE_READY) {
            if (HAL_GetTick() > timeout) {
                return HAL_TIMEOUT;
            }
        }
        
        // Invalidate cache after DMA reception
        MAX78000_InvalidateCache(response_buffer, buffer_size);
    }
    
    return result;
}

void MAX78000_UpdateFeatureStats(const semg_features_t *features) {
    if (features == NULL) return;
    
    float32_t current_features[TOTAL_OPTIMIZED_FEATURES];
    uint8_t idx = 0;
    
    // Extract current feature values
    for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        current_features[idx++] = features->time_domain[ch].rms;
        current_features[idx++] = features->time_domain[ch].mav;
        current_features[idx++] = features->freq_domain[ch].mdf;
        current_features[idx++] = features->freq_domain[ch].mnf;
        current_features[idx++] = features->parametric[ch].coefficients[0];
        current_features[idx++] = features->parametric[ch].coefficients[1];
        current_features[idx++] = features->wavelet[ch].band_energies[0];
        current_features[idx++] = features->wavelet[ch].band_energies[1];
    }
    
    // Update running statistics with exponential moving average
    const float32_t alpha = 0.01f; // Smoothing factor
    
    for (int i = 0; i < TOTAL_OPTIMIZED_FEATURES; i++) {
        if (g_feature_stats.update_count == 0) {
            g_feature_stats.min_values[i] = current_features[i];
            g_feature_stats.max_values[i] = current_features[i];
        } else {
            // Update min/max with smoothing
            if (current_features[i] < g_feature_stats.min_values[i]) {
                g_feature_stats.min_values[i] = 
                    (1.0f - alpha) * g_feature_stats.min_values[i] + alpha * current_features[i];
            }
            if (current_features[i] > g_feature_stats.max_values[i]) {
                g_feature_stats.max_values[i] = 
                    (1.0f - alpha) * g_feature_stats.max_values[i] + alpha * current_features[i];
            }
        }
        
        // Adaptive scale factor update
        float32_t range = g_feature_stats.max_values[i] - g_feature_stats.min_values[i];
        if (range > 0.001f) { // Avoid division by zero
            g_feature_stats.scale_factors[i] = 254.0f / range; // Use 254 to avoid saturation
        }
    }
    
    g_feature_stats.update_count++;
}

int8_t MAX78000_AdaptiveQuantize(float32_t feature_value, uint8_t feature_index) {
    if (feature_index >= TOTAL_OPTIMIZED_FEATURES) {
        return 0;
    }
    
    // Apply adaptive scaling
    float32_t scaled_value = (feature_value - g_feature_stats.min_values[feature_index]) 
                           * g_feature_stats.scale_factors[feature_index] - 127.0f;
    
    // Saturate to int8_t range using ARM CMSIS intrinsic
    return (int8_t)__SSAT((int32_t)scaled_value, 8);
}

uint8_t MAX78000_CalculateChecksum(const max78000_packet_t *packet) {
    if (packet == NULL) return 0;
    
    uint8_t checksum = packet->header;
    checksum ^= (uint8_t)(packet->sequence & 0xFF);
    checksum ^= (uint8_t)((packet->sequence >> 8) & 0xFF);
    
    for (int i = 0; i < MAX78000_FEATURES_COUNT; i++) {
        checksum ^= (uint8_t)packet->features[i];
    }
    
    return checksum;
}

HAL_StatusTypeDef MAX78000_ValidatePacket(const max78000_packet_t *packet) {
    if (packet == NULL) {
        return HAL_ERROR;
    }
    
    if (packet->header != MAX78000_SYNC_BYTE) {
        return HAL_ERROR;
    }
    
    uint8_t calculated_checksum = MAX78000_CalculateChecksum(packet);
    if (calculated_checksum != packet->checksum) {
        return HAL_ERROR;
    }
    
    return HAL_OK;
}

void MAX78000_ResetStats(void) {
    memset(&g_feature_stats, 0, sizeof(feature_stats_t));
    
    // Restore initial scale factors
    for (int i = 0; i < TOTAL_OPTIMIZED_FEATURES; i++) {
        g_feature_stats.scale_factors[i] = INITIAL_SCALES[i];
    }
    
    g_packet_sequence = 0;
    memset(&interface_metrics, 0, sizeof(interface_metrics));
}

void MAX78000_GetPerformanceMetrics(uint32_t *tx_count, uint32_t *error_count, uint32_t *last_latency_us) {
    if (tx_count) *tx_count = interface_metrics.tx_count;
    if (error_count) *error_count = interface_metrics.error_count;
    if (last_latency_us) *last_latency_us = interface_metrics.last_latency_us;
}