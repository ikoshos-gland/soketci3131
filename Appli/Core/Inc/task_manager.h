#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "semg_config.h"

/* FreeRTOS Task Handles */
extern TaskHandle_t AcquisitionTaskHandle;
extern TaskHandle_t ProcessingTaskHandle;
extern TaskHandle_t StreamingTaskHandle;
extern TaskHandle_t MonitorTaskHandle;

/* FreeRTOS Communication Objects */
extern QueueHandle_t DataWindowQueue;
extern QueueHandle_t FeatureResultQueue;
extern QueueHandle_t StreamingQueue;
extern SemaphoreHandle_t DataReadySemaphore;
extern SemaphoreHandle_t ProcessingCompleteSemaphore;

/* System Control Variables */
extern volatile uint32_t g_semg_system_enabled;
extern volatile uint32_t g_streaming_enabled;
extern volatile uint32_t g_system_error_flags;

/* System Error Flags */
#define ERROR_FLAG_ADS1299_COMM         (1U << 0)
#define ERROR_FLAG_DMA_OVERRUN          (1U << 1)
#define ERROR_FLAG_PROCESSING_DEADLINE  (1U << 2)
#define ERROR_FLAG_STACK_OVERFLOW       (1U << 3)
#define ERROR_FLAG_MEMORY_ALLOCATION    (1U << 4)
#define ERROR_FLAG_FFT_ERROR            (1U << 5)
#define ERROR_FLAG_UART_ERROR           (1U << 6)

/* Task Notification Values */
#define NOTIFICATION_DATA_READY         (1U << 0)
#define NOTIFICATION_PROCESSING_DONE    (1U << 1)
#define NOTIFICATION_STREAM_REQUEST     (1U << 2)
#define NOTIFICATION_SYSTEM_ERROR       (1U << 3)

/* Queue Sizes */
#define DATA_WINDOW_QUEUE_SIZE          2    // Double buffering
#define FEATURE_RESULT_QUEUE_SIZE       4    // Results buffer
#define STREAMING_QUEUE_SIZE            8    // Streaming buffer

/* Function Prototypes */

/**
 * @brief Initialize FreeRTOS task management system
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef TaskManager_Init(void);

/**
 * @brief Create all system tasks with proper priorities
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef TaskManager_CreateTasks(void);

/**
 * @brief Start FreeRTOS scheduler
 * @note This function does not return
 */
void TaskManager_StartScheduler(void);

/**
 * @brief Data acquisition task - handles ADS1299 data ready interrupts
 * @param pvParameters: Task parameters (unused)
 */
void AcquisitionTask(void *pvParameters);

/**
 * @brief DSP processing task - executes real-time signal processing
 * @param pvParameters: Task parameters (unused)
 */
void ProcessingTask(void *pvParameters);

/**
 * @brief Streaming task - handles UART/USB communication
 * @param pvParameters: Task parameters (unused)
 */
void StreamingTask(void *pvParameters);

/**
 * @brief System monitoring task - tracks performance and errors
 * @param pvParameters: Task parameters (unused)
 */
void MonitorTask(void *pvParameters);

/**
 * @brief System error handler - called when critical errors occur
 * @param error_code: Error code identifier
 */
void TaskManager_ErrorHandler(uint32_t error_code);

/**
 * @brief Get system status and performance metrics
 * @param status: Output status structure
 */
void TaskManager_GetSystemStatus(performance_metrics_t *status);

/**
 * @brief Enable/disable sEMG data acquisition
 * @param enable: true to enable, false to disable
 */
void TaskManager_EnableSystem(bool enable);

/**
 * @brief Enable/disable data streaming
 * @param enable: true to enable, false to disable
 */
void TaskManager_EnableStreaming(bool enable);

/**
 * @brief Timer callback for periodic system monitoring
 * @param xTimer: Timer handle
 */
void MonitorTimerCallback(TimerHandle_t xTimer);

/**
 * @brief Application stack overflow hook
 * @param xTask: Task handle
 * @param pcTaskName: Task name
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);

/**
 * @brief Application malloc failed hook
 */
void vApplicationMallocFailedHook(void);

/**
 * @brief Application idle hook
 */
void vApplicationIdleHook(void);

/**
 * @brief Application tick hook
 */
void vApplicationTickHook(void);

/* Inline utility functions */

/**
 * @brief Set system error flag
 * @param error_flag: Error flag to set
 */
static inline void TaskManager_SetErrorFlag(uint32_t error_flag) {
    g_system_error_flags |= error_flag;
}

/**
 * @brief Clear system error flag
 * @param error_flag: Error flag to clear
 */
static inline void TaskManager_ClearErrorFlag(uint32_t error_flag) {
    g_system_error_flags &= ~error_flag;
}

/**
 * @brief Check if system error flag is set
 * @param error_flag: Error flag to check
 * @return bool: true if flag is set
 */
static inline bool TaskManager_IsErrorFlagSet(uint32_t error_flag) {
    return (g_system_error_flags & error_flag) != 0;
}

/**
 * @brief Get current system error flags
 * @return uint32_t: Current error flags
 */
static inline uint32_t TaskManager_GetErrorFlags(void) {
    return g_system_error_flags;
}

#ifdef __cplusplus
}
#endif

#endif /* TASK_MANAGER_H */