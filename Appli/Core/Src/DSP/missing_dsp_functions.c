#include "dsp_pipeline.h"
#include "semg_config.h"
#include "arm_math.h"
#include <string.h>

/* PI constant definition for STM32CubeIDE compatibility */
#ifndef PI
#define PI 3.14159265359f
#endif

/**
 * @brief Extract AR (Autoregressive) model features from EMG signal
 * @param input: Input signal buffer
 * @param length: Length of input signal
 * @param ar_coeffs: Output AR coefficients
 * @param order: AR model order
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ExtractARFeatures(const float32_t* input, uint32_t length, 
                                       float32_t* ar_coeffs, uint32_t order)
{
    if (input == NULL || ar_coeffs == NULL || length < order + 1) {
        return HAL_ERROR;
    }
    
    /* Levinson-Durbin algorithm for AR parameter estimation */
    static float32_t autocorr[DSP_AR_ORDER + 1];
    static float32_t reflection_coeffs[DSP_AR_ORDER];
    
    /* Calculate autocorrelation sequence */
    for (uint32_t k = 0; k <= order; k++) {
        autocorr[k] = 0.0f;
        for (uint32_t n = 0; n < length - k; n++) {
            autocorr[k] += input[n] * input[n + k];
        }
        autocorr[k] /= (float32_t)(length - k);
    }
    
    /* Levinson-Durbin recursion */
    float32_t error = autocorr[0];
    
    for (uint32_t m = 1; m <= order; m++) {
        /* Calculate reflection coefficient */
        float32_t sum = autocorr[m];
        for (uint32_t j = 1; j < m; j++) {
            sum += ar_coeffs[j - 1] * autocorr[m - j];
        }
        reflection_coeffs[m - 1] = -sum / error;
        
        /* Update AR coefficients */
        ar_coeffs[m - 1] = reflection_coeffs[m - 1];
        for (uint32_t j = 1; j < m; j++) {
            float32_t temp = ar_coeffs[j - 1];
            ar_coeffs[j - 1] = temp + reflection_coeffs[m - 1] * ar_coeffs[m - j - 1];
        }
        
        /* Update prediction error */
        error *= (1.0f - reflection_coeffs[m - 1] * reflection_coeffs[m - 1]);
    }
    
    return HAL_OK;
}

/**
 * @brief Extract Wavelet features from EMG signal using Daubechies-4 wavelet
 * @param input: Input signal buffer
 * @param length: Length of input signal
 * @param wavelet_features: Output wavelet features
 * @param num_features: Number of output features
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ExtractWaveletFeatures(const float32_t* input, uint32_t length,
                                            float32_t* wavelet_features, uint32_t num_features)
{
    if (input == NULL || wavelet_features == NULL || length < 16 || num_features < DSP_WAVELET_SUBBANDS) {
        return HAL_ERROR;
    }
    
    /* Daubechies-4 wavelet coefficients */
    static const float32_t db4_coeffs[8] = {
        -0.010597401784f, 0.032883011667f, 0.030841381836f, -0.187034811719f,
        -0.027983769417f, 0.630880767930f, 0.714846570553f, 0.230377813309f
    };
    
    /* Simplified 3-level wavelet decomposition */
    static float32_t temp_buffer[SEMG_WINDOW_SIZE];
    static float32_t detail_coeffs[DSP_WAVELET_LEVELS][SEMG_WINDOW_SIZE/2];
    
    memcpy(temp_buffer, input, length * sizeof(float32_t));
    
    uint32_t current_length = length;
    
    /* Perform 3-level decomposition */
    for (uint32_t level = 0; level < DSP_WAVELET_LEVELS && current_length >= 8; level++) {
        uint32_t half_length = current_length / 2;
        
        /* High-pass filtering (detail coefficients) */
        for (uint32_t i = 0; i < half_length; i++) {
            detail_coeffs[level][i] = 0.0f;
            for (uint32_t j = 0; j < 8 && (2*i + j) < current_length; j++) {
                /* High-pass filter using quadrature mirror filter */
                float32_t hp_coeff = (j % 2 == 0) ? -db4_coeffs[7-j] : db4_coeffs[7-j];
                detail_coeffs[level][i] += temp_buffer[2*i + j] * hp_coeff;
            }
        }
        
        /* Low-pass filtering for next level */
        for (uint32_t i = 0; i < half_length; i++) {
            float32_t sum = 0.0f;
            for (uint32_t j = 0; j < 8 && (2*i + j) < current_length; j++) {
                sum += temp_buffer[2*i + j] * db4_coeffs[j];
            }
            temp_buffer[i] = sum;
        }
        
        current_length = half_length;
    }
    
    /* Extract energy features from each subband */
    for (uint32_t level = 0; level < DSP_WAVELET_LEVELS && level < num_features; level++) {
        float32_t energy = 0.0f;
        uint32_t subband_length = length / (2 << level);
        
        /* Calculate energy in detail coefficients */
        for (uint32_t i = 0; i < subband_length; i++) {
            energy += detail_coeffs[level][i] * detail_coeffs[level][i];
        }
        
        wavelet_features[level] = sqrtf(energy / subband_length);
    }
    
    /* Add approximation coefficients energy as the last feature */
    if (num_features > DSP_WAVELET_LEVELS) {
        float32_t approx_energy = 0.0f;
        for (uint32_t i = 0; i < current_length; i++) {
            approx_energy += temp_buffer[i] * temp_buffer[i];
        }
        wavelet_features[DSP_WAVELET_LEVELS] = sqrtf(approx_energy / current_length);
    }
    
    return HAL_OK;
}

/**
 * @brief Apply Hanning window to input signal for FFT preprocessing
 * @param input: Input signal buffer
 * @param output: Output windowed signal buffer
 * @param length: Length of signal
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ApplyHanningWindow(const float32_t* input, float32_t* output, uint32_t length)
{
    if (input == NULL || output == NULL || length == 0) {
        return HAL_ERROR;
    }
    
    /* Apply Hanning window: w(n) = 0.5 * (1 - cos(2*π*n/(N-1))) */
    for (uint32_t n = 0; n < length; n++) {
        float32_t window_coeff = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * n / (length - 1)));
        output[n] = input[n] * window_coeff;
    }
    
    return HAL_OK;
}

/**
 * @brief Process complete data window through DSP pipeline
 * @param input_window: Input data window
 * @param feature_result: Output feature vector
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ProcessWindow(const data_window_t* input_window, feature_vector_t* feature_result)
{
    if (input_window == NULL || feature_result == NULL) {
        return HAL_ERROR;
    }
    
    static float32_t filtered_data[ADS1299_NUM_CHANNELS][SEMG_WINDOW_SIZE];
    static float32_t time_features[SEMG_CHANNELS * NUM_TIME_FEATURES];
    static float32_t freq_features[SEMG_CHANNELS * NUM_FREQ_FEATURES];
    static float32_t ar_features[SEMG_CHANNELS * DSP_AR_ORDER];
    static float32_t wavelet_features[SEMG_CHANNELS * DSP_WAVELET_SUBBANDS];
    
    /* Apply filtering to each channel */
    for (uint32_t ch = 0; ch < ADS1299_NUM_CHANNELS && ch < SEMG_CHANNELS; ch++) {
        /* Apply filter chain from filter_chain.c */
        if (DSP_ApplyFilterChain((float32_t*)input_window->data[ch], 
                               filtered_data[ch], SEMG_WINDOW_SIZE, ch) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    /* Extract time domain features */
    for (uint32_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        if (DSP_ExtractTimeFeatures(filtered_data[ch], SEMG_WINDOW_SIZE,
                                   &time_features[ch * NUM_TIME_FEATURES]) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    /* Extract frequency domain features */
    for (uint32_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        if (DSP_ExtractFreqFeatures(filtered_data[ch], SEMG_WINDOW_SIZE,
                                   &freq_features[ch * NUM_FREQ_FEATURES]) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    /* Extract AR features */
    for (uint32_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        if (DSP_ExtractARFeatures(filtered_data[ch], SEMG_WINDOW_SIZE,
                                 &ar_features[ch * DSP_AR_ORDER], DSP_AR_ORDER) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    /* Extract Wavelet features */
    for (uint32_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        if (DSP_ExtractWaveletFeatures(filtered_data[ch], SEMG_WINDOW_SIZE,
                                      &wavelet_features[ch * DSP_WAVELET_SUBBANDS], 
                                      DSP_WAVELET_SUBBANDS) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    /* Combine all features into output vector */
    feature_result->timestamp = input_window->timestamp;
    feature_result->num_features = 0;
    
    /* Copy time features */
    memcpy(&feature_result->features[feature_result->num_features], 
           time_features, sizeof(time_features));
    feature_result->num_features += SEMG_CHANNELS * NUM_TIME_FEATURES;
    
    /* Copy frequency features */
    memcpy(&feature_result->features[feature_result->num_features], 
           freq_features, sizeof(freq_features));
    feature_result->num_features += SEMG_CHANNELS * NUM_FREQ_FEATURES;
    
    /* Copy AR features */
    memcpy(&feature_result->features[feature_result->num_features], 
           ar_features, sizeof(ar_features));
    feature_result->num_features += SEMG_CHANNELS * DSP_AR_ORDER;
    
    /* Copy Wavelet features */
    memcpy(&feature_result->features[feature_result->num_features], 
           wavelet_features, sizeof(wavelet_features));
    feature_result->num_features += SEMG_CHANNELS * DSP_WAVELET_SUBBANDS;
    
    /* Ensure we don't exceed maximum feature vector size */
    if (feature_result->num_features > MAX_FEATURES) {
        feature_result->num_features = MAX_FEATURES;
    }
    
    return HAL_OK;
}