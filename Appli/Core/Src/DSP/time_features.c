/**
 * @file time_features.c
 * @brief Time-domain feature extraction for sEMG signals
 * @note Optimized using CMSIS-DSP functions for ARM Cortex-M7
 * @author STM32 sEMG System Implementation
 * @date 2025
 */

#include "dsp_pipeline.h"
#include "semg_config.h"
#include <math.h>

/**
 * @brief Extract all time-domain features from filtered signal
 * @param signal: Input filtered sEMG signal
 * @param length: Signal length (typically SEMG_WINDOW_SIZE = 200)
 * @param features: Output time-domain feature structure
 */
HAL_StatusTypeDef DSP_ExtractTimeFeatures(const float32_t *signal, uint32_t length, time_features_t *features)
{
    if (signal == NULL || features == NULL || length == 0) {
        return HAL_ERROR;
    }
    
    START_TIMING();
    
    // Calculate RMS using CMSIS-DSP optimized function
    if (DSP_CalculateRMS(signal, length, &features->rms) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Calculate Mean Absolute Value (MAV)
    if (DSP_CalculateMAV(signal, length, &features->mav) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Calculate Waveform Length (WL)
    if (DSP_CalculateWL(signal, length, &features->wl) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Count Zero Crossings (ZC)
    if (DSP_CountZeroCrossings(signal, length, DSP_ZC_THRESHOLD, &features->zc) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Count Slope Sign Changes (SSC)
    if (DSP_CountSlopeSignChanges(signal, length, DSP_SSC_THRESHOLD, &features->ssc) != HAL_OK) {
        return HAL_ERROR;
    }
    
    END_TIMING(g_dsp_pipeline.feature_cycles);
    
    return HAL_OK;
}

/**
 * @brief Calculate Mean Absolute Value using CMSIS-DSP
 * @note MAV = (1/N) * Σ|x[n]|
 */
HAL_StatusTypeDef DSP_CalculateMAV(const float32_t *signal, uint32_t length, float32_t *mav)
{
    if (signal == NULL || mav == NULL) {
        return HAL_ERROR;
    }
    
    float32_t sum = 0.0f;
    
    // Use CMSIS-DSP vectorized absolute value and sum
    static float32_t abs_signal[SEMG_WINDOW_SIZE] AXI_SRAM_SECTION;
    
    // Calculate absolute values
    arm_abs_f32(signal, abs_signal, length);
    
    // Calculate mean of absolute values
    arm_mean_f32(abs_signal, length, mav);
    
    return HAL_OK;
}

/**
 * @brief Calculate Waveform Length
 * @note WL = Σ|x[n+1] - x[n]|
 */
HAL_StatusTypeDef DSP_CalculateWL(const float32_t *signal, uint32_t length, float32_t *wl)
{
    if (signal == NULL || wl == NULL || length < 2) {
        return HAL_ERROR;
    }
    
    *wl = 0.0f;
    
    // Calculate sum of absolute differences between consecutive samples
    for (uint32_t i = 0; i < length - 1; i++) {
        float32_t diff = signal[i + 1] - signal[i];
        *wl += fabsf(diff);
    }
    
    return HAL_OK;
}

/**
 * @brief Count Zero Crossings with threshold
 * @note ZC counts sign changes in the signal above a threshold
 */
HAL_StatusTypeDef DSP_CountZeroCrossings(const float32_t *signal, uint32_t length, float32_t threshold, uint16_t *zc)
{
    if (signal == NULL || zc == NULL || length < 2) {
        return HAL_ERROR;
    }
    
    *zc = 0;
    
    for (uint32_t i = 0; i < length - 1; i++) {
        // Check for sign change with threshold
        if (((signal[i] > threshold && signal[i + 1] < -threshold) ||
             (signal[i] < -threshold && signal[i + 1] > threshold)) &&
            (fabsf(signal[i] - signal[i + 1]) >= threshold)) {
            (*zc)++;
        }
    }
    
    return HAL_OK;
}

/**
 * @brief Count Slope Sign Changes with threshold
 * @note SSC counts changes in slope direction
 */
HAL_StatusTypeDef DSP_CountSlopeSignChanges(const float32_t *signal, uint32_t length, float32_t threshold, uint16_t *ssc)
{
    if (signal == NULL || ssc == NULL || length < 3) {
        return HAL_ERROR;
    }
    
    *ssc = 0;
    
    for (uint32_t i = 1; i < length - 1; i++) {
        // Calculate slopes
        float32_t slope1 = signal[i] - signal[i - 1];
        float32_t slope2 = signal[i + 1] - signal[i];
        
        // Check for slope sign change with threshold
        if (((slope1 > threshold && slope2 < -threshold) ||
             (slope1 < -threshold && slope2 > threshold)) &&
            (fabsf(slope1 - slope2) >= threshold)) {
            (*ssc)++;
        }
    }
    
    return HAL_OK;
}