/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_uart.h
 *@brief hal uart 头文件
 *@author H-000-H
 *@details
 *   UART HAL 层 — 硬件抽象接口 (跨平台统一头)
 *   设计: 硬件直投, DTSI 提供厂商宏值, HAL 零翻译透传给 LL 库/标准外设库/ESP-IDF driver。
 *   - STM32: uart-base = <UART4_BASE>, uart-clk = <LL_APB1_GRP1_PERIPH_UART4>,
 *   tx-port = <GPIOC_BASE>, tx-pin = <GPIO_PIN_10>,
 *   tx-clk  = <LL_AHB1_GRP1_PERIPH_GPIOC>, tx-af = <GPIO_AF8_UART4>
 *   - WCH: uart-base = <USART1_BASE>, uart-clk = <RCC_APB2Periph_USART1>,
 *   tx-port = <GPIOA_BASE>, tx-pin = <GPIO_Pin_9>,
 *   tx-clk  = <RCC_APB2Periph_GPIOA>, tx-af = <GPIO_Mode_AF_PP>
 *   (WCH 的 af 字段承载 GPIOMode_TypeDef, mode+af 编码在一起)
 *   - ESP32: uart-base = <UART_NUM_1>, uart-clk = <0>,
 *   tx-port = <0>, tx-pin = <43>, tx-clk = <0>, tx-af = <0>
 *   (ESP32 无 port/clk/af 概念, pin 为 SoC GPIO 编号)
 *   - hal_uart_bus_host 嵌入 bus 层 uart_bus_host (非指针), HAL 无池管理, 无 vtable
 *   - HAL 层不分配数据缓冲区, tx/rx 指针由调用者提供
 *   头中立化: 本头不暴露任何 vendor 类型, 只用 uintptr_t/int/void*。
 *   vendor 头由 hal_uart_*.c 内部 include。
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

    /**
     * @brief 绑定 UART 主机并应用 DTSI 配置 (零计算直投)
     * @param[in] host UART 主机对象指针 (非 NULL)
     * @param[in] cfg UART 配置指针 (DTSI 直投, 生命周期由调用方持有)
     * @return 成功返回 MINI_OK, host 或 cfg 为空返回 MINI_ERR_INVAL
     */
    int hal_uart_dev_init(struct hal_uart_bus_host* host,
                          const struct hal_uart_config* cfg) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 打开 UART 硬件 (引用计数 +1, 首次触发底层 init)
     * @param[in] host UART 主机对象指针
     * @return 成功返回 MINI_OK, host 为空返回 MINI_ERR_INVAL
     */
    int hal_uart_dev_hw_open(struct hal_uart_bus_host* host) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 UART 硬件 (引用计数 -1, 归零触发底层 deinit)
     * @param[in] host UART 主机对象指针
     * @return 成功返回 MINI_OK, host 为空返回 MINI_ERR_INVAL
     */
    int hal_uart_dev_hw_close(struct hal_uart_bus_host* host) MINI_WARN_UNUSED_RESULT;

    /**
     * @brief 同步阻塞发送 (polling/中断, 按 it_enable 决定)
     * @param[in] pdev UART 设备对象指针
     * @param[in] data 发送缓冲区
     * @param[in] len 发送字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_* (超时 MINI_ERR_TIMEOUT)
     */
    int hal_uart_write(struct hal_uart_dev* pdev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 同步阻塞接收 (polling/中断, 按 it_enable 决定)
     * @param[in] pdev UART 设备对象指针
     * @param[out] data 接收缓冲区
     * @param[in] len 接收字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_* (超时 MINI_ERR_TIMEOUT)
     */
    int hal_uart_read(struct hal_uart_dev* pdev, uint8_t* data, size_t len,
                      uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

    /* DMA 配置由 host->cfg.dma_cfg 提供 (硬件直投, 仿 ADC), 无需外部传入通道句柄。
     * dma_enable=0 时返回 MINI_ERR_NOTSUPP。 */
    /**
     * @brief DMA 异步发送 (无 OS 等待, 依赖 DMA TC 中断)
     * @param[in] pdev UART 设备对象指针
     * @param[in] data 发送缓冲区 (DMA 期间须保持有效)
     * @param[in] len 发送字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, DMA 不可用返回 MINI_ERR_NOTSUPP, 失败返回 VFS_ERR_*
     */
    int hal_uart_write_dma(struct hal_uart_dev* pdev, const uint8_t* data, size_t len,
                           uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 终止正在进行的 UART DMA 传输
     * @param[in] pdev UART 设备对象指针
     * @return 成功返回 MINI_OK, 无进行中传输返回 MINI_ERR_INVAL
     */
    int hal_uart_dma_abort(struct hal_uart_dev* pdev) MINI_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
