/**
 * @file acquisition_task.c
 * @brief Real-time data acquisition task for ADS1299 sEMG data
 * @note Highest priority task for handling data ready interrupts
 * @author STM32 sEMG System Implementation
 * @date 2025
 */

#include "task_manager.h"
#include "ads1299_driver.h"
#include "dsp_pipeline.h"
#include <string.h>

/* Private Variables */
static semg_window_t current_window AXI_SRAM_SECTION;
static uint32_t sample_buffer[SEMG_CHANNELS][SEMG_WINDOW_SIZE] AXI_SRAM_SECTION;
static uint32_t buffer_index = 0;
static uint32_t window_count = 0;
static uint32_t acquisition_errors = 0;

/* External Variables */
extern QueueHandle_t DataWindowQueue;
extern SemaphoreHandle_t DataReadySemaphore;

/**
 * @brief Data acquisition task - handles ADS1299 data ready interrupts
 * @param pvParameters: Task parameters (unused)
 * 
 * This task runs at the highest priority and is responsible for:
 * 1. Waiting for data ready signals from ADS1299
 * 2. Reading 8-channel sEMG data via SPI
 * 3. Converting 24-bit to 32-bit signed format
 * 4. Building 200-sample windows with 50% overlap
 * 5. Sending complete windows to processing task
 */
void AcquisitionTask(void *pvParameters)
{
    (void)pvParameters;  // Unused parameter
    
    uint32_t status;
    int32_t channel_data[SEMG_CHANNELS];
    uint32_t samples_collected = 0;
    TickType_t last_sample_time = 0;
    
    DEBUG_PRINT("Acquisition Task started\r\n");
    
    // Wait for system initialization
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize current window structure
    memset(&current_window, 0, sizeof(semg_window_t));
    current_window.valid_channels = 0xFF;  // All 8 channels enabled
    
    for (;;) {
        // Wait for data ready notification from interrupt
        if (xSemaphoreTake(DataReadySemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
            
            TickType_t current_time = xTaskGetTickCount();
            
            // Check for timing violations (should be ~1ms between samples at 1kHz)
            if (last_sample_time != 0) {
                TickType_t time_diff = current_time - last_sample_time;
                if (time_diff > pdMS_TO_TICKS(2) || time_diff < pdMS_TO_TICKS(1)) {
                    // Timing violation detected
                    TaskManager_SetErrorFlag(ERROR_FLAG_ADS1299_COMM);
                    acquisition_errors++;
                }
            }
            last_sample_time = current_time;
            
            // Read data from ADS1299
            HAL_StatusTypeDef result = ADS1299_ReadData(&status, channel_data);
            
            if (result != HAL_OK) {
                acquisition_errors++;
                TaskManager_SetErrorFlag(ERROR_FLAG_ADS1299_COMM);
                continue;
            }
            
            // Clear communication error flag if read successful
            TaskManager_ClearErrorFlag(ERROR_FLAG_ADS1299_COMM);
            
            // Convert and store channel data
            for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
                // Convert 24-bit signed to float32 (ADS1299 resolution: 4.5μV/LSB with PGA=6)
                float32_t voltage = (float32_t)channel_data[ch] * 4.5e-6f * 6.0f;  // Convert to volts
                current_window.channels[ch][buffer_index] = voltage;
                sample_buffer[ch][buffer_index] = (uint32_t)channel_data[ch];
            }
            
            buffer_index++;
            samples_collected++;
            
            // Check if we have a complete window (200 samples)
            if (buffer_index >= SEMG_WINDOW_SIZE) {
                
                // Set window metadata
                current_window.timestamp = current_time;
                current_window.sequence_number = window_count++;
                
                // Send window to processing task (non-blocking)
                if (xQueueSend(DataWindowQueue, &current_window, 0) != pdTRUE) {
                    // Queue full - processing task is falling behind
                    TaskManager_SetErrorFlag(ERROR_FLAG_PROCESSING_DEADLINE);
                    acquisition_errors++;
                }
                
                // Implement 50% overlap: shift data by 100 samples
                // Move last 100 samples to beginning of buffer
                for (uint8_t ch = 0; ch < SEMG_CHANNELS; ch++) {
                    memmove(current_window.channels[ch], 
                           &current_window.channels[ch][SEMG_WINDOW_OVERLAP],
                           SEMG_WINDOW_OVERLAP * sizeof(float32_t));
                    
                    memmove(sample_buffer[ch],
                           &sample_buffer[ch][SEMG_WINDOW_OVERLAP],
                           SEMG_WINDOW_OVERLAP * sizeof(uint32_t));
                }
                
                // Reset buffer index for next 100 samples
                buffer_index = SEMG_WINDOW_OVERLAP;
                
                // Cache management for DMA coherency
                CACHE_CLEAN_BY_ADDR(&current_window, sizeof(semg_window_t));
                
                // Update performance statistics
                static uint32_t last_stats_update = 0;
                if (current_time - last_stats_update > pdMS_TO_TICKS(1000)) {
                    // Update statistics every second
                    g_performance_metrics.total_windows_processed = window_count;
                    last_stats_update = current_time;
                }
            }
            
        } else {
            // Timeout waiting for data ready - check system state
            if (g_semg_system_enabled) {
                acquisition_errors++;
                TaskManager_SetErrorFlag(ERROR_FLAG_ADS1299_COMM);
            }
        }
        
        // Monitor stack usage periodically
        #if ENABLE_STACK_MONITORING
        static uint32_t stack_check_counter = 0;
        if (++stack_check_counter >= 1000) {  // Check every 1000 samples
            UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
            if (stack_high_water < 50) {  // Less than 50 words remaining
                TaskManager_SetErrorFlag(ERROR_FLAG_STACK_OVERFLOW);
            }
            stack_check_counter = 0;
        }
        #endif
    }
}

/**
 * @brief Data ready interrupt callback (called from EXTI handler)
 * @note This function runs in interrupt context - keep it minimal
 */
void ADS1299_DataReadyISR(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Release semaphore to notify acquisition task
    xSemaphoreGiveFromISR(DataReadySemaphore, &xHigherPriorityTaskWoken);
    
    // Yield to higher priority task if necessary
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Get acquisition task statistics
 * @param stats: Output statistics structure
 */
void AcquisitionTask_GetStats(struct {
    uint32_t windows_processed;
    uint32_t samples_collected;
    uint32_t acquisition_errors;
    uint32_t buffer_overruns;
} *stats)
{
    if (stats != NULL) {
        taskENTER_CRITICAL();
        stats->windows_processed = window_count;
        stats->samples_collected = samples_collected;
        stats->acquisition_errors = acquisition_errors;
        stats->buffer_overruns = 0;  // Would be tracked if using circular DMA
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief Reset acquisition task counters
 */
void AcquisitionTask_Reset(void)
{
    taskENTER_CRITICAL();
    buffer_index = 0;
    window_count = 0;
    acquisition_errors = 0;
    samples_collected = 0;
    memset(&current_window, 0, sizeof(semg_window_t));
    current_window.valid_channels = 0xFF;
    taskEXIT_CRITICAL();
    
    DEBUG_PRINT("Acquisition Task reset\r\n");
}