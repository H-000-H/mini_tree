/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART HAL 层 — 硬件抽象接口 (STM32/CH32)
 *
 * 结构与 API 与 ESP32 hal_uart.h 对齐, 采用 vtable 模式。
 * 职责: 寄存器配置与传输执行, 不含锁/DMA 策略。
 *
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 * 需要中间缓冲的场景在 VFS 层通过 buffer_pool 处理。
 */
#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hal_gpio.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bus_dma_chan;

typedef enum {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_EVEN,
    HAL_UART_PARITY_ODD
} hal_uart_parity_t;

typedef enum {
    HAL_UART_STOP_BITS_1 = 0,
    HAL_UART_STOP_BITS_1_5,
    HAL_UART_STOP_BITS_2
} hal_uart_stop_bits_t;

typedef enum {
    HAL_UART_DATA_BITS_5 = 5,
    HAL_UART_DATA_BITS_6 = 6,
    HAL_UART_DATA_BITS_7 = 7,
    HAL_UART_DATA_BITS_8 = 8
} hal_uart_data_bits_t;

typedef enum {
    UART_STATE_UNINIT = 0,
    UART_STATE_READY,
    UART_STATE_BUSY,
    UART_STATE_ERROR
} uart_status_t;

struct hal_uart_config_t {
    uint32_t             baud_rate;
    hal_uart_data_bits_t data_bits;
    hal_uart_parity_t    parity;
    hal_uart_stop_bits_t stop_bits;
    hal_pin_t            tx_io;
    hal_pin_t            rx_io;
    int                  uart_host;
};

struct hal_uart_dev {
    struct hal_uart_config_t        cfg;
    int                             hw_open;
    int                             pool_idx;
    bool                            hw_inited;
    void*                           uart_queue;  /* 平台私有 (ESP32: QueueHandle_t, st/ch: NULL) */
    volatile uint8_t                status;
};

typedef void (*hal_uart_rx_irq_callback_t)(struct hal_uart_config_t* pdev,
                                            void* user_data, void** callback_data);

struct hal_uart_bus {
    int  (*open)(const struct hal_uart_config_t* cfg);
    int  (*close)(const struct hal_uart_config_t* cfg);
    int  (*read)(struct hal_uart_dev* pdev, uint8_t* data, size_t len);
    int  (*write)(struct hal_uart_dev* pdev, const uint8_t* data, size_t len);
    int  (*transmit)(struct hal_uart_dev* pdev, uint8_t* rx, uint8_t* tx,
                     size_t rx_len, size_t tx_len);
    int  (*deinit)(const struct hal_uart_config_t* cfg);
    void* _impl;
};

/* 平台总线 vtable 获取 */
const struct hal_uart_bus* hal_uart_bus_get(void);

/* xfer 生命周期 (bus 层调用) */
int hal_uart_xfer_begin(struct hal_uart_dev* pdev, uint32_t timeout_ms);
int hal_uart_xfer_end(struct hal_uart_dev* pdev);

/* 强制停止 */
int hal_uart_force_stop(void) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
