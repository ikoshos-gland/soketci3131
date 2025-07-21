/**
 * @file processing_task.c
 * @brief Real-time DSP processing task for sEMG feature extraction
 * @note Implements complete multi-domain feature extraction pipeline
 * @author STM32 sEMG System Implementation
 * @date 2025
 */

#include "task_manager.h"
#include "dsp_pipeline.h"
#include "ads1299_driver.h"
#include <string.h>

/* Private Variables */
static semg_features_t current_features AXI_SRAM_SECTION;
static uint32_t processing_count = 0;
static uint32_t deadline_violations = 0;
static uint32_t processing_errors = 0;

/* Performance Monitoring */
static uint32_t min_processing_cycles = UINT32_MAX;
static uint32_t max_processing_cycles = 0;
static uint64_t total_processing_cycles = 0;

/* External Variables */
extern QueueHandle_t DataWindowQueue;
extern QueueHandle_t FeatureResultQueue;

/**
 * @brief DSP processing task - executes real-time signal processing
 * @param pvParameters: Task parameters (unused)
 * 
 * This task runs at high priority and is responsible for:
 * 1. Receiving sEMG data windows from acquisition task
 * 2. Applying digital filter chain (HP, notch, BP)
 * 3. Extracting multi-domain features (time, freq, AR, wavelet)
 * 4. Ensuring <2ms processing deadline compliance
 * 5. Sending results to streaming task
 */
void ProcessingTask(void *pvParameters)
{
    (void)pvParameters;  // Unused parameter
    
    semg_window_t input_window;
    TickType_t processing_start_time;
    uint32_t processing_cycles;
    
    DEBUG_PRINT("Processing Task started\r\n");
    
    // Wait for DSP pipeline initialization
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Initialize performance monitoring
    g_performance_metrics.processing_cycles_min = UINT32_MAX;
    g_performance_metrics.processing_cycles_max = 0;
    g_performance_metrics.processing_cycles_avg = 0;
    g_performance_metrics.deadline_violations = 0;
    
    for (;;) {
        // Wait for new data window from acquisition task
        if (xQueueReceive(DataWindowQueue, &input_window, portMAX_DELAY) == pdTRUE) {
            
            processing_start_time = xTaskGetTickCount();
            
            // Start cycle-accurate timing measurement
            uint32_t start_cycles = DWT->CYCCNT;
            
            // Process window through complete DSP pipeline
            HAL_StatusTypeDef result = DSP_ProcessWindow(&input_window, &current_features);
            
            // End timing measurement
            uint32_t end_cycles = DWT->CYCCNT;
            processing_cycles = end_cycles - start_cycles;
            
            TickType_t processing_end_time = xTaskGetTickCount();
            TickType_t processing_time_ms = processing_end_time - processing_start_time;
            
            // Update performance statistics
            UpdateProcessingStats(processing_cycles, processing_time_ms);
            
            if (result != HAL_OK) {
                processing_errors++;
                
                if (result == SEMG_ERROR_PROCESSING_DEADLINE) {
                    deadline_violations++;
                    TaskManager_SetErrorFlag(ERROR_FLAG_PROCESSING_DEADLINE);
                    
                    char debug_msg[64];
                    snprintf(debug_msg, sizeof(debug_msg), 
                            "DEADLINE VIOLATION: %lu cycles (%.2f ms)\r\n",
                            processing_cycles, 
                            (float)processing_cycles / SYSTEM_CLOCK_FREQUENCY * 1000.0f);
                    DEBUG_PRINT(debug_msg);
                }
                
                // Continue processing despite errors (graceful degradation)
            } else {
                // Clear processing error flags on successful completion
                TaskManager_ClearErrorFlag(ERROR_FLAG_PROCESSING_DEADLINE);
            }
            
            // Copy timing information to features
            current_features.processing_time_cycles = processing_cycles;
            current_features.timestamp = input_window.timestamp;
            
            processing_count++;
            
            // Send results to streaming task (non-blocking)
            if (g_streaming_enabled) {
                if (xQueueSend(FeatureResultQueue, &current_features, 0) != pdTRUE) {
                    // Streaming queue full - not critical, just drop data
                    static uint32_t dropped_results = 0;
                    dropped_results++;
                }
            }
            
            // Cache management for output data
            CACHE_CLEAN_BY_ADDR(&current_features, sizeof(semg_features_t));
            
            // Periodic performance reporting
            if (processing_count % 100 == 0) {  // Every 100 windows (~10 seconds at 5Hz)
                ReportPerformanceMetrics();
            }
            
        } else {
            // This should not happen with portMAX_DELAY
            processing_errors++;
        }
        
        // Monitor stack usage periodically
        #if ENABLE_STACK_MONITORING
        static uint32_t stack_check_counter = 0;
        if (++stack_check_counter >= 50) {  // Check every 50 windows
            UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
            if (stack_high_water < 100) {  // Less than 100 words remaining
                TaskManager_SetErrorFlag(ERROR_FLAG_STACK_OVERFLOW);
            }
            stack_check_counter = 0;
        }
        #endif
        
        // Yield to allow other tasks to run
        taskYIELD();
    }
}

/**
 * @brief Update processing performance statistics
 * @param cycles: Processing cycles for current window
 * @param time_ms: Processing time in milliseconds
 */
static void UpdateProcessingStats(uint32_t cycles, TickType_t time_ms)
{
    // Update cycle statistics
    if (cycles < min_processing_cycles) {
        min_processing_cycles = cycles;
        g_performance_metrics.processing_cycles_min = cycles;
    }
    
    if (cycles > max_processing_cycles) {
        max_processing_cycles = cycles;
        g_performance_metrics.processing_cycles_max = cycles;
    }
    
    total_processing_cycles += cycles;
    g_performance_metrics.processing_cycles_avg = (uint32_t)(total_processing_cycles / processing_count);
    
    // Update global metrics
    g_performance_metrics.deadline_violations = deadline_violations;
    g_performance_metrics.total_windows_processed = processing_count;
    
    // Calculate CPU utilization (approximate)
    // Assuming 200ms window at 5Hz processing rate = 1 window per 200ms
    float32_t processing_time_ratio = (float32_t)cycles / (SYSTEM_CLOCK_FREQUENCY / 5);  // 5Hz window rate
    g_performance_metrics.cpu_utilization_percent = (uint32_t)(processing_time_ratio * 100.0f);
    
    // Ensure CPU utilization doesn't exceed 100%
    if (g_performance_metrics.cpu_utilization_percent > 100) {
        g_performance_metrics.cpu_utilization_percent = 100;
    }
}

/**
 * @brief Report performance metrics via debug output
 */
static void ReportPerformanceMetrics(void)
{
    #if ENABLE_UART_DEBUG
    char report[256];
    
    float32_t avg_time_ms = (float32_t)g_performance_metrics.processing_cycles_avg / SYSTEM_CLOCK_FREQUENCY * 1000.0f;
    float32_t min_time_ms = (float32_t)g_performance_metrics.processing_cycles_min / SYSTEM_CLOCK_FREQUENCY * 1000.0f;
    float32_t max_time_ms = (float32_t)g_performance_metrics.processing_cycles_max / SYSTEM_CLOCK_FREQUENCY * 1000.0f;
    
    snprintf(report, sizeof(report),
            "PERFORMANCE: Windows=%lu, CPU=%lu%%, Time: avg=%.3fms min=%.3fms max=%.3fms, Violations=%lu\r\n",
            g_performance_metrics.total_windows_processed,
            g_performance_metrics.cpu_utilization_percent,
            avg_time_ms, min_time_ms, max_time_ms,
            g_performance_metrics.deadline_violations);
    
    DEBUG_PRINT(report);
    #endif
}

/**
 * @brief Get processing task statistics
 * @param stats: Output statistics structure
 */
void ProcessingTask_GetStats(struct {
    uint32_t windows_processed;
    uint32_t processing_errors;
    uint32_t deadline_violations;
    uint32_t avg_processing_cycles;
    uint32_t min_processing_cycles;
    uint32_t max_processing_cycles;
} *stats)
{
    if (stats != NULL) {
        taskENTER_CRITICAL();
        stats->windows_processed = processing_count;
        stats->processing_errors = processing_errors;
        stats->deadline_violations = deadline_violations;
        stats->avg_processing_cycles = g_performance_metrics.processing_cycles_avg;
        stats->min_processing_cycles = min_processing_cycles;
        stats->max_processing_cycles = max_processing_cycles;
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief Reset processing task counters
 */
void ProcessingTask_Reset(void)
{
    taskENTER_CRITICAL();
    processing_count = 0;
    deadline_violations = 0;
    processing_errors = 0;
    min_processing_cycles = UINT32_MAX;
    max_processing_cycles = 0;
    total_processing_cycles = 0;
    
    memset(&g_performance_metrics, 0, sizeof(performance_metrics_t));
    g_performance_metrics.processing_cycles_min = UINT32_MAX;
    taskEXIT_CRITICAL();
    
    DEBUG_PRINT("Processing Task reset\r\n");
}

/**
 * @brief Enable/disable feature processing
 * @param enable: true to enable, false to disable
 */
void ProcessingTask_Enable(bool enable)
{
    if (enable) {
        DEBUG_PRINT("Processing Task enabled\r\n");
    } else {
        DEBUG_PRINT("Processing Task disabled\r\n");
    }
    
    // Could implement task suspension/resumption here if needed
}

/**
 * @brief Get current processing load estimate
 * @return float32_t: Processing load as percentage (0-100%)
 */
float32_t ProcessingTask_GetCurrentLoad(void)
{
    if (processing_count == 0) {
        return 0.0f;
    }
    
    return (float32_t)g_performance_metrics.cpu_utilization_percent;
}

/**
 * @brief Check if processing is meeting real-time deadlines
 * @return bool: true if meeting deadlines (>99.9% compliance)
 */
bool ProcessingTask_IsMeetingDeadlines(void)
{
    if (processing_count < 100) {
        return true;  // Not enough data yet
    }
    
    float32_t violation_rate = (float32_t)deadline_violations / processing_count;
    return (violation_rate < 0.001f);  // 99.9% compliance requirement
}