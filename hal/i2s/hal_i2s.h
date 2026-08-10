/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file        hal_i2s.h
 * @brief       I2S HAL — 音频总线硬件抽象 (挂在 SPI 外设上)
 * @note        sync: poll / DMA NORMAL(TC 轮询) / AUTO
 * @note        circular + fifo_spsc; HT/TC / async 控制面走 ioctl
 * @note        虚拟中断对齐 ADC: ISR 清标志+dispatch; 上半部返回 BOTTOM; 下半部占位
 * @note        文件约定: 返回值用 int + status.h 错误码; 禁止 enum
 */
#ifndef HAL_I2S_H
#define HAL_I2S_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct fifo_spsc;

#ifndef HAL_I2S_HOST_MAX
#define HAL_I2S_HOST_MAX 3
#endif
#ifndef HAL_I2S_MAX_XFER
#define HAL_I2S_MAX_XFER 2048U
#endif

#define HAL_I2S_BUS_ROLE_SLAVE 0
#define HAL_I2S_BUS_ROLE_MASTER 1

#define HAL_I2S_XFER_AUTO 0U /**< 有 DMA 用 DMA, 否则 poll */
#define HAL_I2S_XFER_POLL 1U /**< 强制 CPU 轮询 */
#define HAL_I2S_XFER_DMA 2U /**< 强制 DMA NORMAL + TC 轮询 */

/** DMA 中断事件模式 (ioctl 配置; circular/async 用) */
#define HAL_I2S_IRQ_NONE 0U /**< 不使能 DMA HT/TC IT */
#define HAL_I2S_IRQ_TC 1U /**< 仅 Transfer Complete */
#define HAL_I2S_IRQ_HT 2U /**< 仅 Half Transfer */
#define HAL_I2S_IRQ_HT_TC 3U /**< HT + TC */

    struct hal_i2s_dev;
    typedef void (*hal_i2s_callback_t)(struct hal_i2s_dev* pdev, const void* trans, void* userdata);

    /**
     * @brief I2S 引脚配置 (DTSI 直投 LL/HAL 宏值)
     */
    struct hal_i2s_pin_cfg
    {
        uintptr_t port; /**< GPIOx_BASE */
        uint16_t pin; /**< GPIO_PIN_x */
        uint32_t clk_bus; /**< LL_AHB1_GRP1_PERIPH_GPIOx */
        uint32_t af; /**< GPIO_AFx_SPIy */
        uint32_t speed; /**< LL_GPIO_SPEED_*; 0=默认 HIGH */
        uint32_t mode; /**< LL_GPIO_MODE_*; 0=默认 ALTERNATE */
        uint32_t pull; /**< LL_GPIO_PULL_*; 0=默认 NO */
    };

    /**
     * @brief I2S DMA 流配置 (TX/RX 各一份)
     */
    struct hal_i2s_dma_config
    {
        uint32_t dma_enable; /**< 1=该方向启用 DMA */
        uintptr_t dma_handle; /**< DMAx_BASE */
        uint32_t dma_stream; /**< LL_DMA_STREAM_n */
        uint32_t dma_channel; /**< LL_DMA_CHANNEL_n */
        uint32_t dma_priority; /**< LL_DMA_PRIORITY_* */
        uint32_t dma_memory_size; /**< LL_DMA_MDATAALIGN_* */
        uint32_t dma_mode; /**< LL_DMA_MODE_NORMAL / CIRCULAR */
        uint32_t dma_periph_inc; /**< LL_DMA_PERIPH_*INCREMENT */
        uint32_t dma_mem_inc; /**< LL_DMA_MEMORY_*INCREMENT */
        uint32_t dma_periph_data_size; /**< LL_DMA_PDATAALIGN_* */
        uint32_t dma_fifo_mode; /**< DMA FIFO 模式 */
        uint32_t dma_fifo_threshold; /**< DMA FIFO 阈值 */
        uint32_t dma_mem_burst; /**< DMA 内存突发 */
        uint32_t dma_periph_burst; /**< DMA 外设突发 */
    };

    /**
     * @brief I2S host 总线配置 (VFS 从 DTSI 填充)
     */
    struct hal_i2s_bus_config
    {
        uintptr_t spi; /**< SPIx_BASE (I2S 挂在 SPI 上) */
        uint32_t spi_clk_periph; /**< LL_APBx_GRPy_PERIPH_SPIx */
        uint32_t bus_role; /**< MASTER / SLAVE */
        size_t max_transfer_sz; /**< 单次最大采样数; 0=用 HAL_I2S_MAX_XFER */
        struct hal_i2s_pin_cfg ws; /**< WS (Word Select) 引脚 */
        struct hal_i2s_pin_cfg ck; /**< CK (Clock) 引脚 */
        struct hal_i2s_pin_cfg sd; /**< SD (Serial Data) 引脚 */
        struct hal_i2s_pin_cfg mck; /**< 可选 MCLK 引脚 */
        struct hal_i2s_dma_config dma_tx; /**< TX DMA 配置 */
        struct hal_i2s_dma_config dma_rx; /**< RX DMA 配置 */
        struct fifo_spsc* circ_fifo; /**< circular 共用环缓; VFS 绑定 */
        int32_t irqn; /**< DMA TX IRQn; <0 不使能 NVIC */
        int32_t irqn_rx; /**< DMA RX IRQn; <0 不使能 NVIC */
        uint32_t irq_priority; /**< NVIC 抢占优先级 */
        uint32_t it_enable; /**< 1=open 时注册 VIRQ 并 hw_enable */
    };

    /**
     * @brief I2S 设备侧参数 (client DTSI)
     */
    struct hal_i2s_device_config
    {
        uint32_t mode; /**< LL_I2S_MODE_* */
        uint32_t standard; /**< LL_I2S_STANDARD_* */
        uint32_t data_format; /**< LL_I2S_DATAFORMAT_* */
        uint32_t mclk_output; /**< LL_I2S_MCLK_OUTPUT_* */
        uint32_t audio_freq; /**< LL_I2S_AUDIOFREQ_* */
        uint32_t cpollarity; /**< LL_I2S_CPOL_* */
    };

    /**
     * @brief I2S host 运行态 (嵌入 bus 层)
     */
    struct hal_i2s_bus_host
    {
        struct hal_i2s_bus_config cfg; /**< 总线配置 (DTSI 直投) */
        uintptr_t spi; /**< 缓存 cfg.spi, fast path */
        int hw_idx; /**< host 池下标, 兼 VIRQ(i2s, hw_idx) */
        bool bus_ready; /**< 总线就绪 */
        bool hw_inited; /**< 硬件已初始化 */
        int ref_count; /**< 引用计数 */
        struct hal_i2s_device_config active_cfg; /**< 当前 active 设备配置 */
        int circ_running; /**< circular DMA 是否已启动 */
        uint32_t irq_mode; /**< HAL_I2S_IRQ_*; ioctl 配置 */
    };

    /**
     * @brief I2S client 设备句柄
     */
    struct hal_i2s_dev
    {
        struct hal_i2s_bus_host* ctlr; /**< 所属 host */
        struct hal_i2s_device_config cfg; /**< 设备配置 */
        int hw_open; /**< 硬件打开计数 */
        const uint16_t* async_tx; /**< async 占位: 发送缓冲 */
        uint16_t* async_rx; /**< async 占位: 接收缓冲 */
        size_t async_samples; /**< async 采样点数 */
        hal_i2s_callback_t async_cb; /**< async 完成回调 */
        void* async_user; /**< async 用户私有数据 */
        int async_pending; /**< 1=已提交未完成 */
    };

    int hal_i2s_bus_host_init(struct hal_i2s_bus_host* host, int hw_idx,
                              const struct hal_i2s_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_bus_host_deinit(struct hal_i2s_bus_host* host) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dev_init(struct hal_i2s_dev* pdev, struct hal_i2s_bus_host* host,
                         const struct hal_i2s_device_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dev_deinit(struct hal_i2s_dev* pdev) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dev_hw_open(struct hal_i2s_dev* pdev) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dev_hw_close(struct hal_i2s_dev* pdev) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_sync(struct hal_i2s_dev* pdev, const uint16_t* tx, uint16_t* rx, size_t samples,
                     uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;

    /** @brief 设置 DMA HT/TC 中断模式 (ioctl; circular 运行中返回 BUSY) */
    int hal_i2s_set_dma_irq_mode(struct hal_i2s_dev* pdev,
                                 uint32_t irq_mode) COMPAT_WARN_UNUSED_RESULT;
    /** @brief 查询 DMA HT/TC 中断模式 */
    int hal_i2s_get_dma_irq_mode(struct hal_i2s_dev* pdev,
                                 uint32_t* irq_mode) COMPAT_WARN_UNUSED_RESULT;

    int hal_i2s_dma_circ_start(struct hal_i2s_dev* pdev, int tx_enable,
                               int rx_enable) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dma_circ_stop(struct hal_i2s_dev* pdev) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dma_circ_write(struct hal_i2s_dev* pdev, const uint16_t* data,
                               uint32_t samples) COMPAT_WARN_UNUSED_RESULT;
    int hal_i2s_dma_circ_read(struct hal_i2s_dev* pdev, uint16_t* data,
                              uint32_t samples) COMPAT_WARN_UNUSED_RESULT;

    /** @brief 异步传输 — 参数存档占位; DMA+IT 启动后续补 */
    int hal_i2s_transfer_async(struct hal_i2s_dev* pdev, const uint16_t* tx, uint16_t* rx,
                               size_t samples, hal_i2s_callback_t cb,
                               void* userdata) COMPAT_WARN_UNUSED_RESULT;
    /** @brief 轮询异步完成 — 占位 */
    int hal_i2s_transfer_poll(struct hal_i2s_dev* pdev,
                              uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif
#endif
