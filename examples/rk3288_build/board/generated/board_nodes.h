#ifndef BOARD_NODES_H
#define BOARD_NODES_H

#include <stdint.h>

/* ===== 设备 ID 枚举 (自动生成) ===== */
typedef enum {
    DEV_ID_ = 0,
    DEV_ID_UART_40010000 = 1,
    DEV_ID_I2C_40020000 = 2,
    DEV_ID_SPI_40030000 = 3,
    DEV_ID_ADC_40040000 = 4,
    DEV_ID_PWM_40050000 = 5,
    DEV_ID_GPIO_40060000 = 6,
    DEV_ID_COUNT = 7
} device_id_t;

#endif /* BOARD_NODES_H */
