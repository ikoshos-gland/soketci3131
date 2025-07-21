#include "task_manager.h"
#include "ads1299_driver.h"
#include "dsp_pipeline.h"
#include "main.h"
#include <stdbool.h>

/* FreeRTOS Task Handles */
TaskHandle_t AcquisitionTaskHandle = NULL;
TaskHandle_t ProcessingTaskHandle = NULL;
TaskHandle_t StreamingTaskHandle = NULL;
TaskHandle_t MonitorTaskHandle = NULL;

/* FreeRTOS Communication Objects */
QueueHandle_t DataWindowQueue = NULL;
QueueHandle_t FeatureResultQueue = NULL;
QueueHandle_t StreamingQueue = NULL;
SemaphoreHandle_t DataReadySemaphore = NULL;
SemaphoreHandle_t ProcessingCompleteSemaphore = NULL;

/* Timer Handle */
TimerHandle_t MonitorTimer = NULL;

/* System Control Variables */
volatile uint32_t g_semg_system_enabled = 0;
volatile uint32_t g_streaming_enabled = 0;
volatile uint32_t g_system_error_flags = 0;

/* External SPI handle from main.c */
extern SPI_HandleTypeDef hspi1;

/**
 * @brief Initialize FreeRTOS task management system
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef TaskManager_Init(void)
{
    /* Create semaphores */
    DataReadySemaphore = xSemaphoreCreateBinary();
    if (DataReadySemaphore == NULL) {
        return HAL_ERROR;
    }

    ProcessingCompleteSemaphore = xSemaphoreCreateBinary();
    if (ProcessingCompleteSemaphore == NULL) {
        return HAL_ERROR;
    }

    /* Create queues */
    DataWindowQueue = xQueueCreate(DATA_WINDOW_QUEUE_SIZE, sizeof(data_window_t));
    if (DataWindowQueue == NULL) {
        return HAL_ERROR;
    }

    FeatureResultQueue = xQueueCreate(FEATURE_RESULT_QUEUE_SIZE, sizeof(feature_vector_t));
    if (FeatureResultQueue == NULL) {
        return HAL_ERROR;
    }

    StreamingQueue = xQueueCreate(STREAMING_QUEUE_SIZE, sizeof(streaming_packet_t));
    if (StreamingQueue == NULL) {
        return HAL_ERROR;
    }

    /* Create tasks */
    if (TaskManager_CreateTasks() != HAL_OK) {
        return HAL_ERROR;
    }

    /* Create monitoring timer */
    MonitorTimer = xTimerCreate("MonitorTimer", 
                               pdMS_TO_TICKS(1000), // 1 second period
                               pdTRUE,               // Auto-reload
                               (void *)0,            // Timer ID
                               MonitorTimerCallback);
    if (MonitorTimer == NULL) {
        return HAL_ERROR;
    }

    /* Start monitoring timer */
    if (xTimerStart(MonitorTimer, 0) != pdPASS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief Create all system tasks with proper priorities
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef TaskManager_CreateTasks(void)
{
    BaseType_t result;

    /* Create Acquisition Task (Highest Priority) */
    result = xTaskCreate(AcquisitionTask,
                        "AcqTask",
                        configMINIMAL_STACK_SIZE * 4,
                        NULL,
                        ACQUISITION_TASK_PRIORITY,
                        &AcquisitionTaskHandle);
    if (result != pdPASS) {
        return HAL_ERROR;
    }

    /* Create Processing Task (High Priority) */
    result = xTaskCreate(ProcessingTask,
                        "ProcTask",
                        configMINIMAL_STACK_SIZE * 8,
                        NULL,
                        PROCESSING_TASK_PRIORITY,
                        &ProcessingTaskHandle);
    if (result != pdPASS) {
        return HAL_ERROR;
    }

    /* Create Streaming Task (Medium Priority) */
    result = xTaskCreate(StreamingTask,
                        "StreamTask",
                        configMINIMAL_STACK_SIZE * 2,
                        NULL,
                        STREAMING_TASK_PRIORITY,
                        &StreamingTaskHandle);
    if (result != pdPASS) {
        return HAL_ERROR;
    }

    /* Create Monitor Task (Low Priority) */
    result = xTaskCreate(MonitorTask,
                        "MonitorTask",
                        configMINIMAL_STACK_SIZE * 2,
                        NULL,
                        MONITOR_TASK_PRIORITY,
                        &MonitorTaskHandle);
    if (result != pdPASS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief Start FreeRTOS scheduler
 * @note This function does not return
 */
void TaskManager_StartScheduler(void)
{
    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler();
    
    /* Should never reach here if scheduler starts successfully */
    while(1) {
        HAL_Delay(1000);
    }
}

/**
 * @brief Data acquisition task - handles ADS1299 data ready interrupts
 * @param pvParameters: Task parameters (unused)
 */
void AcquisitionTask(void *pvParameters)
{
    data_window_t data_window;
    uint32_t notification_value;
    
    for(;;)
    {
        /* Wait for data ready notification or timeout */
        if (xTaskNotifyWait(0x00, ULONG_MAX, &notification_value, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (notification_value & NOTIFICATION_DATA_READY)
            {
                /* Read data from ADS1299 */
                if (ADS1299_ReadDataContinuous(&hspi1, data_window.data, ADS1299_NUM_CHANNELS) == HAL_OK)
                {
                    /* Fill timestamp and metadata */
                    data_window.timestamp = xTaskGetTickCount();
                    data_window.sample_count = SEMG_WINDOW_SIZE;
                    data_window.is_valid = true;
                    
                    /* Send to processing queue */
                    if (xQueueSend(DataWindowQueue, &data_window, 0) != pdTRUE)
                    {
                        TaskManager_SetErrorFlag(ERROR_FLAG_DMA_OVERRUN);
                    }
                    else
                    {
                        /* Notify processing task */
                        xTaskNotify(ProcessingTaskHandle, NOTIFICATION_DATA_READY, eSetBits);
                    }
                }
                else
                {
                    TaskManager_SetErrorFlag(ERROR_FLAG_ADS1299_COMM);
                }
            }
        }
        
        /* Check for system errors */
        if (g_system_error_flags != 0)
        {
            TaskManager_ErrorHandler(g_system_error_flags);
        }
    }
}

/**
 * @brief DSP processing task - executes real-time signal processing
 * @param pvParameters: Task parameters (unused)
 */
void ProcessingTask(void *pvParameters)
{
    data_window_t input_window;
    feature_vector_t feature_result;
    uint32_t notification_value;
    TickType_t processing_start_time;
    
    for(;;)
    {
        /* Wait for processing notification */
        if (xTaskNotifyWait(0x00, ULONG_MAX, &notification_value, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            if (notification_value & NOTIFICATION_DATA_READY)
            {
                /* Receive data window */
                if (xQueueReceive(DataWindowQueue, &input_window, 0) == pdTRUE)
                {
                    processing_start_time = xTaskGetTickCount();
                    
                    /* Execute DSP pipeline */
                    if (DSP_ProcessWindow(&input_window, &feature_result) == HAL_OK)
                    {
                        /* Check processing deadline */
                        TickType_t processing_time = xTaskGetTickCount() - processing_start_time;
                        if (processing_time > pdMS_TO_TICKS(PROCESSING_DEADLINE_MS))
                        {
                            TaskManager_SetErrorFlag(ERROR_FLAG_PROCESSING_DEADLINE);
                        }
                        
                        /* Send result to streaming queue */
                        streaming_packet_t packet;
                        packet.timestamp = feature_result.timestamp;
                        packet.feature_count = feature_result.num_features;
                        memcpy(packet.features, feature_result.features, sizeof(packet.features));
                        
                        if (xQueueSend(StreamingQueue, &packet, 0) == pdTRUE)
                        {
                            /* Notify streaming task */
                            xTaskNotify(StreamingTaskHandle, NOTIFICATION_STREAM_REQUEST, eSetBits);
                        }
                        
                        /* Signal processing complete */
                        xSemaphoreGive(ProcessingCompleteSemaphore);
                    }
                    else
                    {
                        TaskManager_SetErrorFlag(ERROR_FLAG_FFT_ERROR);
                    }
                }
            }
        }
    }
}

/**
 * @brief Streaming task - handles UART/USB communication
 * @param pvParameters: Task parameters (unused)
 */
void StreamingTask(void *pvParameters)
{
    streaming_packet_t stream_packet;
    uint32_t notification_value;
    extern UART_HandleTypeDef huart1;
    
    for(;;)
    {
        /* Wait for streaming notification */
        if (xTaskNotifyWait(0x00, ULONG_MAX, &notification_value, pdMS_TO_TICKS(500)) == pdTRUE)
        {
            if ((notification_value & NOTIFICATION_STREAM_REQUEST) && g_streaming_enabled)
            {
                /* Receive streaming packet */
                while (xQueueReceive(StreamingQueue, &stream_packet, 0) == pdTRUE)
                {
                    /* Transmit via UART */
                    if (HAL_UART_Transmit(&huart1, (uint8_t*)&stream_packet, 
                                        sizeof(streaming_packet_t), 100) != HAL_OK)
                    {
                        TaskManager_SetErrorFlag(ERROR_FLAG_UART_ERROR);
                    }
                }
            }
        }
    }
}

/**
 * @brief System monitoring task - tracks performance and errors
 * @param pvParameters: Task parameters (unused)
 */
void MonitorTask(void *pvParameters)
{
    performance_metrics_t metrics;
    
    for(;;)
    {
        /* Collect system performance metrics */
        metrics.cpu_usage = 100 - (uxTaskGetSystemState(NULL, 0, NULL) * 100 / configTICK_RATE_HZ);
        metrics.free_heap = xPortGetFreeHeapSize();
        metrics.min_free_heap = xPortGetMinimumEverFreeHeapSize();
        metrics.task_switches = uxTaskGetNumberOfTasks();
        metrics.error_flags = g_system_error_flags;
        
        /* Check for critical conditions */
        if (metrics.free_heap < CRITICAL_HEAP_THRESHOLD)
        {
            TaskManager_SetErrorFlag(ERROR_FLAG_MEMORY_ALLOCATION);
        }
        
        if (metrics.cpu_usage > CRITICAL_CPU_THRESHOLD)
        {
            /* Reduce non-critical task priorities if CPU usage is too high */
            vTaskPrioritySet(StreamingTaskHandle, STREAMING_TASK_PRIORITY - 1);
        }
        
        /* Sleep for monitoring interval */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Timer callback for periodic system monitoring
 * @param xTimer: Timer handle
 */
void MonitorTimerCallback(TimerHandle_t xTimer)
{
    /* Notify monitor task to update metrics */
    if (MonitorTaskHandle != NULL)
    {
        xTaskNotifyFromISR(MonitorTaskHandle, NOTIFICATION_SYSTEM_ERROR, eSetBits, NULL);
    }
}

/**
 * @brief System error handler - called when critical errors occur
 * @param error_code: Error code identifier
 */
void TaskManager_ErrorHandler(uint32_t error_code)
{
    /* Disable system temporarily */
    g_semg_system_enabled = 0;
    
    /* Handle specific errors */
    if (error_code & ERROR_FLAG_ADS1299_COMM)
    {
        /* Attempt to reinitialize ADS1299 */
        ADS1299_Init(&hspi1);
        TaskManager_ClearErrorFlag(ERROR_FLAG_ADS1299_COMM);
    }
    
    if (error_code & ERROR_FLAG_STACK_OVERFLOW)
    {
        /* Critical error - reset system */
        NVIC_SystemReset();
    }
    
    if (error_code & ERROR_FLAG_MEMORY_ALLOCATION)
    {
        /* Free non-essential memory */
        TaskManager_ClearErrorFlag(ERROR_FLAG_MEMORY_ALLOCATION);
    }
    
    /* Re-enable system if errors are resolved */
    if (g_system_error_flags == 0)
    {
        g_semg_system_enabled = 1;
    }
}

/**
 * @brief Get system status and performance metrics
 * @param status: Output status structure
 */
void TaskManager_GetSystemStatus(performance_metrics_t *status)
{
    if (status != NULL)
    {
        status->system_enabled = g_semg_system_enabled;
        status->streaming_enabled = g_streaming_enabled;
        status->error_flags = g_system_error_flags;
        status->free_heap = xPortGetFreeHeapSize();
        status->min_free_heap = xPortGetMinimumEverFreeHeapSize();
        status->task_switches = uxTaskGetNumberOfTasks();
        status->uptime_ticks = xTaskGetTickCount();
    }
}

/**
 * @brief Enable/disable sEMG data acquisition
 * @param enable: true to enable, false to disable
 */
void TaskManager_EnableSystem(bool enable)
{
    g_semg_system_enabled = enable ? 1 : 0;
    
    if (enable)
    {
        /* Start data acquisition */
        if (AcquisitionTaskHandle != NULL)
        {
            xTaskNotify(AcquisitionTaskHandle, NOTIFICATION_DATA_READY, eSetBits);
        }
    }
}

/**
 * @brief Enable/disable data streaming
 * @param enable: true to enable, false to disable
 */
void TaskManager_EnableStreaming(bool enable)
{
    g_streaming_enabled = enable ? 1 : 0;
}

/* FreeRTOS Hook Functions - Required by STM32CubeIDE */

/**
 * @brief Application stack overflow hook
 * @param xTask: Task handle
 * @param pcTaskName: Task name
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* Unused parameters */
    (void)xTask;
    (void)pcTaskName;
    
    TaskManager_SetErrorFlag(ERROR_FLAG_STACK_OVERFLOW);
    TaskManager_ErrorHandler(ERROR_FLAG_STACK_OVERFLOW);
}

/**
 * @brief Application malloc failed hook
 */
void vApplicationMallocFailedHook(void)
{
    TaskManager_SetErrorFlag(ERROR_FLAG_MEMORY_ALLOCATION);
    TaskManager_ErrorHandler(ERROR_FLAG_MEMORY_ALLOCATION);
}

/**
 * @brief Application idle hook
 */
void vApplicationIdleHook(void)
{
    /* Called when idle task runs - can be used for power management */
    __WFI(); /* Wait for interrupt to save power */
}

/**
 * @brief Application tick hook
 */
void vApplicationTickHook(void)
{
    /* Called on each FreeRTOS tick - keep minimal */
}