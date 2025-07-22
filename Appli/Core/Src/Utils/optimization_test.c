#include "optimization_test.h"
#include "dsp_pipeline.h"
#include <string.h>
#include <stdio.h>

/* Test Data - Synthetic sEMG features for validation */
static const semg_features_t test_features = {
    .time_domain = {
        {.rms = 0.05f, .mav = 0.03f, .wl = 2.5f, .zc = 15, .ssc = 12},
        {.rms = 0.04f, .mav = 0.025f, .wl = 2.1f, .zc = 18, .ssc = 14},
        {.rms = 0.06f, .mav = 0.035f, .wl = 2.8f, .zc = 12, .ssc = 10},
        {.rms = 0.045f, .mav = 0.028f, .wl = 2.3f, .zc = 16, .ssc = 13},
        {.rms = 0.055f, .mav = 0.032f, .wl = 2.6f, .zc = 14, .ssc = 11},
        {.rms = 0.042f, .mav = 0.026f, .wl = 2.2f, .zc = 17, .ssc = 15},
        {.rms = 0.058f, .mav = 0.034f, .wl = 2.9f, .zc = 11, .ssc = 9},
        {.rms = 0.048f, .mav = 0.029f, .wl = 2.4f, .zc = 16, .ssc = 12}
    },
    .freq_domain = {
        {.mdf = 85.5f, .mnf = 92.3f, .total_power = 0.025f},
        {.mdf = 78.2f, .mnf = 87.1f, .total_power = 0.019f},
        {.mdf = 91.8f, .mnf = 98.5f, .total_power = 0.031f},
        {.mdf = 82.4f, .mnf = 89.7f, .total_power = 0.023f},
        {.mdf = 88.1f, .mnf = 94.2f, .total_power = 0.027f},
        {.mdf = 79.6f, .mnf = 86.3f, .total_power = 0.021f},
        {.mdf = 93.2f, .mnf = 99.8f, .total_power = 0.033f},
        {.mdf = 84.7f, .mnf = 91.4f, .total_power = 0.025f}
    },
    .parametric = {
        {.coefficients = {1.2f, -0.8f, 0.3f, -0.1f}, .prediction_error = 0.05f},
        {.coefficients = {1.1f, -0.7f, 0.25f, -0.08f}, .prediction_error = 0.04f},
        {.coefficients = {1.3f, -0.9f, 0.35f, -0.12f}, .prediction_error = 0.06f},
        {.coefficients = {1.15f, -0.75f, 0.28f, -0.09f}, .prediction_error = 0.045f},
        {.coefficients = {1.25f, -0.85f, 0.32f, -0.11f}, .prediction_error = 0.055f},
        {.coefficients = {1.05f, -0.65f, 0.22f, -0.07f}, .prediction_error = 0.035f},
        {.coefficients = {1.35f, -0.95f, 0.38f, -0.13f}, .prediction_error = 0.065f},
        {.coefficients = {1.18f, -0.78f, 0.3f, -0.1f}, .prediction_error = 0.048f}
    },
    .wavelet = {
        {.band_energies = {0.4f, 0.3f, 0.2f, 0.1f}, .total_energy = 1.0f},
        {.band_energies = {0.35f, 0.32f, 0.23f, 0.1f}, .total_energy = 1.0f},
        {.band_energies = {0.42f, 0.28f, 0.18f, 0.12f}, .total_energy = 1.0f},
        {.band_energies = {0.38f, 0.31f, 0.21f, 0.1f}, .total_energy = 1.0f},
        {.band_energies = {0.41f, 0.29f, 0.19f, 0.11f}, .total_energy = 1.0f},
        {.band_energies = {0.36f, 0.33f, 0.22f, 0.09f}, .total_energy = 1.0f},
        {.band_energies = {0.43f, 0.27f, 0.17f, 0.13f}, .total_energy = 1.0f},
        {.band_energies = {0.39f, 0.3f, 0.2f, 0.11f}, .total_energy = 1.0f}
    },
    .processing_time_cycles = 50000,
    .timestamp = 1000
};

HAL_StatusTypeDef OptimizationTest_RunAll(optimization_test_results_t *results) {
    if (results == NULL) {
        return HAL_ERROR;
    }
    
    HAL_StatusTypeDef status;
    memset(results, 0, sizeof(optimization_test_results_t));
    
    // Test 1: Feature Performance
    status = OptimizationTest_FeaturePerformance(results);
    if (status != HAL_OK) {
        return status;
    }
    
    // Test 2: Packet Integrity  
    status = OptimizationTest_PacketIntegrity(results);
    if (status != HAL_OK) {
        return status;
    }
    
    // Test 3: Dimension Compliance
    status = OptimizationTest_DimensionCompliance(results);
    if (status != HAL_OK) {
        return status;
    }
    
    // Calculate overall improvement
    uint32_t original_features = 128;  // Original feature count
    uint32_t optimized_features = MAX78000_FEATURES_COUNT;  // 64
    results->feature_reduction_ratio = (uint8_t)((100 * (original_features - optimized_features)) / original_features);
    
    // Estimate performance improvement (bandwidth reduction)
    uint32_t original_packet_size = sizeof(semg_features_t);  // ~2KB
    uint32_t optimized_packet_size = sizeof(max78000_packet_t);  // 68 bytes
    results->performance_improvement_percent = (uint8_t)((100 * (original_packet_size - optimized_packet_size)) / original_packet_size);
    
    return HAL_OK;
}

HAL_StatusTypeDef OptimizationTest_FeaturePerformance(optimization_test_results_t *results) {
    uint32_t start_cycles, end_cycles;
    max78000_packet_t test_packet;
    
    // Test feature extraction performance
    start_cycles = DWT->CYCCNT;
    
    HAL_StatusTypeDef status = MAX78000_ExtractOptimizedFeatures(&test_features, &test_packet);
    
    end_cycles = DWT->CYCCNT;
    results->feature_extraction_cycles = end_cycles - start_cycles;
    
    if (status != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Test quantization performance individually
    start_cycles = DWT->CYCCNT;
    
    for (int i = 0; i < MAX78000_FEATURES_COUNT; i++) {
        volatile int8_t quantized = MAX78000_AdaptiveQuantize(50.5f, i % TOTAL_OPTIMIZED_FEATURES);
        (void)quantized;  // Suppress unused variable warning
    }
    
    end_cycles = DWT->CYCCNT;
    results->quantization_cycles = end_cycles - start_cycles;
    
    return HAL_OK;
}

HAL_StatusTypeDef OptimizationTest_PacketIntegrity(optimization_test_results_t *results) {
    max78000_packet_t test_packet;
    
    // Extract features to packet
    HAL_StatusTypeDef status = MAX78000_ExtractOptimizedFeatures(&test_features, &test_packet);
    if (status != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Verify packet structure
    if (test_packet.header != MAX78000_SYNC_BYTE) {
        results->packet_integrity_pass = 0;
        return HAL_ERROR;
    }
    
    // Verify checksum
    uint8_t calculated_checksum = MAX78000_CalculateChecksum(&test_packet);
    if (calculated_checksum != test_packet.checksum) {
        results->packet_integrity_pass = 0;
        return HAL_ERROR;
    }
    
    // Verify packet validation function
    status = MAX78000_ValidatePacket(&test_packet);
    if (status != HAL_OK) {
        results->packet_integrity_pass = 0;
        return HAL_ERROR;
    }
    
    // Test transmission timing (without actual SPI transfer)
    uint32_t start_cycles = DWT->CYCCNT;
    
    // Simulate cache operations
    MAX78000_PrepareCache(&test_packet, sizeof(max78000_packet_t));
    
    uint32_t end_cycles = DWT->CYCCNT;
    results->transmission_cycles = end_cycles - start_cycles;
    
    results->packet_integrity_pass = 1;
    return HAL_OK;
}

HAL_StatusTypeDef OptimizationTest_DimensionCompliance(optimization_test_results_t *results) {
    // Verify MAX78000 dimension constraints
    uint32_t C = 1;  // Single channel input to MAX78000
    uint32_t H = MAX78000_FEATURES_COUNT;  // Height = number of features
    uint32_t W = 1;  // Width = 1
    
    uint32_t total_elements = C * H * W;
    
    // Check against MAX78000 constraint: C×H×W ≤ 16,384
    if (total_elements > 16384) {
        return HAL_ERROR;
    }
    
    // Verify feature count matches expectation
    if (MAX78000_FEATURES_COUNT != 64) {
        return HAL_ERROR;
    }
    
    // Verify packet size is reasonable
    if (sizeof(max78000_packet_t) > 100) {  // Should be 68 bytes
        return HAL_ERROR;
    }
    
    results->total_optimization_cycles = results->feature_extraction_cycles + 
                                        results->quantization_cycles + 
                                        results->transmission_cycles;
    
    return HAL_OK;
}

void OptimizationTest_GenerateReport(const optimization_test_results_t *results) {
    #if ENABLE_UART_DEBUG
    char report[512];
    
    snprintf(report, sizeof(report),
        "\r\n=== OPTIMIZATION TEST REPORT ===\r\n"
        "Feature Extraction: %lu cycles (%.2f ms)\r\n"
        "Quantization: %lu cycles (%.2f ms)\r\n" 
        "Transmission Prep: %lu cycles (%.2f ms)\r\n"
        "Total Optimization: %lu cycles (%.2f ms)\r\n"
        "Packet Integrity: %s\r\n"
        "Feature Reduction: %d%% (128 -> %d features)\r\n"
        "Bandwidth Improvement: %d%% (2KB -> 68 bytes)\r\n"
        "MAX78000 Compatibility: C×H×W = 1×64×1 = 64 ≤ 16,384 ✓\r\n"
        "=== END REPORT ===\r\n\r\n",
        results->feature_extraction_cycles,
        (float)results->feature_extraction_cycles / SYSTEM_CLOCK_FREQUENCY * 1000.0f,
        results->quantization_cycles,
        (float)results->quantization_cycles / SYSTEM_CLOCK_FREQUENCY * 1000.0f,
        results->transmission_cycles,
        (float)results->transmission_cycles / SYSTEM_CLOCK_FREQUENCY * 1000.0f,
        results->total_optimization_cycles,
        (float)results->total_optimization_cycles / SYSTEM_CLOCK_FREQUENCY * 1000.0f,
        results->packet_integrity_pass ? "PASS" : "FAIL",
        results->feature_reduction_ratio,
        MAX78000_FEATURES_COUNT,
        results->performance_improvement_percent
    );
    
    DEBUG_PRINT(report);
    #endif
}

/**
 * @brief Run optimization tests from main application
 * Call this function to validate the optimization implementation
 */
void OptimizationTest_Execute(void) {
    optimization_test_results_t test_results;
    
    DEBUG_PRINT("Starting optimization validation tests...\r\n");
    
    HAL_StatusTypeDef status = OptimizationTest_RunAll(&test_results);
    
    if (status == HAL_OK) {
        DEBUG_PRINT("All optimization tests PASSED!\r\n");
    } else {
        DEBUG_PRINT("Optimization tests FAILED!\r\n");
    }
    
    OptimizationTest_GenerateReport(&test_results);
}