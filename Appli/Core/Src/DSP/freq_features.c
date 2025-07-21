/**
 * @file freq_features.c
 * @brief Frequency-domain feature extraction using CMSIS-DSP FFT
 * @note Optimized for STM32H7RS ARM Cortex-M7 with FPU
 * @author STM32 sEMG System Implementation
 * @date 2025
 */

#include "dsp_pipeline.h"
#include "semg_config.h"
#include <string.h>

/**
 * @brief Extract frequency-domain features using FFT analysis
 * @param signal: Input filtered sEMG signal
 * @param length: Signal length (will be zero-padded or truncated to FFT_SIZE)
 * @param features: Output frequency-domain features
 */
HAL_StatusTypeDef DSP_ExtractFreqFeatures(const float32_t *signal, uint32_t length, freq_features_t *features)
{
    if (signal == NULL || features == NULL) {
        return HAL_ERROR;
    }
    
    START_TIMING();
    
    // Prepare FFT input buffer (zero-pad or truncate to FFT_SIZE)
    memset(g_dsp_pipeline.fft_input, 0, sizeof(g_dsp_pipeline.fft_input));
    
    uint32_t copy_length = (length < DSP_FFT_SIZE) ? length : DSP_FFT_SIZE;
    memcpy(g_dsp_pipeline.fft_input, signal, copy_length * sizeof(float32_t));
    
    // Apply Hanning window to reduce spectral leakage
    DSP_ApplyHanningWindow(g_dsp_pipeline.fft_input, DSP_FFT_SIZE);
    
    // Perform FFT using CMSIS-DSP optimized function
    arm_rfft_fast_f32(&g_dsp_pipeline.fft_instance, 
                      g_dsp_pipeline.fft_input, 
                      g_dsp_pipeline.fft_output);
    
    // Calculate magnitude spectrum
    arm_cmplx_mag_f32(g_dsp_pipeline.fft_output, 
                      g_dsp_pipeline.fft_magnitude, 
                      DSP_FFT_SIZE / 2);
    
    // Calculate power spectral density
    static float32_t psd[DSP_FFT_SIZE / 2] AXI_SRAM_SECTION;
    DSP_CalculatePSD(g_dsp_pipeline.fft_magnitude, DSP_FFT_SIZE / 2, psd);
    
    // Calculate total power
    arm_power_f32(g_dsp_pipeline.fft_magnitude, DSP_FFT_SIZE / 2, &features->total_power);
    
    // Calculate Median Frequency (MDF)
    if (DSP_CalculateMedianFreq(psd, DSP_FFT_SIZE / 2, DSP_SAMPLING_FREQUENCY, &features->mdf) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Calculate Mean Frequency (MNF)
    if (DSP_CalculateMeanFreq(psd, DSP_FFT_SIZE / 2, DSP_SAMPLING_FREQUENCY, &features->mnf) != HAL_OK) {
        return HAL_ERROR;
    }
    
    END_TIMING(g_dsp_pipeline.feature_cycles);
    
    return HAL_OK;
}

/**
 * @brief Apply Hanning window to reduce spectral leakage
 * @param signal: Input/output signal array
 * @param length: Signal length
 */
void DSP_ApplyHanningWindow(float32_t *signal, uint32_t length)
{
    if (signal == NULL || length == 0) {
        return;
    }
    
    for (uint32_t i = 0; i < length; i++) {
        float32_t window_value = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (length - 1)));
        signal[i] *= window_value;
    }
}

/**
 * @brief Calculate power spectral density from FFT magnitude
 * @param fft_magnitude: Input FFT magnitude array
 * @param length: FFT magnitude length (FFT_SIZE/2)
 * @param psd: Output power spectral density array
 */
void DSP_CalculatePSD(const float32_t *fft_magnitude, uint32_t length, float32_t *psd)
{
    if (fft_magnitude == NULL || psd == NULL) {
        return;
    }
    
    // PSD = |X(k)|^2 / (Fs * N)
    float32_t scale_factor = 1.0f / (DSP_SAMPLING_FREQUENCY * DSP_FFT_SIZE);
    
    for (uint32_t i = 0; i < length; i++) {
        psd[i] = (fft_magnitude[i] * fft_magnitude[i]) * scale_factor;
    }
    
    // Account for two-sided spectrum (multiply by 2, except DC and Nyquist)
    for (uint32_t i = 1; i < length - 1; i++) {
        psd[i] *= 2.0f;
    }
}

/**
 * @brief Calculate median frequency from power spectrum
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param mdf: Output median frequency
 */
HAL_StatusTypeDef DSP_CalculateMedianFreq(const float32_t *psd, uint32_t length, float32_t sampling_freq, float32_t *mdf)
{
    if (psd == NULL || mdf == NULL || length == 0) {
        return HAL_ERROR;
    }
    
    // Calculate total power
    float32_t total_power = 0.0f;
    arm_power_f32(psd, length, &total_power);
    
    if (total_power <= 0.0f) {
        *mdf = 0.0f;
        return HAL_OK;
    }
    
    // Find frequency where cumulative power reaches 50% of total
    float32_t cumulative_power = 0.0f;
    float32_t half_power = total_power * 0.5f;
    float32_t freq_resolution = sampling_freq / (2.0f * length);
    
    for (uint32_t i = 0; i < length; i++) {
        cumulative_power += psd[i];
        
        if (cumulative_power >= half_power) {
            *mdf = i * freq_resolution;
            return HAL_OK;
        }
    }
    
    // If we reach here, set MDF to Nyquist frequency
    *mdf = sampling_freq / 2.0f;
    return HAL_OK;
}

/**
 * @brief Calculate mean frequency from power spectrum
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param mnf: Output mean frequency
 */
HAL_StatusTypeDef DSP_CalculateMeanFreq(const float32_t *psd, uint32_t length, float32_t sampling_freq, float32_t *mnf)
{
    if (psd == NULL || mnf == NULL || length == 0) {
        return HAL_ERROR;
    }
    
    float32_t freq_resolution = sampling_freq / (2.0f * length);
    float32_t weighted_sum = 0.0f;
    float32_t total_power = 0.0f;
    
    // Calculate weighted sum: Σ(f * P(f)) and total power: Σ(P(f))
    for (uint32_t i = 0; i < length; i++) {
        float32_t frequency = i * freq_resolution;
        weighted_sum += frequency * psd[i];
        total_power += psd[i];
    }
    
    if (total_power <= 0.0f) {
        *mnf = 0.0f;
        return HAL_OK;
    }
    
    // Mean frequency = Σ(f * P(f)) / Σ(P(f))
    *mnf = weighted_sum / total_power;
    
    return HAL_OK;
}

/**
 * @brief Calculate frequency band power
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param low_freq: Lower frequency bound (Hz)
 * @param high_freq: Upper frequency bound (Hz)
 * @param band_power: Output band power
 */
HAL_StatusTypeDef DSP_CalculateBandPower(const float32_t *psd, uint32_t length, float32_t sampling_freq,
                                        float32_t low_freq, float32_t high_freq, float32_t *band_power)
{
    if (psd == NULL || band_power == NULL || length == 0) {
        return HAL_ERROR;
    }
    
    float32_t freq_resolution = sampling_freq / (2.0f * length);
    uint32_t low_bin = (uint32_t)(low_freq / freq_resolution);
    uint32_t high_bin = (uint32_t)(high_freq / freq_resolution);
    
    // Ensure bins are within valid range
    if (low_bin >= length) low_bin = length - 1;
    if (high_bin >= length) high_bin = length - 1;
    if (low_bin > high_bin) {
        uint32_t temp = low_bin;
        low_bin = high_bin;
        high_bin = temp;
    }
    
    *band_power = 0.0f;
    
    // Sum power in frequency band
    for (uint32_t i = low_bin; i <= high_bin; i++) {
        *band_power += psd[i];
    }
    
    return HAL_OK;
}

/**
 * @brief Calculate spectral centroid (center of mass of spectrum)
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param centroid: Output spectral centroid frequency
 */
HAL_StatusTypeDef DSP_CalculateSpectralCentroid(const float32_t *psd, uint32_t length, float32_t sampling_freq, float32_t *centroid)
{
    return DSP_CalculateMeanFreq(psd, length, sampling_freq, centroid);  // Same calculation
}

/**
 * @brief Calculate spectral spread (variance around centroid)
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param centroid: Spectral centroid frequency
 * @param spread: Output spectral spread
 */
HAL_StatusTypeDef DSP_CalculateSpectralSpread(const float32_t *psd, uint32_t length, float32_t sampling_freq,
                                             float32_t centroid, float32_t *spread)
{
    if (psd == NULL || spread == NULL || length == 0) {
        return HAL_ERROR;
    }
    
    float32_t freq_resolution = sampling_freq / (2.0f * length);
    float32_t weighted_variance = 0.0f;
    float32_t total_power = 0.0f;
    
    // Calculate weighted variance: Σ((f - centroid)^2 * P(f))
    for (uint32_t i = 0; i < length; i++) {
        float32_t frequency = i * freq_resolution;
        float32_t freq_diff = frequency - centroid;
        weighted_variance += (freq_diff * freq_diff) * psd[i];
        total_power += psd[i];
    }
    
    if (total_power <= 0.0f) {
        *spread = 0.0f;
        return HAL_OK;
    }
    
    // Spectral spread = sqrt(Σ((f - centroid)^2 * P(f)) / Σ(P(f)))
    arm_sqrt_f32(weighted_variance / total_power, spread);
    
    return HAL_OK;
}