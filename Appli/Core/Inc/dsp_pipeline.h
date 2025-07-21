#ifndef DSP_PIPELINE_H
#define DSP_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"
#include "arm_math.h"
#include "semg_config.h"

/* DSP Pipeline Configuration */
#define DSP_NOTCH_FREQUENCY              50.0f    // 50Hz notch filter for power line interference
#define DSP_HIGHPASS_FREQUENCY           10.0f    // 10Hz highpass for DC and motion artifacts
#define DSP_LOWPASS_FREQUENCY            450.0f   // 450Hz lowpass for EMG bandwidth
#define DSP_SAMPLING_FREQUENCY           1000.0f  // 1kHz sampling rate

/* FFT Configuration */
#define DSP_FFT_SIZE                     256
#define DSP_FFT_SIZE_LOG2                8        // log2(256) = 8

/* AR Model Configuration */
#define DSP_AR_ORDER                     4        // AR(4) model order

/* Wavelet Configuration */
#define DSP_WAVELET_LEVELS               3        // 3-level decomposition
#define DSP_WAVELET_SUBBANDS             4        // 4 frequency subbands

/* Feature Extraction Thresholds */
#define DSP_ZC_THRESHOLD                 0.001f   // Zero crossing threshold (V)
#define DSP_SSC_THRESHOLD                0.001f   // Slope sign change threshold (V)

/* Filter Coefficient Arrays - From Working Implementation */
extern const float32_t g_biquad_hp_coeffs[5 * BIQUAD_STAGES_HP];
extern const float32_t g_biquad_bp_coeffs[5 * BIQUAD_STAGES_BP];
extern const float32_t g_notch_coeffs[5 * 2];  // 2-stage notch filter

/* Global DSP Instance Structures - Memory Optimized */
typedef struct {
    /* Filter Instances */
    arm_biquad_cascade_df2T_instance_f32 hp_filter[SEMG_CHANNELS];
    arm_biquad_cascade_df2T_instance_f32 bp_filter[SEMG_CHANNELS];
    arm_biquad_cascade_df2T_instance_f32 notch_filter[SEMG_CHANNELS];
    
    /* Filter States */
    float32_t hp_state[SEMG_CHANNELS][2 * BIQUAD_STAGES_HP] SRAM1_SECTION;
    float32_t bp_state[SEMG_CHANNELS][2 * BIQUAD_STAGES_BP] SRAM1_SECTION;
    float32_t notch_state[SEMG_CHANNELS][2 * 2] SRAM1_SECTION;
    
    /* FFT Instance */
    arm_rfft_fast_instance_f32 fft_instance;
    
    /* Working Buffers */
    float32_t fft_input[DSP_FFT_SIZE] AXI_SRAM_SECTION SIMD_ALIGN_32;
    float32_t fft_output[DSP_FFT_SIZE] AXI_SRAM_SECTION SIMD_ALIGN_32;
    float32_t fft_magnitude[DSP_FFT_SIZE/2] AXI_SRAM_SECTION SIMD_ALIGN_32;
    
    /* Statistics Tracking */
    float32_t channel_mean[SEMG_CHANNELS];
    float32_t channel_std[SEMG_CHANNELS];
    uint32_t sample_count;
    
    /* Performance Monitoring */
    uint32_t filter_cycles;
    uint32_t feature_cycles;
    uint32_t total_cycles;
    
} dsp_pipeline_t;

extern dsp_pipeline_t g_dsp_pipeline;

/* Function Prototypes */

/**
 * @brief Initialize DSP pipeline with CMSIS-DSP instances
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_Pipeline_Init(void);

/**
 * @brief Process complete sEMG window through DSP pipeline
 * @param input_window: Raw sEMG data window (8 channels x 200 samples)
 * @param features: Output feature structure
 * @return HAL_StatusTypeDef: HAL_OK if processing successful
 */
HAL_StatusTypeDef DSP_ProcessWindow(const semg_window_t *input_window, semg_features_t *features);

/**
 * @brief Apply digital filter chain to single channel
 * @param input: Input data array
 * @param output: Filtered output array
 * @param channel: Channel index (0-7)
 * @param length: Data length
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_FilterChain(const float32_t *input, float32_t *output, uint8_t channel, uint32_t length);

/**
 * @brief Extract time-domain features from filtered signal
 * @param signal: Input filtered signal
 * @param length: Signal length
 * @param features: Output time-domain features
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ExtractTimeFeatures(const float32_t *signal, uint32_t length, time_features_t *features);

/**
 * @brief Extract frequency-domain features using FFT
 * @param signal: Input filtered signal
 * @param length: Signal length (must be <= DSP_FFT_SIZE)
 * @param features: Output frequency-domain features
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ExtractFreqFeatures(const float32_t *signal, uint32_t length, freq_features_t *features);

/**
 * @brief Extract autoregressive model parameters
 * @param signal: Input filtered signal
 * @param length: Signal length
 * @param features: Output AR features
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ExtractARFeatures(const float32_t *signal, uint32_t length, ar_features_t *features);

/**
 * @brief Extract wavelet packet features
 * @param signal: Input filtered signal
 * @param length: Signal length
 * @param features: Output wavelet features
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_ExtractWaveletFeatures(const float32_t *signal, uint32_t length, wavelet_features_t *features);

/**
 * @brief Update running statistics for Z-score normalization
 * @param channel: Channel index
 * @param new_sample: New sample value
 */
void DSP_UpdateStatistics(uint8_t channel, float32_t new_sample);

/**
 * @brief Apply Z-score normalization to signal
 * @param signal: Input/output signal array
 * @param channel: Channel index for statistics
 * @param length: Signal length
 */
void DSP_ZScoreNormalization(float32_t *signal, uint8_t channel, uint32_t length);

/**
 * @brief Calculate RMS value using CMSIS-DSP
 * @param signal: Input signal
 * @param length: Signal length
 * @param rms: Output RMS value
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
static inline HAL_StatusTypeDef DSP_CalculateRMS(const float32_t *signal, uint32_t length, float32_t *rms) {
    arm_rms_f32(signal, length, rms);
    return HAL_OK;
}

/**
 * @brief Calculate Mean Absolute Value
 * @param signal: Input signal
 * @param length: Signal length
 * @param mav: Output MAV value
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_CalculateMAV(const float32_t *signal, uint32_t length, float32_t *mav);

/**
 * @brief Calculate Waveform Length
 * @param signal: Input signal
 * @param length: Signal length
 * @param wl: Output WL value
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_CalculateWL(const float32_t *signal, uint32_t length, float32_t *wl);

/**
 * @brief Count Zero Crossings
 * @param signal: Input signal
 * @param length: Signal length
 * @param threshold: Zero crossing threshold
 * @param zc: Output zero crossing count
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_CountZeroCrossings(const float32_t *signal, uint32_t length, float32_t threshold, uint16_t *zc);

/**
 * @brief Count Slope Sign Changes
 * @param signal: Input signal
 * @param length: Signal length
 * @param threshold: SSC threshold
 * @param ssc: Output slope sign change count
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_CountSlopeSignChanges(const float32_t *signal, uint32_t length, float32_t threshold, uint16_t *ssc);

/**
 * @brief Calculate power spectral density from FFT magnitude
 * @param fft_magnitude: Input FFT magnitude array
 * @param length: FFT magnitude length (FFT_SIZE/2)
 * @param psd: Output power spectral density array
 */
void DSP_CalculatePSD(const float32_t *fft_magnitude, uint32_t length, float32_t *psd);

/**
 * @brief Calculate median frequency from power spectrum
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param mdf: Output median frequency
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_CalculateMedianFreq(const float32_t *psd, uint32_t length, float32_t sampling_freq, float32_t *mdf);

/**
 * @brief Calculate mean frequency from power spectrum
 * @param psd: Power spectral density array
 * @param length: PSD length
 * @param sampling_freq: Sampling frequency
 * @param mnf: Output mean frequency
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_CalculateMeanFreq(const float32_t *psd, uint32_t length, float32_t sampling_freq, float32_t *mnf);

/**
 * @brief Get DSP pipeline performance metrics
 * @param cycles_filter: Output filter processing cycles
 * @param cycles_features: Output feature extraction cycles
 * @param cycles_total: Output total processing cycles
 */
void DSP_GetPerformanceMetrics(uint32_t *cycles_filter, uint32_t *cycles_features, uint32_t *cycles_total);

/**
 * @brief Reset DSP pipeline state (filters, statistics)
 */
void DSP_ResetPipeline(void);

/**
 * @brief Quantize features to Q7 fixed-point format
 * @param features: Input floating-point features
 * @param q7_features: Output Q7 quantized features
 * @param num_features: Number of features to quantize
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef DSP_QuantizeToQ7(const float32_t *features, q7_t *q7_features, uint32_t num_features);

#ifdef __cplusplus
}
#endif

#endif /* DSP_PIPELINE_H */