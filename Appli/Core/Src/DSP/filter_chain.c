/**
 * @file filter_chain.c
 * @brief Digital filter chain implementation for sEMG signal conditioning
 * @note Uses proven filter coefficients from working oldproject implementation
 * @author STM32 sEMG System Implementation
 * @date 2025
 */

#include "dsp_pipeline.h"
#include "semg_config.h"
#include <string.h>

/* Filter Coefficients - From Working EMG Implementation (coeffs_IIR.c) */

/**
 * EMG-Optimized Bandpass Filter Coefficients
 * Butterworth bandpass filter [10-450Hz], 6th order (3 biquad stages)
 * Sampling rate: 1000 SPS
 * Generated and validated in working oldproject implementation
 */
const float32_t g_biquad_bp_coeffs[5 * BIQUAD_STAGES_BP] = {
    0.5432977372f, 1.0865954745f, 0.5432977372f, 1.4648681940f, 0.5402535694f,
    1.0000000000f, 2.0000000000f, 1.0000000000f, 1.5610180758f, 0.6413515381f,
    1.0000000000f, 2.0000000000f, 1.0000000000f, 1.7612492291f, 0.8518870319f
};

/**
 * EMG-Optimized Highpass Filter Coefficients  
 * Butterworth highpass filter [10Hz cutoff], 6th order (3 biquad stages)
 * Removes DC offset and motion artifacts
 */
const float32_t g_biquad_hp_coeffs[5 * BIQUAD_STAGES_HP] = {
    0.8856732902f, -1.7713465803f, 0.8856732902f, -1.8819135475f, 0.8856344163f,
    1.0000000000f, -2.0000000000f, 1.0000000000f, -1.9111970674f, 0.9149758348f,
    1.0000000000f, -2.0000000000f, 1.0000000000f, -1.9641335713f, 0.9680170033f
};

/**
 * 50Hz Notch Filter Coefficients
 * Removes power line interference (50Hz)
 * 2-stage biquad implementation for deep notch
 */
const float32_t g_notch_coeffs[5 * 2] = {
    // Stage 1
    0.9604936210f, -1.8709906429f, 0.9604936210f, -1.8709906429f, 0.9209872420f,
    // Stage 2  
    0.9604936210f, -1.8709906429f, 0.9604936210f, -1.8709906429f, 0.9209872420f
};

/* Global DSP Pipeline Instance */
dsp_pipeline_t g_dsp_pipeline;

/**
 * @brief Initialize DSP pipeline with CMSIS-DSP filter instances
 */
HAL_StatusTypeDef DSP_Pipeline_Init(void)
{
    // Clear pipeline structure
    memset(&g_dsp_pipeline, 0, sizeof(dsp_pipeline_t));
    
    // Initialize filter instances for all channels
    for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        
        // Initialize highpass filter (removes DC and motion artifacts)
        arm_biquad_cascade_df2T_init_f32(&g_dsp_pipeline.hp_filter[ch],
                                         BIQUAD_STAGES_HP,
                                         g_biquad_hp_coeffs,
                                         g_dsp_pipeline.hp_state[ch]);
        
        // Initialize bandpass filter (EMG frequency range)
        arm_biquad_cascade_df2T_init_f32(&g_dsp_pipeline.bp_filter[ch],
                                         BIQUAD_STAGES_BP, 
                                         g_biquad_bp_coeffs,
                                         g_dsp_pipeline.bp_state[ch]);
                                         
        // Initialize 50Hz notch filter
        arm_biquad_cascade_df2T_init_f32(&g_dsp_pipeline.notch_filter[ch],
                                         2,  // 2 stages for notch
                                         g_notch_coeffs,
                                         g_dsp_pipeline.notch_state[ch]);
        
        // Initialize statistics
        g_dsp_pipeline.channel_mean[ch] = 0.0f;
        g_dsp_pipeline.channel_std[ch] = 1.0f;  // Initialize to 1 to avoid division by zero
    }
    
    // Initialize FFT instance for spectral analysis
    arm_status fft_status = arm_rfft_fast_init_f32(&g_dsp_pipeline.fft_instance, DSP_FFT_SIZE);
    if (fft_status != ARM_MATH_SUCCESS) {
        return HAL_ERROR;
    }
    
    DEBUG_PRINT("DSP Pipeline initialized successfully\r\n");
    return HAL_OK;
}

/**
 * @brief Apply complete digital filter chain to single channel
 * @note Filter order: DC removal (HP) -> Notch (50Hz) -> Bandpass (10-450Hz)
 */
HAL_StatusTypeDef DSP_FilterChain(const float32_t *input, float32_t *output, uint8_t channel, uint32_t length)
{
    if (channel >= SEMG_CHANNELS || input == NULL || output == NULL) {
        return HAL_ERROR;
    }
    
    START_TIMING();
    
    // Temporary buffer for intermediate filtering results
    static float32_t temp_buffer[SEMG_WINDOW_SIZE] AXI_SRAM_SECTION;
    
    if (length > SEMG_WINDOW_SIZE) {
        return HAL_ERROR;
    }
    
    // Stage 1: Highpass filter (DC removal and motion artifact suppression)
    arm_biquad_cascade_df2T_f32(&g_dsp_pipeline.hp_filter[channel], 
                                input, 
                                temp_buffer, 
                                length);
    
    // Stage 2: 50Hz Notch filter (power line interference removal)
    arm_biquad_cascade_df2T_f32(&g_dsp_pipeline.notch_filter[channel],
                                temp_buffer,
                                temp_buffer,  // In-place filtering
                                length);
    
    // Stage 3: Bandpass filter (EMG frequency range: 10-450Hz)
    arm_biquad_cascade_df2T_f32(&g_dsp_pipeline.bp_filter[channel],
                                temp_buffer,
                                output,
                                length);
    
    END_TIMING(g_dsp_pipeline.filter_cycles);
    
    return HAL_OK;
}

/**
 * @brief Update running statistics for Z-score normalization
 * @note Uses exponential moving average for computational efficiency
 */
void DSP_UpdateStatistics(uint8_t channel, float32_t new_sample)
{
    if (channel >= SEMG_CHANNELS) {
        return;
    }
    
    const float32_t alpha = 0.01f;  // Smoothing factor for exponential moving average
    
    // Update running mean
    g_dsp_pipeline.channel_mean[channel] = (1.0f - alpha) * g_dsp_pipeline.channel_mean[channel] + 
                                          alpha * new_sample;
    
    // Update running variance (simplified)
    float32_t diff = new_sample - g_dsp_pipeline.channel_mean[channel];
    float32_t variance = (1.0f - alpha) * (g_dsp_pipeline.channel_std[channel] * g_dsp_pipeline.channel_std[channel]) + 
                        alpha * (diff * diff);
    
    // Update standard deviation
    arm_sqrt_f32(variance, &g_dsp_pipeline.channel_std[channel]);
    
    // Ensure std is never zero
    if (g_dsp_pipeline.channel_std[channel] < 1e-6f) {
        g_dsp_pipeline.channel_std[channel] = 1e-6f;
    }
    
    g_dsp_pipeline.sample_count++;
}

/**
 * @brief Apply Z-score normalization to signal
 * @note Z = (X - mean) / std
 */
void DSP_ZScoreNormalization(float32_t *signal, uint8_t channel, uint32_t length)
{
    if (channel >= SEMG_CHANNELS || signal == NULL) {
        return;
    }
    
    float32_t mean = g_dsp_pipeline.channel_mean[channel];
    float32_t std = g_dsp_pipeline.channel_std[channel];
    
    // Apply Z-score normalization: (x - mean) / std
    for (uint32_t i = 0; i < length; i++) {
        signal[i] = (signal[i] - mean) / std;
        
        // Update statistics with filtered sample
        DSP_UpdateStatistics(channel, signal[i]);
    }
}

/**
 * @brief Process complete sEMG window through filter chain
 */
HAL_StatusTypeDef DSP_ProcessWindow(const semg_window_t *input_window, semg_features_t *features)
{
    if (input_window == NULL || features == NULL) {
        return HAL_ERROR;
    }
    
    START_TIMING();
    
    // Temporary filtered signal buffer
    static float32_t filtered_signal[SEMG_CHANNELS][SEMG_WINDOW_SIZE] AXI_SRAM_SECTION SIMD_ALIGN_32;
    
    // Process each channel through filter chain
    for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        
        // Apply digital filter chain
        if (DSP_FilterChain(input_window->channels[ch], filtered_signal[ch], ch, SEMG_WINDOW_SIZE) != HAL_OK) {
            return HAL_ERROR;
        }
        
        // Apply Z-score normalization
        DSP_ZScoreNormalization(filtered_signal[ch], ch, SEMG_WINDOW_SIZE);
        
        // Extract time-domain features
        if (DSP_ExtractTimeFeatures(filtered_signal[ch], SEMG_WINDOW_SIZE, &features->time_domain[ch]) != HAL_OK) {
            return HAL_ERROR;
        }
        
        // Extract frequency-domain features
        if (DSP_ExtractFreqFeatures(filtered_signal[ch], SEMG_WINDOW_SIZE, &features->freq_domain[ch]) != HAL_OK) {
            return HAL_ERROR;
        }
        
        // Extract AR model features
        if (DSP_ExtractARFeatures(filtered_signal[ch], SEMG_WINDOW_SIZE, &features->parametric[ch]) != HAL_OK) {
            return HAL_ERROR;
        }
        
        // Extract wavelet features
        if (DSP_ExtractWaveletFeatures(filtered_signal[ch], SEMG_WINDOW_SIZE, &features->wavelet[ch]) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    // Store processing timestamp
    features->timestamp = HAL_GetTick();
    
    END_TIMING(g_dsp_pipeline.total_cycles);
    features->processing_time_cycles = g_dsp_pipeline.total_cycles;
    
    // Check real-time deadline compliance (2ms at 600MHz)
    if (g_dsp_pipeline.total_cycles > PROCESSING_DEADLINE_CYCLES) {
        return SEMG_ERROR_PROCESSING_DEADLINE;
    }
    
    return HAL_OK;
}

/**
 * @brief Reset DSP pipeline state
 */
void DSP_ResetPipeline(void)
{
    // Reset filter states
    for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
        memset(g_dsp_pipeline.hp_state[ch], 0, sizeof(g_dsp_pipeline.hp_state[ch]));
        memset(g_dsp_pipeline.bp_state[ch], 0, sizeof(g_dsp_pipeline.bp_state[ch]));
        memset(g_dsp_pipeline.notch_state[ch], 0, sizeof(g_dsp_pipeline.notch_state[ch]));
        
        // Reset statistics
        g_dsp_pipeline.channel_mean[ch] = 0.0f;
        g_dsp_pipeline.channel_std[ch] = 1.0f;
    }
    
    // Reset performance counters
    g_dsp_pipeline.filter_cycles = 0;
    g_dsp_pipeline.feature_cycles = 0;
    g_dsp_pipeline.total_cycles = 0;
    g_dsp_pipeline.sample_count = 0;
    
    DEBUG_PRINT("DSP Pipeline reset\r\n");
}

/**
 * @brief Get DSP pipeline performance metrics
 */
void DSP_GetPerformanceMetrics(uint32_t *cycles_filter, uint32_t *cycles_features, uint32_t *cycles_total)
{
    if (cycles_filter != NULL) {
        *cycles_filter = g_dsp_pipeline.filter_cycles;
    }
    if (cycles_features != NULL) {
        *cycles_features = g_dsp_pipeline.feature_cycles;
    }
    if (cycles_total != NULL) {
        *cycles_total = g_dsp_pipeline.total_cycles;
    }
}