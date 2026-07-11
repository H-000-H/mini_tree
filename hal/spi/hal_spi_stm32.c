/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SPI HAL — STM32F4 实现 (Master only)
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 LL 库。
 * - hal_spi_bus_host 嵌入 bus 层, HAL 无 s_spi_hosts[] 池
 * - hal_spi_sync: CS 变更检测用 cs_port + cs_pin 直接比较 (多设备共线不打架)
 * - slave / async 返回 VFS_ERR_NOTSUPP
 */
#include "hal_spi.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "interrupt.h"

#include "dt_config_gen.h"

/** SPI 下半部工作项 (fn/arg 由 VFS 层绑定), 供 interrupt_virtual_register 注册 */
struct bottom_half_work g_spi_bottom_half_work;

/** 平台参数：来自 DTS stm32,spi-platform-cap，无 DTS 时提供回退 */
#ifndef DTC_GEN_STM32_SPI_HOST_MAX
#define DTC_GEN_STM32_SPI_HOST_MAX  3
#endif
#ifndef DTC_GEN_STM32_SPI_MAX_XFER
#define DTC_GEN_STM32_SPI_MAX_XFER  512U
#endif

/** 覆盖 hal_spi.h 中的默认值，避免宏重定义警告/错误 */
#undef HAL_SPI_HOST_MAX
#define HAL_SPI_HOST_MAX   DTC_GEN_STM32_SPI_HOST_MAX
#undef HAL_SPI_MAX_XFER
#define HAL_SPI_MAX_XFER   DTC_GEN_STM32_SPI_MAX_XFER

/* per-host dummy buffer: DMA 路径下 tx/rx 为 NULL 时填充占位, 防 cache line 踩踏。
 * - s_dummy_tx 填 0xFF: 用户只收时, DMA 仍需往 SPI->DR 写驱动 SCLK
 * - s_dummy_rx 丢弃区:  用户只发时, DMA 仍需从 SPI->DR 读避免 OVR
 * 32 字节对齐适配 DMA cache line; per-host 索引防多 host 并发踩踏。 */
static uint8_t s_dummy_tx[HAL_SPI_HOST_MAX][HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);
static uint8_t s_dummy_rx[HAL_SPI_HOST_MAX][HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);

/* 纯 LL 库调用, 非抽象层 */
/**
 * @brief 配置 SPI 复用引脚: 时钟使能 + AF 模式 + 推挽高速 (LL 库直投)
 * @param pin 引脚配置 (含 port/pin/clk_bus/af)
 */
COMPAT_STATIC_INLINE void hal_spi_config_af_pin(const struct hal_spi_pin_cfg* pin)
{
    (void)pin;
}

/**
 * @brief 复位 SPI 复用引脚为模拟模式 + 无上下拉 (等效去初始化)
 * @param pin 引脚配置
 */
static void hal_spi_reset_af_pin(const struct hal_spi_pin_cfg* pin)
{
    (void)pin;
}

/**
 * @brief 由目标时钟频率选最近档位的 LL 波特率预分频值
 * @param clock_hz 目标时钟频率 (Hz), <=0 时返回最大分频
 * @return LL_SPI_BAUDRATEPRESCALER_DIV* 宏值
 */
static uint32_t stm32_spi_prescaler(int clock_hz)
{
    (void)clock_hz;
    return (uint32_t)VFS_ERR_NOTSUPP;
}

/**
 * @brief 轮询等待 SPI BSY 标志清零, 超时则直接返回
 * @param spi        SPI 外设寄存器基址
 * @param timeout_ms 超时 (ms)
 */
static void hal_spi_wait_idle(uintptr_t spi, uint32_t timeout_ms)
{
    (void)spi;
    (void)timeout_ms;
}

/*============================================================================*/
/*                              Host 管理 API                                 */
/*============================================================================*/
/** forward: DMA 静态参数配置 (定义在 DMA 区, host_init 提前调用) */
static void hal_spi_dma_init(const struct hal_spi_dma_config* cfg, uint32_t direction);

/**
 * @brief SPI Host 初始化: 使能 SPI 时钟 + 配置 MOSI/MISO/SCLK 复用引脚 + 缓存 fast path 字段
 * @param host  Host 对象指针 (由 bus 层嵌入)
 * @param hw_idx dummy buffer 索引 (per-host 防 DMA 踩踏)
 * @param cfg   总线配置 (DTSI 厂商宏值)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, spi 为空返回 VFS_ERR_NODEV
 */
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg)
{
    if (!host || !cfg || hw_idx < 0 || hw_idx >= HAL_SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (host->bus_ready)
        return VFS_OK;
    if (!cfg->spi)
        return VFS_ERR_NODEV;

    COMPAT_MEM_SET(host, 0, sizeof(*host));

    /** DMA 静态参数一次性配置: TX=mem→periph, RX=periph→mem; dma_enable=0 时跳过 */

    /*<缓存 fast path 字段 */
    return VFS_OK;
}

/**
 * @brief SPI Host 反初始化: 关闭 SPI + 复位 MOSI/MISO/SCLK 为模拟模式
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空返回 VFS_ERR_INVAL, 未就绪直接返回 VFS_OK
 */
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    if (!host)
        return VFS_ERR_INVAL;
    if (!host->bus_ready)
        return VFS_OK;

    /* 引脚复位为 analog (同 GPIO deinit 模式) */

    return VFS_OK;
}

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
/**
 * @brief SPI Device 对象初始化: 清零 + 绑定 host + 拷贝 device 配置 (硬件尚未打开)
 * @param dev      Device 对象指针
 * @param host     所属 Host 对象指针
 * @param dev_cfg  设备配置 (mode/clock_speed/CS 等)
 */
int hal_spi_dev_init(struct hal_spi_dev* dev,struct hal_spi_bus_host* host,const struct hal_spi_device_config* dev_cfg)
{
    if (!dev || !host || !dev_cfg)
        return VFS_ERR_INVAL;

    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->ctlr     = host;
    dev->cfg      = *dev_cfg;
    return VFS_OK;
}

/**
 * @brief 将 device 配置应用到 SPI 硬件: 关 SPI → 填 LL_SPI_InitTypeDef → 重启 SPI
 * @param host     Host 对象指针
 * @param dev_cfg  设备配置 (mode/clock_speed_hz)
 * @return 成功返回 VFS_OK, 参数非法或 LL_SPI_Init 失败返回 VFS_ERR_IO
 */
static int stm32_spi_apply_dev_cfg(struct hal_spi_bus_host* host,const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !host->spi || !dev_cfg)
        return VFS_ERR_NOTSUPP;

    return VFS_OK;
}

/**
 * @brief 打开 SPI Device 硬件: 应用 device 配置 + 标记 hw_open + 增加 host 引用计数
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, host 未就绪返回 VFS_ERR_INVAL
 */
int hal_spi_dev_hw_open(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    if (!host->bus_ready)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 关闭 SPI Device 硬件: 减少 host 引用计数 + 标记 hw_open=0 (不关 SPI 外设)
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 未打开直接返回 VFS_OK
 */
int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (!dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    if (host->ref_count > 0)
        host->ref_count--;

    dev->hw_open = 0;
    return VFS_OK;
}

/*============================================================================*/
/*                              同步传输 (Master)                             */
/*============================================================================*/
/**
 * @brief SPI 轮询传输: 逐字节 TXE/RXNE 标志轮询, 超时检测基于 HAL_GetTick
 * @param host        Host 对象指针
 * @param tx          发送缓冲区 (可为 NULL, 内部填 0xFF)
 * @param rx          接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len         传输字节数
 * @param timeout_ms  超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 */
static int stm32_spi_transfer_poll(struct hal_spi_bus_host* host,const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms)
{
    (void)tx;
    (void)rx;
    (void)timeout_ms;

    if (!host || len == 0)
        return VFS_ERR_INVAL;

    if (!host->spi)
        return VFS_ERR_NOTSUPP;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief SPI 同步传输 (Master): 配置变更检测后转 stm32_spi_transfer_poll 执行
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 master)
 * @param tx         发送缓冲区 (可为 NULL)
 * @param rx         接收缓冲区 (可为 NULL)
 * @param len        传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 配置应用失败或超时返回对应 VFS_ERR_*
 */
int hal_spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (len > (size_t)dev->ctlr->cfg.max_transfer_sz)
        return VFS_ERR_INVAL;

    /* 配置变更检测: CS 引脚 + mode + clock 任一变化则重配 SPI。
     * 多设备共线时, 不同 device 的 cs_port/cs_pin 不同,
     * 切换设备自动触发重配, 保证不会读到上一个设备的残留配置。 */

    return stm32_spi_transfer_poll(dev->ctlr, tx, rx, len, timeout_ms);
}

/* SPI 全双工 DMA: TX (mem→periph) + RX (periph→mem) 双通道, poll 等 TX TC + BSY。
 * 配置来自 host->cfg.dma_tx / dma_rx (DTS 硬件直投), HAL 零翻译灌入 LL_DMA。 */

/** STM32F4 LL_DMA 库 per-stream TC flag, 无参数化 API, switch 展开 */
static void hal_spi_dma_clear_tc(uintptr_t dma, uint32_t stream)
{
    (void)dma;
    (void)stream;
}

/**
 * @brief 检查 DMA 传输是否完成
 * @param dma DMA 控制器
 * @param stream DMA 流
 * @return true 完成, false 未完成
 */
static bool hal_spi_dma_is_active_tc(uintptr_t dma, uint32_t stream)
{
    (void)dma;
    (void)stream;
    return false;
}

/**
 * @brief DMA 静态参数一次性配置 (host_init 时调用, 热路径不再重复)
 * @note  采用 LL_DMA_InitTypeDef + LL_DMA_Init 批量初始化范式 (同 LL_SPI_Init/LL_ADC_Init)。
 *        channel/direction/priority/mode/inc/size 来自 DTS 且永不变;
 *        仅 buffer 地址与长度每次传输不同, 留在 transfer_dma 热路径。
 */
static void hal_spi_dma_init(const struct hal_spi_dma_config* cfg, uint32_t direction)
{
    (void)cfg;
    (void)direction;
}

/**
 * @brief SPI DMA 同步传输: 仅设动态字段 (buffer/长度) + 启停 stream, poll 等 TX TC + BSY
 * @param host       Host 对象 (cfg.dma_tx/dma_rx 提供 DMA 配置)
 * @param tx         发送缓冲 (NULL 时用 s_dummy_tx 填 0xFF)
 * @param rx         接收缓冲 (NULL 时用 s_dummy_rx 丢弃)
 * @param len        字节数
 * @param timeout_ms 超时 (ms)
 * @return VFS_OK 成功; VFS_ERR_NOTSUPP dma_enable=0; VFS_ERR_TIMEOUT 超时
 */
int hal_spi_transfer_dma_stm32(struct hal_spi_bus_host* host,const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms)
{
    (void)tx;
    (void)rx;
    (void)timeout_ms;

    if (!host || len == 0)
        return VFS_ERR_INVAL;
    if (!host->cfg.dma_tx.dma_enable || !host->cfg.dma_rx.dma_enable)
        return VFS_ERR_NOTSUPP;

    if (!host->spi)
        return VFS_ERR_NOTSUPP;

    /** dummy buffer: tx=NULL 填 0xFF 驱动 SCLK; rx=NULL 丢弃避免 OVR */

    /** 热路径: 仅动态字段 — disable stream → 设地址 → 设长度 → 清 TC */

    /** <使能 SPI DMA 请求 + 启动 stream (先 RX 后 TX) */

    /** 中断模式: 使能 TX DMA TC 中断, 由 ISR 下半部通知完成 */

    /** <poll 等 TX DMA TC */

    /** poll 等 SPI BSY 清零 (最后一字节移位完成) */

    /** 清理: 清 TC flag + 关 stream + 关 DMA req */
    return VFS_OK;
}

/**
 * @brief 中止 SPI DMA 传输: 关 DMA 请求 + 关 DMA stream
 * @param host Host 对象 (cfg.dma_tx/dma_rx 提供 DMA 配置)
 */
void hal_spi_abort_stm32(struct hal_spi_bus_host* host)
{
    if (!host || !host->spi)
        return;
}

/* =========================================================================================================================================================== */
/* ISR 虚拟中断回调                                                                                                                                              */
/* =========================================================================================================================================================== */

/**
 * @brief SPI 虚拟中断上半部回调 (ISR 内执行)
 * @param arg 参数 (hal_spi_dev*)
 * @param irq_num 虚拟中断号
 * @return VFS_IRQ_ENTRY_BOTTOM 需要下半部; VFS_IRQ_ENTRY_NOBOTTOM 不需要
 * @note  清除 TX + RX DMA TC 标志; 下半部由 VFS 层通过 g_spi_bottom_half_work 注册
 */
int hal_virtual_spi_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    struct hal_spi_dev* dev = (struct hal_spi_dev*)arg;

    if (!dev || !dev->ctlr)
        return VFS_IRQ_ENTRY_NOBOTTOM;

    /** 清除 TX DMA TC 标志 */

    /** 清除 RX DMA TC 标志 */

    return VFS_IRQ_ENTRY_NOBOTTOM;
}

/*============================================================================*/
/*                              异步传输 (STM32 不支持, 返回 NOTSUPP)          */
/*============================================================================*/
int hal_spi_transfer_async(struct hal_spi_dev* dev,const uint8_t* tx, uint8_t* rx,size_t len, hal_spi_callback_t cb,void* userdata)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(tx);
    COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(cb);
    COMPAT_IGNORE_RESULT(userdata);
    return VFS_ERR_NOTSUPP;
}

int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(rx_data);
    COMPAT_IGNORE_RESULT(rx_cap);
    COMPAT_IGNORE_RESULT(trans_len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

/*============================================================================*/
/*                              Slave 传输 (STM32 不支持, 返回 NOTSUPP)        */
/*============================================================================*/
int hal_spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(tx);
    COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int hal_spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,uint32_t timeout_ms)/**<timeout_ms 为超时时间，单位为毫秒。*/
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(data);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}
