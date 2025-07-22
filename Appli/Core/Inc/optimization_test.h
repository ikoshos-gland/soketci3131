#ifndef OPTIMIZATION_TEST_H
#define OPTIMIZATION_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"
#include "semg_config.h"
#include "max78000_interface.h"

/* Test Result Structure */
typedef struct {
    uint32_t feature_extraction_cycles;
    uint32_t quantization_cycles; 
    uint32_t transmission_cycles;
    uint32_t total_optimization_cycles;
    uint8_t packet_integrity_pass;
    uint8_t performance_improvement_percent;
    uint8_t feature_reduction_ratio;
} optimization_test_results_t;

/* Test Functions */

/**
 * @brief Run comprehensive optimization validation tests
 * @param results: Output test results structure
 * @return HAL_StatusTypeDef: HAL_OK if all tests pass
 */
HAL_StatusTypeDef OptimizationTest_RunAll(optimization_test_results_t *results);

/**
 * @brief Test feature extraction and quantization performance
 * @param results: Output test results
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef OptimizationTest_FeaturePerformance(optimization_test_results_t *results);

/**
 * @brief Test MAX78000 packet transmission and integrity
 * @param results: Output test results
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef OptimizationTest_PacketIntegrity(optimization_test_results_t *results);

/**
 * @brief Compare optimized vs original feature dimensions
 * @param results: Output test results  
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef OptimizationTest_DimensionCompliance(optimization_test_results_t *results);

/**
 * @brief Generate test report via UART debug
 * @param results: Test results to report
 */
void OptimizationTest_GenerateReport(const optimization_test_results_t *results);

#ifdef __cplusplus
}
#endif

#endif /* OPTIMIZATION_TEST_H */