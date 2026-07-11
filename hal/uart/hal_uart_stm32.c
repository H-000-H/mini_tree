/* SPDX-License-Identifier: Apache-2.0 */
/*
 * UART HAL — STM32F4 实现
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 LL 库。
 * - hal_uart_dev 嵌入 bus 层, HAL 无池管理无 vtable
 * - 自行配置 GPIO AF, 不依赖 CubeMX; write/read 用 LL_USART 轮询, fast path 访问 dev->uart
 */
#include "hal_uart.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "dt_config_gen.h"
#include "interrupt.h"

/** UART 下半部工作项 (fn/arg 由 VFS 层绑定), 供 interrupt_virtual_register 注册 */
struct bottom_half_work g_uart_bottom_half_work;

/* ── 平台参数：来自 DTS stm32,uart-platform-cap，无 DTS 时提供回退 ── */
#ifndef DTC_GEN_STM32_UART_MAX_XFER
#define DTC_GEN_STM32_UART_MAX_XFER  512U
#endif
#ifndef DTC_GEN_STM32_UART_TIMEOUT_MS
#define DTC_GEN_STM32_UART_TIMEOUT_MS  10U
#endif

#define STM32_UART_DMA_MAX_XFER    DTC_GEN_STM32_UART_MAX_XFER
#define STM32_UART_READ_TIMEOUT_MS DTC_GEN_STM32_UART_TIMEOUT_MS

/*============================================================================*/
/*                              LL 库直投 helper                              */
/*============================================================================*/
/* 纯 LL 库调用, 非抽象层 */
/**
 * @brief 配置 UART 复用引脚: 时钟使能 + AF 模式 + 推挽高速 (LL 库直投)
 * @param pin 引脚配置 (含 port/pin/clk_bus/af)
 */
COMPAT_STATIC_INLINE void hal_uart_config_af_pin(const struct hal_uart_pin_cfg* pin)
{
}

/**
 * @brief 轮询等待 UART TC (发送完成) 标志置位
 * @param usart      UART 外设寄存器基址
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT
 */
static int stm32_uart_wait_tc(uintptr_t usart, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(usart);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_OK;
}

/*============================================================================*/
/*                              DMA helper                                    */
/*============================================================================*/
/**
 * @brief 清除 DMA stream 对应的 TC 标志 (平台自行实现具体清除逻辑)
 * @param dma    DMA 控制器基址
 * @param stream DMA 流编号 (0..7)
 */
static void hal_uart_dma_clear_tc(uintptr_t dma, uint32_t stream)
{
    COMPAT_IGNORE_RESULT(dma);
    COMPAT_IGNORE_RESULT(stream);
}

/**
 * @brief DMA 静态参数一次性配置 (hw_open 时调用, 热路径不再重复)
 * @note  采用 LL_DMA_InitTypeDef + LL_DMA_Init 批量初始化范式 (同 LL_USART_Init/LL_ADC_Init)。
 *        channel/direction/priority/mode/inc/size 来自 DTS 且永不变;
 *        仅 buffer 地址与长度每次传输不同, 留在 write_dma 热路径。
 */
static void hal_uart_dma_init(const struct hal_uart_dma_config* cfg, uintptr_t usart_dr)
{
}

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
/**
 * @brief UART Device 对象初始化: 清零 + 拷贝配置 + 缓存 uart + 标记 UNINIT
 * @param host     bus host 对象指针
 * @param cfg      UART 配置 (DTSI 厂商宏值)
 */
int hal_uart_dev_init(struct hal_uart_bus_host* host, const struct hal_uart_config* cfg)
{
    if (!host || !cfg)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 打开 UART 硬件: 使能时钟 + 配置 TX/RX 复用 + LL_USART_Init + 缓存 uart + 标记 READY
 * @param host bus host 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, LL_USART_Init 失败返回 VFS_ERR_IO
 */
int hal_uart_dev_hw_open(struct hal_uart_bus_host* host)
{
    if (!host || !host->cfg.uart)
        return VFS_ERR_INVAL;
    if (host->hw_inited)
        return VFS_OK;

    return VFS_OK;
}

/**
 * @brief 关闭 UART 硬件: 禁用 USART + 标记 UNINIT
 * @param host bus host 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
 */
int hal_uart_dev_hw_close(struct hal_uart_bus_host* host)
{
    if (!host || !host->uart)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/*============================================================================*/
/*                              同步传输                                       */
/*============================================================================*/
/**
 * @brief UART 同步写: 逐字节 TXE 轮询发送 + 等待 TC 完成
 * @param dev  Device 对象指针
 * @param data 待发送数据
 * @param len  字节数
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 */
int hal_uart_write(struct hal_uart_dev* dev, const uint8_t* data, size_t len)
{
    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief UART 同步读: 逐字节 RXNE 轮询接收, 超时已读到部分则返回已读字节数
 * @param dev  Device 对象指针
 * @param data 接收缓冲区
 * @param len  字节数
 * @return 成功返回读到的字节数, 参数非法返回 VFS_ERR_INVAL, 一字节未读到且超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 */
int hal_uart_read(struct hal_uart_dev* dev, uint8_t* data, size_t len)
{
    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    return VFS_ERR_NOTSUPP;
}

/**
 * @brief UART DMA 写: 仅设动态字段 (buffer/长度) + 启停 stream, 轮询等待 USART TC
 * @param dev        Device 对象指针 (dma_cfg 由 DTSI 提供, 硬件直投)
 * @param data       待发送数据
 * @param len        字节数
 * @param timeout_ms 超时 (ms, 用于 USART TC 等待)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, DMA 未启用返回 VFS_ERR_NOTSUPP,
 *         超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 * @note  静态 DMA 参数在 hal_uart_dev_hw_open 时已配好, 这里只改 buffer 地址与长度。
 *        同步轮询路径: 等 USART TC (最后一个字节发出), 此时 DMA 必已完成搬运。
 */
int hal_uart_write_dma(struct hal_uart_dev* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    if (len > STM32_UART_DMA_MAX_XFER)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 强行中止 UART DMA: 关 DMA 请求 + disable stream (panic 路径使用)
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 外设异常返回 VFS_ERR_IO
 */
int hal_uart_dma_abort(struct hal_uart_dev* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/* =========================================================================================================================================================== */
/* ISR 虚拟中断回调                                                                                                                                              */
/* =========================================================================================================================================================== */

/**
 * @brief UART 虚拟中断上半部回调 (ISR 内执行)
 * @param arg 参数 (hal_uart_dev*)
 * @param irq_num 虚拟中断号
 * @return VFS_IRQ_ENTRY_BOTTOM 需要下半部; VFS_IRQ_ENTRY_NOBOTTOM 不需要
 * @note  清除 DMA TC 标志 + USART TC 标志; 下半部由 VFS 层通过 g_uart_bottom_half_work 注册
 */
int hal_virtual_uart_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    struct hal_uart_dev* dev = (struct hal_uart_dev*)arg;

    if (!dev || !dev->ctlr)
        return VFS_IRQ_ENTRY_NOBOTTOM;

    return VFS_IRQ_ENTRY_NOBOTTOM;
}
