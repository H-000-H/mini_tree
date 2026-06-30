/* SPDX-License-Identifier: Apache-2.0 */
/*
 * UART HAL 层 — 硬件抽象接口 (跨平台统一头)
 *
 * 设计: 硬件直投, DTSI 提供厂商宏值, HAL 零翻译透传给 LL 库/标准外设库/ESP-IDF driver。
 * - STM32: uart-base = <UART4_BASE>, uart-clk = <LL_APB1_GRP1_PERIPH_UART4>,
 *   tx-port = <GPIOC_BASE>, tx-pin = <GPIO_PIN_10>,
 *   tx-clk  = <LL_AHB1_GRP1_PERIPH_GPIOC>, tx-af = <GPIO_AF8_UART4>
 * - WCH: uart-base = <USART1_BASE>, uart-clk = <RCC_APB2Periph_USART1>,
 *   tx-port = <GPIOA_BASE>, tx-pin = <GPIO_Pin_9>,
 *   tx-clk  = <RCC_APB2Periph_GPIOA>, tx-af = <GPIO_Mode_AF_PP>
 *   (WCH 的 af 字段承载 GPIOMode_TypeDef, mode+af 编码在一起)
 * - ESP32: uart-base = <UART_NUM_1>, uart-clk = <0>,
 *   tx-port = <0>, tx-pin = <43>, tx-clk = <0>, tx-af = <0>
 *   (ESP32 无 port/clk/af 概念, pin 为 SoC GPIO 编号)
 * - hal_uart_dev 嵌入 bus 层 uart_bus_host (非指针), HAL 无池管理, 无 vtable
 * - HAL 层不分配数据缓冲区, tx/rx 指针由调用者提供
 *
 * 头中立化: 本头不暴露任何 vendor 类型, 只用 uintptr_t/int/void*。
 * vendor 头由 hal_uart_*.c 内部 include。
 */
#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "compiler_compat.h"
#include "VFS.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bus_dma_chan;

/*============================================================================*/
/*                              引脚配置 (硬件直投)                            */
/*============================================================================*/
/* 纯数据实体: 所有字段由 DTSI 提供厂商宏值, HAL 零计算直接灌入 LL 库/标准外设库。
 * TX/RX 均用此结构体 (含 af)。
 * - STM32: af = GPIO_AF8_UART4 等 (LL_GPIO AF 选择)
 * - WCH: af = GPIO_Mode_AF_PP / GPIO_Mode_IN_FLOATING 等 (GPIOMode_TypeDef)
 * - ESP32: port=0, clk_periph=0, af=0, pin=SoC GPIO 编号 (无 AF 概念)
 */
struct hal_uart_pin_cfg
{
    uintptr_t port;
    uint16_t  pin;
    uint32_t  clk_periph;
    uint32_t  af;
};

/*============================================================================*/
/*                              UART 配置 (硬件直投)                           */
/*============================================================================*/
struct hal_uart_config
{
    uintptr_t                 uart;          /* STM32/WCH: 基地址; ESP32: (uintptr_t)uart_host */
    uint32_t                  uart_clk_periph;
    uint32_t                  baud_rate;
    uint32_t                  data_width;
    uint32_t                  parity;
    uint32_t                  stop_bits;
    struct hal_uart_pin_cfg   tx;
    struct hal_uart_pin_cfg   rx;
};

/*============================================================================*/
/*                              HAL 对象 (嵌入 bus 层)                         */
/*============================================================================*/
/* 纯数据实体, 嵌入 bus 层 uart_bus_host (非指针), HAL 无池管理无 alloc/free。
 * uart 缓存 cfg.uart, fast path 直接访问。
 *
 * 跨平台字段说明:
 * - uart: STM32/WCH 缓存 cfg.uart (fast path); ESP32 缓存 (uintptr_t)uart_host
 * - uart_queue: ESP32 FreeRTOS QueueHandle_t (头中立用 void*); STM32/WCH 为 NULL
 */
struct hal_uart_dev
{
    struct hal_uart_config   cfg;
    uintptr_t                uart;          /* 缓存 cfg.uart, fast path */
    int                      pool_idx;
    int                      hw_open;
    bool                     hw_inited;
    void*                    uart_queue;    /* ESP32 FreeRTOS QueueHandle_t; 其他 NULL */
    volatile uint8_t         status;
};

typedef enum {
    UART_STATE_UNINIT = 0,
    UART_STATE_READY,
    UART_STATE_BUSY,
    UART_STATE_ERROR
} uart_status_t;

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
/* 对象由 bus 层提供 (嵌入), HAL 不做池管理。pool_idx 由 bus 层传入。 */
void hal_uart_dev_init(struct hal_uart_dev* dev, int pool_idx,
                       const struct hal_uart_config* cfg);
int  hal_uart_dev_hw_open(struct hal_uart_dev* dev) COMPAT_WARN_UNUSED_RESULT;
int  hal_uart_dev_hw_close(struct hal_uart_dev* dev) COMPAT_WARN_UNUSED_RESULT;

/*============================================================================*/
/*                              同步传输                                       */
/*============================================================================*/
int  hal_uart_write(struct hal_uart_dev* dev, const uint8_t* data, size_t len)
    COMPAT_WARN_UNUSED_RESULT;
int  hal_uart_read(struct hal_uart_dev* dev, uint8_t* data, size_t len)
    COMPAT_WARN_UNUSED_RESULT;

/* 强制停止所有 UART (panic/reboot 路径) */
int  hal_uart_force_stop(void) COMPAT_WARN_UNUSED_RESULT;

/*============================================================================*/
/*                              DMA 传输 (STM32/WCH 支持, ESP32 返回 NOTSUPP)  */
/*============================================================================*/
int  hal_uart_write_dma(struct hal_uart_dev* pdev,
                        struct bus_dma_chan* dma_tx,
                        const uint8_t* data, size_t len,
                        uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
void hal_uart_dma_abort(struct hal_uart_dev* pdev, struct bus_dma_chan* dma_tx);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
