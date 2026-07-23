/* IDE-only stub — real header from dtc-lite at build time.
 * Provide DTC_GEN_* macros referenced by board_config / vfs / drivers.
 */
#ifndef DT_CONFIG_GEN_H
#define DT_CONFIG_GEN_H

#define DTC_GEN_COUNT_HETEROGENEOUS_GPIOS  1
#define DTC_GEN_COUNT_WINBOND_W25Q64       1
#define DTC_GEN_COUNT_CAN_HOST             2
#define DTC_GEN_COUNT_UART                 1
#define DTC_GEN_COUNT_SPI_MASTER           1
#define DTC_GEN_COUNT_I2C_MASTER           1
#define DTC_GEN_COUNT_I2S_MASTER           1
#define DTC_GEN_COUNT_ADC                  1
#define DTC_GEN_COUNT_DAC                  1
#define DTC_GEN_COUNT_TIM                  2
#define DTC_GEN_COUNT_USB_OTG_HOST         1

#define DTC_GEN_CPU_CLOCK_HZ               168000000
#define DTC_GEN_TICK_RATE_HZ               1000
#define DTC_GEN_HEAP_SIZE                  32768

#define DTC_GEN_SPI_HOST_MAX               4
#define DTC_GEN_SPI_MAX_XFER               512
#define DTC_GEN_UART_HOST_MAX              6
#define DTC_GEN_UART_MAX_XFER              512
#define DTC_GEN_UART_TIMEOUT_MS            10
#define DTC_GEN_I2C_HOST_MAX               3
#define DTC_GEN_I2C_MAX_XFER               512
#define DTC_GEN_I2S_HOST_MAX               2
#define DTC_GEN_RTC_HOST_MAX               1
#define DTC_GEN_STM32_TIM_HOST_MAX         14
#define DTC_GEN_ADC_HOST_MAX               3
#define DTC_GEN_DAC_HOST_MAX               1
#define DTC_GEN_CAN_HOST_MAX               2
#define DTC_GEN_CAN_MAX_XFER               8
#define DTC_GEN_USB_HOST_MAX               1
#define DTC_GEN_USB_CLIENT_MAX             3

#endif /* DT_CONFIG_GEN_H */
