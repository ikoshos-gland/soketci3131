#ifndef ADS1299_DRIVER_H
#define ADS1299_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"
#include "semg_config.h"
#include <stdint.h>

/* ADS1299 Register Addresses - From Working Implementation */
#define ADS1299_ID          0x00
#define ADS1299_CONFIG1     0x01
#define ADS1299_CONFIG2     0x02
#define ADS1299_CONFIG3     0x03
#define ADS1299_LOFF        0x04
#define ADS1299_CH1SET      0x05
#define ADS1299_CH2SET      0x06
#define ADS1299_CH3SET      0x07
#define ADS1299_CH4SET      0x08
#define ADS1299_CH5SET      0x09
#define ADS1299_CH6SET      0x0A
#define ADS1299_CH7SET      0x0B
#define ADS1299_CH8SET      0x0C
#define ADS1299_BIAS_SENSP  0x0D
#define ADS1299_BIAS_SENSN  0x0E
#define ADS1299_LOFF_SENSP  0x0F
#define ADS1299_LOFF_SENSN  0x10
#define ADS1299_LOFF_FLIP   0x11
#define ADS1299_LOFF_STATP  0x12
#define ADS1299_LOFF_STATN  0x13
#define ADS1299_GPIO        0x14
#define ADS1299_MISC1       0x15
#define ADS1299_MISC2       0x16
#define ADS1299_CONFIG4     0x17

/* ADS1299 SPI Commands - From Working Implementation */
#define ADS1299_WAKEUP      0x02
#define ADS1299_STANDBY     0x04
#define ADS1299_RESET       0x06
#define ADS1299_START       0x08
#define ADS1299_STOP        0x0A
#define ADS1299_RDATAC      0x10
#define ADS1299_SDATAC      0x11
#define ADS1299_RDATA       0x12
#define ADS1299_RREG        0x20
#define ADS1299_WREG        0x40

/* Channel Settings - From Working Implementation */
#define ADS1299_INPUT_PWR_DOWN     0x80
#define ADS1299_INPUT_PWR_UP       0x00
#define ADS1299_PGA_GAIN01         0x00
#define ADS1299_PGA_GAIN02         0x10
#define ADS1299_PGA_GAIN04         0x20
#define ADS1299_PGA_GAIN06         0x30
#define ADS1299_PGA_GAIN08         0x40
#define ADS1299_PGA_GAIN12         0x50
#define ADS1299_PGA_GAIN24         0x60
#define ADS1299_INPUT_NORMAL       0x00
#define ADS1299_INPUT_SHORTED      0x01
#define ADS1299_INPUT_BIAS_MEAS    0x02
#define ADS1299_INPUT_MVDD         0x03
#define ADS1299_INPUT_TEMP         0x04
#define ADS1299_INPUT_TESTSIG      0x05
#define ADS1299_INPUT_BIAS_DRP     0x06
#define ADS1299_INPUT_BIAS_DRN     0x07

/* CONFIG2 Test Signal Settings */
#define ADS1299_TEST_INT           0x10
#define ADS1299_TESTSIGNAL_AMP_1X  0x00
#define ADS1299_TESTSIGNAL_AMP_2X  0x04
#define ADS1299_TESTSIGNAL_PULSE_SLOW  0x00
#define ADS1299_TESTSIGNAL_PULSE_FAST  0x01
#define ADS1299_TESTSIGNAL_DC      0x03

/* Lead-off Settings */
#define ADS1299_LOFF_FREQ_DC       0x00
#define ADS1299_LOFF_FREQ_FS_4     0x01
#define ADS1299_LOFF_FREQ_FS_16    0x02
#define ADS1299_LOFF_FREQ_FS_64    0x03
#define ADS1299_LOFF_FREQ_FS_256   0x04
#define ADS1299_LOFF_FREQ_FMOD_4   0x05

/* Hardware Pin Definitions - STM32H7RS Nucleo */
#define ADS1299_SPI_INSTANCE       SPI1
#define ADS1299_CS_PORT            GPIOD
#define ADS1299_CS_PIN             GPIO_PIN_7
#define ADS1299_RESET_PORT         GPIOB
#define ADS1299_RESET_PIN          GPIO_PIN_7
#define ADS1299_START_PORT         GPIOB
#define ADS1299_START_PIN          GPIO_PIN_6
#define ADS1299_DRDY_PORT          GPIOD
#define ADS1299_DRDY_PIN           GPIO_PIN_6

/* Timing Constants - From Working Implementation */
#define ADS1299_POWER_UP_DELAY_MS   40
#define ADS1299_RESET_PULSE_CYCLES  1100    // ~2µs at 600MHz
#define ADS1299_RESET_DELAY_CYCLES  5500    // ~10µs at 600MHz
#define ADS1299_SPI_DELAY_CYCLES    550     // ~1µs at 600MHz
#define ADS1299_DRDY_TIMEOUT_MS     10

/* Data Structure Sizes */
#define ADS1299_DATA_PACKET_SIZE    27      // 3 status + 24 data bytes
#define ADS1299_STATUS_BYTES        3
#define ADS1299_DATA_BYTES_PER_CH   3
#define ADS1299_EXPECTED_ID         0x3E

/* Driver State Machine */
typedef enum {
    ADS1299_STATE_UNINITIALIZED = 0,
    ADS1299_STATE_POWER_UP,
    ADS1299_STATE_RESET,
    ADS1299_STATE_CONFIGURED,
    ADS1299_STATE_CONTINUOUS_READ,
    ADS1299_STATE_ERROR
} ads1299_state_t;

/* ADS1299 Configuration Structure */
typedef struct {
    uint8_t sample_rate;          // CONFIG1 register value
    uint8_t test_signal_config;   // CONFIG2 register value  
    uint8_t reference_config;     // CONFIG3 register value
    uint8_t channel_config[8];    // CH1SET-CH8SET register values
    uint8_t lead_off_config;      // LOFF register value
    bool bias_enabled;
    bool lead_off_detection;
} ads1299_config_t;

/* Driver Handle Structure */
typedef struct {
    SPI_HandleTypeDef *hspi;
    ads1299_state_t state;
    ads1299_config_t config;
    uint32_t last_error;
    uint32_t data_ready_count;
    uint32_t error_count;
    bool dma_enabled;
} ads1299_handle_t;

/* Global Variables - Memory Optimized for STM32H7RS */
extern uint8_t  g_spi_tx_buffer[32] SRAM2_SECTION;
extern uint8_t  g_spi_rx_buffer[32] SRAM2_SECTION;
extern uint8_t  g_ads1299_data_buffer[ADS1299_DATA_PACKET_SIZE] AXI_SRAM_SECTION;
extern ads1299_handle_t g_ads1299_handle;

/* Function Prototypes */

/**
 * @brief Initialize ADS1299 with EMG-optimized configuration
 * @param hspi: SPI handle for communication
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef ADS1299_Init(SPI_HandleTypeDef *hspi);

/**
 * @brief Power-up sequence for ADS1299 (exact timing from working code)
 */
void ADS1299_PowerUpSequence(void);

/**
 * @brief Stop continuous data read mode
 */
void ADS1299_StopDataRead(void);

/**
 * @brief Start continuous data read mode (RDATAC)
 */
HAL_StatusTypeDef ADS1299_StartContinuousRead(void);

/**
 * @brief Read data packet (27 bytes: 3 status + 24 data)
 * @param status: Pointer to status bytes (3 bytes)
 * @param data: Pointer to channel data array (8 channels x 3 bytes)
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef ADS1299_ReadData(uint32_t *status, int32_t *data);

/**
 * @brief Read data using DMA for high-speed operation
 * @param status: Pointer to status bytes
 * @param data: Pointer to channel data array
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef ADS1299_ReadDataDMA(uint32_t *status, int32_t *data);

/**
 * @brief Write to ADS1299 register
 * @param reg_addr: Register address
 * @param value: Value to write
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef ADS1299_WriteRegister(uint8_t reg_addr, uint8_t value);

/**
 * @brief Read from ADS1299 register
 * @param reg_addr: Register address
 * @param value: Pointer to store read value
 * @return HAL_StatusTypeDef: HAL_OK if successful
 */
HAL_StatusTypeDef ADS1299_ReadRegister(uint8_t reg_addr, uint8_t *value);

/**
 * @brief Configure DMA for continuous data acquisition
 */
HAL_StatusTypeDef ADS1299_ConfigureDMA(void);

/**
 * @brief Data ready interrupt handler (called from EXTI callback)
 */
void ADS1299_DataReadyCallback(void);

/**
 * @brief Get driver status and error information
 * @param handle: Pointer to store handle information
 */
void ADS1299_GetStatus(ads1299_handle_t *handle);

/**
 * @brief Reset ADS1299 to default state
 */
HAL_StatusTypeDef ADS1299_Reset(void);

/**
 * @brief Enable/disable specific channels
 * @param channel_mask: Bitmask of channels to enable (bit 0 = CH1, bit 7 = CH8)
 */
HAL_StatusTypeDef ADS1299_ConfigureChannels(uint8_t channel_mask);

/**
 * @brief Low-level delay function (cycle-accurate for timing)
 * @param cycles: Number of CPU cycles to delay
 */
static inline void ADS1299_DelayCycles(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __NOP();
    }
}

/**
 * @brief Convert 24-bit two's complement to 32-bit signed integer
 * @param data_24bit: 24-bit data from ADS1299
 * @return int32_t: Converted 32-bit signed value
 */
static inline int32_t ADS1299_Convert24To32(uint32_t data_24bit) {
    if (data_24bit & 0x800000) {
        return (int32_t)(data_24bit | 0xFF000000);  // Sign extend
    } else {
        return (int32_t)(data_24bit & 0x00FFFFFF);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* ADS1299_DRIVER_H */