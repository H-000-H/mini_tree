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
 * - hal_uart_bus_host 嵌入 bus 层 uart_bus_host (非指针), HAL 无池管理, 无 vtable
 * - HAL 层不分配数据缓冲区, tx/rx 指针由调用者提供
 *
 * 头中立化: 本头不暴露任何 vendor 类型, 只用 uintptr_t/int/void*。
 * vendor 头由 hal_uart_*.c 内部 include。
 */
#ifndef HAL_UART_H
#define HAL_UART_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*============================================================================*/
    /*                              DMA 配置 (硬件直投, 仿 ADC)                    */
    /*============================================================================*/
    /* 纯数据实体: 所有字段由 DTSI 提供厂商宏值, HAL 零计算直接灌入 LL_DMA。
     * - dma_handle: DMA 控制器寄存器基址 (DMA1_BASE / DMA2_BASE)
     * - dma_stream: DMA 流编号 (LL_DMA_STREAM_0..7)
     * - dma_channel: DMA 通道编号 (LL_DMA_CHANNEL_0..7)
     * - dma_enable: 0=不使用 DMA, 1=使用 DMA
     * - 其余字段为 LL_DMA 厂商宏值
     */
    struct hal_uart_dma_config
    {
        uint32_t dma_enable; /**< DMA 使能: 0=禁用, 1=启用 */
        uintptr_t dma_handle; /**< DMA 控制器寄存器基址 */
        uint32_t dma_stream; /**< DMA 流编号 */
        uint32_t dma_channel; /**< DMA 通道编号 */
        uint32_t dma_priority; /**< DMA 优先级 */
        uint32_t dma_memory_size; /**< DMA 内存数据宽度 */
        uint32_t dma_direction; /**< DMA 传输方向 (LL_DMA_DIRECTION_MEMORY_TO_PERIPH) */
        uint32_t dma_mode; /**< DMA 模式 (LL_DMA_MODE_NORMAL/CIRCULAR) */
        uint32_t dma_periph_inc; /**< DMA 外设地址递增 */
        uint32_t dma_mem_inc; /**< DMA 内存地址递增 */
        uint32_t dma_periph_data_size; /**< DMA 外设数据宽度 */
        uint32_t dma_fifo_mode; /**< DMA FIFO 模式 */
        uint32_t dma_fifo_threshold; /**< DMA FIFO 阈值 */
        uint32_t dma_mem_burst; /**< DMA 内存突发 */
        uint32_t dma_periph_burst; /**< DMA 外设突发 */
    };

    /*============================================================================*/
    /*                              引脚配置 (硬件直投)                            */
    /*============================================================================*/
    /* 纯数据实体: 所有字段由 DTSI 提供厂商宏值, HAL 零计算直接灌入 LL 库/标准外设库。
     * TX/RX 均用此结构体 (含 af)。
     * - STM32: af = GPIO_AF8_UART4 等 (LL_GPIO AF 选择)
     * - WCH: af = GPIO_Mode_AF_PP / GPIO_Mode_IN_FLOATING 等 (GPIOMode_TypeDef)
     * - ESP32: port=0, clk_bus=0, af=0, output_type=0, speed=0, mode=0, pull=0,
     *   pin=SoC GPIO 编号 (无 AF 概念)
     */
    struct hal_uart_pin_cfg
    {
        uintptr_t port; /**< GPIOx_BASE */
        uint16_t pin; /**< GPIO_PIN_x */
        uint32_t clk_bus; /**< LL_AHBx_GRPy_PERIPH_GPIOx */
        uint32_t af; /**< GPIO_AFx_UARTy */
        uint32_t output_type; /**< LL_GPIO_OUTPUT_* */
        uint32_t speed; /**< LL_GPIO_SPEED_* */
        uint32_t mode; /**< LL_GPIO_MODE_* */
        uint32_t pull; /**< LL_GPIO_PULL_* */
    };

    /*============================================================================*/
    /*                              UART 配置 (硬件直投)                           */
    /*============================================================================*/
    struct hal_uart_config
    {
        uintptr_t uart; /**< STM32/WCH: 基地址; ESP32: (uintptr_t)uart_host */
        uint32_t uart_clk_periph; /**< LL_APBx_GRPy_PERIPH_UARTy */
        int32_t irqn; /**< NVIC 中断号 (DTS irqn, -1 = 无中断) */
        uint32_t irq_priority; /**< NVIC 中断优先级 (DTS irq-priority, 0=最高) */
        uint32_t it_enable; /**< 中断模式使能: 0=禁用, 1=启用 DMA TC/NVIC 中断 */
        uint32_t baud_rate; /**< 波特率 (如 115200) */
        uint32_t data_width; /**< 数据位宽 (LL_USART_DATAWIDTH_*) */
        uint32_t parity; /**< 校验位 (LL_USART_PARITY_*) */
        uint32_t stop_bits; /**< 停止位 (LL_USART_STOPBITS_*) */
        uint32_t direction; /**< 传输方向 (LL_USART_DIRECTION_TX_RX/RX/TX) */
        uint32_t hw_control; /**< 硬件流控 (LL_USART_HWCONTROL_NONE/RTS/CTS/RTS_CTS) */
        uint32_t oversampling; /**< 过采样 (LL_USART_OVERSAMPLING_16/8) */
        struct hal_uart_pin_cfg tx; /**< TX 引脚配置 */
        struct hal_uart_pin_cfg rx; /**< RX 引脚配置 */
        struct hal_uart_dma_config dma_cfg; /**< DMA 配置 (dma_enable=0 时不使用 DMA) */
    };

    /*============================================================================*/
    /*                              Host / Device 对象                            */
    /*============================================================================*/
    /*
     * hal_uart_bus_host 嵌入 bus 层 uart_bus_host (非指针), HAL 无池管理, 无 vtable。
     * 跨平台字段说明:
     * - uart: STM32/WCH 缓存 cfg.uart (fast path); ESP32 缓存 (uintptr_t)uart_host
     * - uart_queue: ESP32 FreeRTOS QueueHandle_t (头中立用 void*); STM32/WCH 为 NULL
     */
    struct hal_uart_bus_host
    {
        struct hal_uart_config cfg; /**< UART 配置 (DTSI 直投) */
        uintptr_t uart; /**< 缓存 cfg.uart, fast path */
        void* uart_queue; /**< ESP32 FreeRTOS QueueHandle_t; 其他 NULL */
        volatile uint8_t status; /**< 运行状态 */
        bool hw_inited; /**< 硬件已初始化 */
    };

    /**
     * @brief HAL UART 设备句柄 (轻量: bus_host 指针 + hw_open)
     */
    struct hal_uart_dev
    {
        struct hal_uart_bus_host* ctlr; /**< 所属 host */
        int hw_open; /**< 硬件打开计数 */
    };

    /*============================================================================*/
    /*                              Device 管理 API                               */
    /*============================================================================*/
    int hal_uart_dev_init(struct hal_uart_bus_host* host,
                          const struct hal_uart_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    int hal_uart_dev_hw_open(struct hal_uart_bus_host* host) COMPAT_WARN_UNUSED_RESULT;
    int hal_uart_dev_hw_close(struct hal_uart_bus_host* host) COMPAT_WARN_UNUSED_RESULT;

    /*============================================================================*/
    /*                              同步传输                                       */
    /*============================================================================*/
    int hal_uart_write(struct hal_uart_dev* pdev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    int hal_uart_read(struct hal_uart_dev* pdev, uint8_t* data, size_t len,
                      uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

    /*============================================================================*/
    /*                              DMA 传输 (STM32/WCH 支持, ESP32 返回 NOTSUPP)  */
    /*============================================================================*/
    /* DMA 配置由 host->cfg.dma_cfg 提供 (硬件直投, 仿 ADC), 无需外部传入通道句柄。
     * dma_enable=0 时返回 VFS_ERR_NOTSUPP。 */
    int hal_uart_write_dma(struct hal_uart_dev* pdev, const uint8_t* data, size_t len,
                           uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    int hal_uart_dma_abort(struct hal_uart_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
