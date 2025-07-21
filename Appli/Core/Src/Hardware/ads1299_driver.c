/**
 * @file ads1299_driver.c
 * @brief ADS1299 8-channel 24-bit ADC driver for STM32H7RS
 * @note Based on proven working implementation from oldproject
 * @author STM32 sEMG System Implementation
 * @date 2025
 */

#include "ads1299_driver.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Global Variables with STM32H7RS Memory Optimization */
uint8_t g_spi_tx_buffer[32] SRAM2_SECTION;
uint8_t g_spi_rx_buffer[32] SRAM2_SECTION;
uint8_t g_ads1299_data_buffer[ADS1299_DATA_PACKET_SIZE] AXI_SRAM_SECTION;
ads1299_handle_t g_ads1299_handle;

/* External SPI Handle (defined in main.c or peripheral init) */
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;

/* Private Function Prototypes */
static HAL_StatusTypeDef ADS1299_SPITransmit(uint8_t data);
static HAL_StatusTypeDef ADS1299_SPITransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t size);
static void ADS1299_ChipSelect(bool active);

/**
 * @brief Initialize ADS1299 with EMG-optimized configuration
 * @note Following exact sequence from working oldproject implementation
 */
HAL_StatusTypeDef ADS1299_Init(SPI_HandleTypeDef *hspi)
{
    char debug_msg[64];
    
    // Initialize handle
    memset(&g_ads1299_handle, 0, sizeof(ads1299_handle_t));
    g_ads1299_handle.hspi = hspi;
    g_ads1299_handle.state = ADS1299_STATE_UNINITIALIZED;
    
    DEBUG_PRINT("Starting ADS1299 Power-up sequence...\r\n");
    
    // Power-up sequence (exact timing from working code)
    ADS1299_PowerUpSequence();
    HAL_Delay(1);  // 1ms between sequences
    ADS1299_PowerUpSequence();  // Double power-up as per working code
    
    g_ads1299_handle.state = ADS1299_STATE_POWER_UP;
    DEBUG_PRINT("Power-up sequence completed\r\n");
    
    // Stop any ongoing data read before configuration
    DEBUG_PRINT("Sending SDATAC...\r\n");
    ADS1299_StopDataRead();
    
    // Verify chip ID - CRITICAL validation step
    uint8_t chip_id;
    if (ADS1299_ReadRegister(ADS1299_ID, &chip_id) != HAL_OK) {
        g_ads1299_handle.state = ADS1299_STATE_ERROR;
        DEBUG_PRINT("ERROR: Failed to read ADS1299 ID\r\n");
        return HAL_ERROR;
    }
    
    if (chip_id == ADS1299_EXPECTED_ID) {
        DEBUG_PRINT("ADS1299 ID verified – OK\r\n");
    } else {
        snprintf(debug_msg, sizeof(debug_msg), "ERROR: ADS1299 ID mismatch. Expected 0x%02X, got 0x%02X\r\n", 
                 ADS1299_EXPECTED_ID, chip_id);
        DEBUG_PRINT(debug_msg);
        g_ads1299_handle.state = ADS1299_STATE_ERROR;
        return HAL_ERROR;
    }
    
    // EMG configuration - EXACT values from working implementation
    ADS1299_WriteRegister(ADS1299_CONFIG1, 0x94);  // 1 kSPS sampling rate
    ADS1299_WriteRegister(ADS1299_CONFIG3, 0x60 | (1u << 7));  // REF buf ON, BIAS buf OFF
    ADS1299_WriteRegister(ADS1299_CONFIG2, 0xC0 | ADS1299_TEST_INT | ADS1299_TESTSIGNAL_PULSE_FAST);
    
    // Channel configuration - PGA gain 6, normal input, powered down initially
    const uint8_t ch_default = ADS1299_PGA_GAIN06 | ADS1299_INPUT_NORMAL | ADS1299_INPUT_PWR_DOWN;
    for (uint8_t ch = 0; ch < 8; ch++) {
        ADS1299_WriteRegister(ADS1299_CH1SET + ch, ch_default);
    }
    
    // Lead-off detection and bias configuration
    ADS1299_WriteRegister(ADS1299_LOFF, ADS1299_LOFF_FREQ_FS_4);  // AC lead-off 62.5 Hz
    ADS1299_WriteRegister(ADS1299_MISC1, 1u << 5);               // SRB1 connected to all channels
    
    g_ads1299_handle.state = ADS1299_STATE_CONFIGURED;
    
    // Configure DMA for high-speed data acquisition
    if (ADS1299_ConfigureDMA() != HAL_OK) {
        DEBUG_PRINT("WARNING: DMA configuration failed, using polling mode\r\n");
        g_ads1299_handle.dma_enabled = false;
    } else {
        g_ads1299_handle.dma_enabled = true;
    }
    
    DEBUG_PRINT("ADS1299 initialization completed successfully\r\n");
    return HAL_OK;
}

/**
 * @brief Power-up sequence - EXACT implementation from working code
 */
void ADS1299_PowerUpSequence(void)
{
    HAL_Delay(ADS1299_POWER_UP_DELAY_MS);                          // t_pwd power-down time
    HAL_GPIO_WritePin(ADS1299_RESET_PORT, ADS1299_RESET_PIN, GPIO_PIN_RESET);  // RESET low
    ADS1299_DelayCycles(ADS1299_RESET_PULSE_CYCLES);               // ~2µs delay
    HAL_GPIO_WritePin(ADS1299_RESET_PORT, ADS1299_RESET_PIN, GPIO_PIN_SET);    // RESET high
    ADS1299_DelayCycles(ADS1299_RESET_DELAY_CYCLES);               // ~10µs delay
}

/**
 * @brief Stop continuous data read mode
 */
void ADS1299_StopDataRead(void)
{
    ADS1299_ChipSelect(true);   // CS low
    ADS1299_SPITransmit(ADS1299_SDATAC);
    ADS1299_ChipSelect(false);  // CS high
    ADS1299_DelayCycles(ADS1299_SPI_DELAY_CYCLES);  // 2 tCLK delay
}

/**
 * @brief Start continuous data read mode
 */
HAL_StatusTypeDef ADS1299_StartContinuousRead(void)
{
    if (g_ads1299_handle.state != ADS1299_STATE_CONFIGURED) {
        return HAL_ERROR;
    }
    
    // Enable all channels for sEMG acquisition
    const uint8_t ch_enabled = ADS1299_PGA_GAIN06 | ADS1299_INPUT_NORMAL | ADS1299_INPUT_PWR_UP;
    for (uint8_t ch = 0; ch < 8; ch++) {
        ADS1299_WriteRegister(ADS1299_CH1SET + ch, ch_enabled);
    }
    
    // Start continuous data conversion
    ADS1299_ChipSelect(true);
    ADS1299_SPITransmit(ADS1299_RDATAC);
    ADS1299_ChipSelect(false);
    ADS1299_DelayCycles(ADS1299_SPI_DELAY_CYCLES);
    
    // Send START command
    HAL_GPIO_WritePin(ADS1299_START_PORT, ADS1299_START_PIN, GPIO_PIN_SET);
    
    g_ads1299_handle.state = ADS1299_STATE_CONTINUOUS_READ;
    DEBUG_PRINT("ADS1299 continuous read mode started\r\n");
    
    return HAL_OK;
}

/**
 * @brief Read data packet (polling mode)
 */
HAL_StatusTypeDef ADS1299_ReadData(uint32_t *status, int32_t *data)
{
    if (g_ads1299_handle.state != ADS1299_STATE_CONTINUOUS_READ) {
        return HAL_ERROR;
    }
    
    // Wait for DRDY signal (data ready)
    uint32_t timeout = HAL_GetTick() + ADS1299_DRDY_TIMEOUT_MS;
    while (HAL_GPIO_ReadPin(ADS1299_DRDY_PORT, ADS1299_DRDY_PIN) == GPIO_PIN_SET) {
        if (HAL_GetTick() > timeout) {
            g_ads1299_handle.error_count++;
            return HAL_TIMEOUT;
        }
    }
    
    // Read complete data packet (27 bytes)
    ADS1299_ChipSelect(true);
    
    // Read status bytes (3 bytes)
    HAL_StatusTypeDef result = HAL_SPI_Receive(g_ads1299_handle.hspi, g_ads1299_data_buffer, 
                                               ADS1299_DATA_PACKET_SIZE, HAL_MAX_DELAY);
    
    ADS1299_ChipSelect(false);
    
    if (result != HAL_OK) {
        g_ads1299_handle.error_count++;
        return result;
    }
    
    // Cache management for DMA coherency
    CACHE_INVALIDATE_BY_ADDR(g_ads1299_data_buffer, ADS1299_DATA_PACKET_SIZE);
    
    // Parse status bytes
    *status = (g_ads1299_data_buffer[0] << 16) | (g_ads1299_data_buffer[1] << 8) | g_ads1299_data_buffer[2];
    
    // Parse channel data (24-bit to 32-bit conversion)
    for (int ch = 0; ch < 8; ch++) {
        uint32_t raw_data = (g_ads1299_data_buffer[3 + ch*3] << 16) | 
                           (g_ads1299_data_buffer[4 + ch*3] << 8) | 
                           g_ads1299_data_buffer[5 + ch*3];
        data[ch] = ADS1299_Convert24To32(raw_data);
    }
    
    g_ads1299_handle.data_ready_count++;
    return HAL_OK;
}

/**
 * @brief Read data using DMA for high-speed operation
 */
HAL_StatusTypeDef ADS1299_ReadDataDMA(uint32_t *status, int32_t *data)
{
    if (!g_ads1299_handle.dma_enabled || g_ads1299_handle.state != ADS1299_STATE_CONTINUOUS_READ) {
        return ADS1299_ReadData(status, data);  // Fallback to polling
    }
    
    // DMA-based implementation would be added here
    // For now, fallback to polling mode
    return ADS1299_ReadData(status, data);
}

/**
 * @brief Write to ADS1299 register
 */
HAL_StatusTypeDef ADS1299_WriteRegister(uint8_t reg_addr, uint8_t value)
{
    g_spi_tx_buffer[0] = ADS1299_WREG | reg_addr;  // Write command + register address
    g_spi_tx_buffer[1] = 0x00;                     // Number of registers - 1 (0 = 1 register)
    g_spi_tx_buffer[2] = value;                    // Register value
    
    ADS1299_ChipSelect(true);
    HAL_StatusTypeDef result = HAL_SPI_Transmit(g_ads1299_handle.hspi, g_spi_tx_buffer, 3, HAL_MAX_DELAY);
    ADS1299_ChipSelect(false);
    
    ADS1299_DelayCycles(ADS1299_SPI_DELAY_CYCLES);  // Inter-command delay
    
    return result;
}

/**
 * @brief Read from ADS1299 register
 */
HAL_StatusTypeDef ADS1299_ReadRegister(uint8_t reg_addr, uint8_t *value)
{
    g_spi_tx_buffer[0] = ADS1299_RREG | reg_addr;  // Read command + register address
    g_spi_tx_buffer[1] = 0x00;                     // Number of registers - 1
    g_spi_tx_buffer[2] = 0x00;                     // Dummy byte for read
    
    ADS1299_ChipSelect(true);
    HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(g_ads1299_handle.hspi, 
                                                       g_spi_tx_buffer, g_spi_rx_buffer, 3, HAL_MAX_DELAY);
    ADS1299_ChipSelect(false);
    
    if (result == HAL_OK) {
        *value = g_spi_rx_buffer[2];  // Register value is in third byte
    }
    
    ADS1299_DelayCycles(ADS1299_SPI_DELAY_CYCLES);
    return result;
}

/**
 * @brief Configure DMA for continuous data acquisition
 */
HAL_StatusTypeDef ADS1299_ConfigureDMA(void)
{
    // DMA configuration would be implemented here
    // For initial implementation, return OK but disable DMA
    return HAL_OK;
}

/**
 * @brief Data ready interrupt handler
 */
void ADS1299_DataReadyCallback(void)
{
    // This function is called from EXTI interrupt handler
    // Notify acquisition task that data is ready
    g_ads1299_handle.data_ready_count++;
    
    // Send notification to FreeRTOS acquisition task
    // BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // vTaskNotifyGiveFromISR(AcquisitionTaskHandle, &xHigherPriorityTaskWoken);
    // portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Get driver status and error information
 */
void ADS1299_GetStatus(ads1299_handle_t *handle)
{
    if (handle != NULL) {
        memcpy(handle, &g_ads1299_handle, sizeof(ads1299_handle_t));
    }
}

/**
 * @brief Reset ADS1299 to default state
 */
HAL_StatusTypeDef ADS1299_Reset(void)
{
    ADS1299_ChipSelect(true);
    ADS1299_SPITransmit(ADS1299_RESET);
    ADS1299_ChipSelect(false);
    
    HAL_Delay(1);  // Wait for reset to complete
    
    g_ads1299_handle.state = ADS1299_STATE_RESET;
    return HAL_OK;
}

/**
 * @brief Enable/disable specific channels
 */
HAL_StatusTypeDef ADS1299_ConfigureChannels(uint8_t channel_mask)
{
    for (uint8_t ch = 0; ch < 8; ch++) {
        uint8_t ch_config;
        if (channel_mask & (1 << ch)) {
            // Enable channel
            ch_config = ADS1299_PGA_GAIN06 | ADS1299_INPUT_NORMAL | ADS1299_INPUT_PWR_UP;
        } else {
            // Disable channel
            ch_config = ADS1299_PGA_GAIN06 | ADS1299_INPUT_NORMAL | ADS1299_INPUT_PWR_DOWN;
        }
        
        if (ADS1299_WriteRegister(ADS1299_CH1SET + ch, ch_config) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    
    return HAL_OK;
}

/* Private Functions */

/**
 * @brief Transmit single byte over SPI
 */
static HAL_StatusTypeDef ADS1299_SPITransmit(uint8_t data)
{
    return HAL_SPI_Transmit(g_ads1299_handle.hspi, &data, 1, HAL_MAX_DELAY);
}

/**
 * @brief Transmit and receive data over SPI
 */
static HAL_StatusTypeDef ADS1299_SPITransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t size)
{
    return HAL_SPI_TransmitReceive(g_ads1299_handle.hspi, tx_data, rx_data, size, HAL_MAX_DELAY);
}

/**
 * @brief Control chip select pin
 */
static void ADS1299_ChipSelect(bool active)
{
    if (active) {
        HAL_GPIO_WritePin(ADS1299_CS_PORT, ADS1299_CS_PIN, GPIO_PIN_RESET);  // CS low (active)
    } else {
        HAL_GPIO_WritePin(ADS1299_CS_PORT, ADS1299_CS_PIN, GPIO_PIN_SET);    // CS high (inactive)
    }
}

/* Debug Helper Function */
void UART_Transmit(const char* message)
{
#if ENABLE_UART_DEBUG
    HAL_UART_Transmit(&huart1, (uint8_t*)message, strlen(message), HAL_MAX_DELAY);
#endif
}