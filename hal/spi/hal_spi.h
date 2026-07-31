/** 
 * @license: SPDX-License-Identifier: Apache-2.0 
 * @file: hal_spi.h
 * @brief: SPI HAL 层 — 硬件抽象接口,硬件直投层
 * @note 所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note 由于SPI是快速热路径外设所以SPI的初始化与配置应该尽量在硬件直投层完成
 * @note 文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 接收的参数必须为指针，并且必须为合法的指针，不能为空指针
 * @note 禁止使用enum,enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
 */
#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "compiler_compat.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0 /**< 从机角色 */
#define HAL_SPI_BUS_ROLE_MASTER 1 /**< 主机角色 */

/** 同步传输路径选择 (0=隐式, 兼容零初始化) */
#define HAL_SPI_XFER_AUTO  0U /**< 隐式: DMA 可用则 DMA, 否则 poll */
#define HAL_SPI_XFER_POLL  1U /**< 强制 CPU poll */
#define HAL_SPI_XFER_DMA   2U /**< 强制 DMA, 不可用则返回 NOTSUPP */

#ifndef HAL_SPI_MAX_TRANSFER_BYTES
#define HAL_SPI_MAX_TRANSFER_BYTES  2048 /**< 历史/平台上限占位; STM32 路径实际以 HAL_SPI_MAX_XFER 为准 */
#endif

#ifndef HAL_SPI_HOST_MAX
#define HAL_SPI_HOST_MAX  4 /**< 最大 host 数量 (per-host dummy buffer 防 DMA 踩踏) */
#endif

#define HAL_SPI_MAX_XFER  512U /**< 单次传输最大字节数 (dummy buffer / len 上限) */
#define HAL_SPI_MAX_ASYNC  4 /**< 每个 master device 最大并发 async transfer 数 */

/**
 * @brief SPI 设备对象
 * @param ctlr SPI 控制器对象指针
 * @param cfg SPI 设备配置
 * @param hw_open 设备是否打开
 */
struct hal_spi_dev;

/**
 * @brief SPI DMA 配置 (硬件直投, 仿 ADC/UART)
 * @note  纯数据实体: 所有字段由 DTSI 提供厂商宏值, HAL 零计算直接灌入 LL_DMA。
 *        SPI 全双工需要 TX + RX 两个独立 DMA 流, 各自配置。
 */
struct hal_spi_dma_config
{
    uint32_t  dma_enable;        /**< DMA 使能: 0=禁用, 1=启用 */
    uintptr_t dma_handle;        /**< DMA 控制器寄存器基址 (DMA1_BASE / DMA2_BASE) */
    uint32_t  dma_stream;        /**< DMA 流编号 (LL_DMA_STREAM_0..7) */
    uint32_t  dma_channel;       /**< DMA 通道编号 (LL_DMA_CHANNEL_0..7) */
    uint32_t  dma_priority;      /**< DMA 优先级 */
    uint32_t  dma_memory_size;   /**< DMA 内存数据宽度 */
    uint32_t  dma_mode;          /**< DMA 模式 (LL_DMA_MODE_NORMAL/CIRCULAR) */
    uint32_t  dma_periph_inc;    /**< DMA 外设地址递增 (LL_DMA_PERIPH_INCREMENT/NOINCREMENT) */
    uint32_t  dma_mem_inc;       /**< DMA 内存地址递增 (LL_DMA_MEMORY_INCREMENT/NOINCREMENT) */
    uint32_t  dma_periph_data_size; /**< DMA 外设数据宽度 (LL_DMA_PDATAALIGN_BYTE/HALFWORD/WORD) */
    uint32_t  dma_fifo_mode;     /**< DMA FIFO 模式 (LL_DMA_FIFOMODE_ENABLE/DISABLE) */
    uint32_t  dma_fifo_threshold; /**< DMA FIFO 阈值 (LL_DMA_FIFOTHRESHOLD_1_4/1_2/3_4/FULL) */
    uint32_t  dma_mem_burst;     /**< DMA 内存突发 (LL_DMA_MBURST_SINGLE/INCR4/INCR8/INCR16) */
    uint32_t  dma_periph_burst;  /**< DMA 外设突发 (LL_DMA_PBURST_SINGLE/INCR4/INCR8/INCR16) */
};

/**
 * @brief SPI 传输完成 callback (ISR 上下文)
 * @note  STM32/WCH 不支持 async, 类型签名保持跨平台一致。
 * @param dev SPI 设备对象指针
 * @param trans 传输数据
 * @param userdata 用户数据指针
 */
typedef void (*hal_spi_callback_t)(struct hal_spi_dev* dev,const void* trans, void* userdata);

/**
 * @brief SPI 引脚配置
 * @param port 端口
 * @param pin 引脚
 * @param clk_bus 该引脚所属的外设时钟总线
 * @param af 复用功能,用于配置引脚的复用功能
 * @param output_type 引脚输出类型
 * @param speed 引脚速度
 * @param mode 引脚模式
 * @param pull 引脚上拉/下拉
 */
struct hal_spi_pin_cfg
{
    uintptr_t port;/**< 端口 */
    uint16_t  pin;/**< 引脚 */
    uint32_t  clk_bus;/**< 该引脚所属的外设时钟总线 */
    uint32_t  af;/**< 复用功能 */
    uint32_t  output_type;/**< 引脚输出类型 */
    uint32_t  speed;/**< 引脚速度 */
    uint32_t  mode;/**< 引脚模式 */
    uint32_t  pull;/**< 引脚上拉/下拉 */
};

/**
 * @brief SPI 总线配置
 * @param spi SPI 基地址
 * @param spi_clk_periph RCC 外设时钟使能位 (LL_APBx_GRPy_PERIPH_SPIx)
 * @param mosi MOSI 引脚配置
 * @param miso MISO 引脚配置
 * @param sclk SCLK 引脚配置
 * @param max_transfer_sz 最大传输字节数
 * @param dma_tx TX DMA 配置 (硬件直投, 仿 ADC)
 * @param dma_rx RX DMA 配置 (硬件直投, 仿 ADC)
 * @param bus_role 总线角色
 */
struct hal_spi_bus_config
{
    uintptr_t               spi;            /**< SPI 基地址 */
    uint32_t                spi_clk_periph; /**< RCC 外设时钟使能位 (LL_APBx_GRPy_PERIPH_SPIx) */
    int32_t                 irqn;           /**< NVIC 中断号 (DTS irqn, -1 = 无中断) */
    uint32_t                irq_priority;   /**< NVIC 中断优先级 (DTS irq-priority, 0=最高) */
    uint32_t                it_enable;      /**< 中断模式使能: 0=禁用, 1=启用 DMA TC/NVIC 中断 */
    struct hal_spi_pin_cfg  mosi;           /**< MOSI 引脚配置 */
    struct hal_spi_pin_cfg  miso;           /**< MISO 引脚配置 */
    struct hal_spi_pin_cfg  sclk;           /**< SCLK 引脚配置 */
    size_t                  max_transfer_sz;/**< 最大传输字节数 */
    struct hal_spi_dma_config dma_tx;      /**< TX DMA 配置 (dma_enable=0 不使用 DMA) */
    struct hal_spi_dma_config dma_rx;      /**< RX DMA 配置 (dma_enable=0 不使用 DMA) */
    int                     bus_role;       /**< 总线角色 */
};

/**
 * @brief SPI 设备配置
 * @param mode 模式
 * @param clock_speed_hz 时钟速度
 * @param cs_port CS 引脚端口
 * @param cs_pin CS 引脚
 * @param cs_clk_periph CS 引脚所属 GPIO 的 RCC 时钟使能位
 */
struct hal_spi_device_config
{
    int             mode;               /**< 模式 (CPOL/CPHA: bit1=CPOL, bit0=CPHA) */
    uint32_t        clock_speed_hz;     /**< 时钟速度 (Hz) */
    uintptr_t       cs_port;            /**< CS 引脚端口 */
    int32_t         cs_pin;             /**< CS 引脚; -1 = 无硬件 CS (spics 禁用) */
    uint32_t        cs_clk_periph;      /**< CS 引脚时钟总线 */
    uint32_t        transfer_direction; /**< 传输方向 (LL_SPI_FULL_DUPLEX/HALF_DUPLEX_RX/TX) */
    uint32_t        data_width;         /**< 数据宽度 (LL_SPI_DATAWIDTH_8BIT/16BIT) */
    uint32_t        nss;                /**< NSS 模式 (LL_SPI_NSS_SOFT/HARD) */
    uint32_t        bit_order;          /**< 位序 (LL_SPI_MSB_FIRST/LSB_FIRST) */
    uint32_t        crc_calculation;    /**< CRC 计算 (LL_SPI_CRCCALCULATION_ENABLE/DISABLE) */
    uint32_t        crc_poly;           /**< CRC 多项式 */
    uint32_t        standard;           /**< 协议标准 (LL_SPI_PROTOCOL_MOTOROLA/TI) */
};

/**
 * @brief SPI 总线主机对象
 * @param cfg 总线配置
 * @param active_cfg 当前生效的设备配置
 * @param spi 缓存 cfg.spi, fast path
 * @param hw_idx dummy buffer / HW slot 索引
 */
struct hal_spi_bus_host
{
    struct hal_spi_bus_config       cfg;          /**< 总线配置 */
    struct hal_spi_device_config    active_cfg;   /**< 当前生效的 device 配置 */
    uintptr_t                       spi;          /**< 缓存 cfg.spi, fast path */
    int                             hw_idx;       /**< dummy buffer / HW slot 索引 */
    int                             ref_count;    /**< 引用计数 */
    bool                            bus_ready;    /**< 总线是否准备好 */
    bool                            hw_inited;    /**< 硬件是否初始化 */
};

/**
 * @brief SPI 设备对象
 * @param ctlr 总线控制器对象指针
 * @param cfg 设备配置
 * @param hw_open 设备是否打开
 */
struct hal_spi_dev
{
    struct hal_spi_bus_host*        ctlr;         /**< 总线控制器对象指针 */
    struct hal_spi_device_config    cfg;          /**< 设备配置 */
    int                             hw_open;      /**< 设备是否打开 */
};


/**
 * @brief 初始化 SPI 总线主机
 * @param host 总线主机对象指针
 * @param hw_idx dummy buffer / HW slot 索引
 * @param cfg 总线配置
 */
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,const struct hal_spi_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 释放 SPI 总线主机
 * @param host 总线主机对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 初始化 SPI 设备
 * @param dev 设备对象指针
 * @param host 总线控制器对象指针
 * @param dev_cfg 设备配置
 */
int hal_spi_dev_init(struct hal_spi_dev* dev,struct hal_spi_bus_host* host,const struct hal_spi_device_config* dev_cfg) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 打开 SPI 设备
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_dev_hw_open(struct hal_spi_dev* dev) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 关闭 SPI 设备
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_dev_hw_close(struct hal_spi_dev* dev) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 同步传输
 * @param dev 设备对象指针
 * @param tx 发送缓冲区 (可为 NULL, 内部填 0xFF/dummy)
 * @param rx 接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len 传输字节数
 * @param timeout_ms 超时 (ms)
 * @param xfer_mode HAL_SPI_XFER_AUTO / POLL / DMA
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int hal_spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 异步传输
 * @param dev 设备对象指针
 * @param tx 发送缓冲区 (可为 NULL, 内部填 0xFF/dummy)
 * @param rx 接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len 传输字节数
 * @param cb 回调函数
 * @param userdata 用户数据指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_transfer_async(struct hal_spi_dev* dev,const uint8_t* tx, uint8_t* rx,size_t len, hal_spi_callback_t cb,void* userdata) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 异步传输轮询
 * @param dev 设备对象指针
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 获取传输结果
 * @param dev 设备对象指针
 * @param rx_data 接收缓冲区
 * @param rx_cap 接收缓冲区容量
 * @param trans_len 传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,size_t* trans_len, uint32_t timeout_ms);

/**
 * @brief SPI 从机同步传输
 * @param dev 设备对象指针
 * @param tx 发送缓冲区 (可为 NULL, 内部填 0xFF/dummy)
 * @param rx 接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len 传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 从机队列传输
 * @param dev 设备对象指针
 * @param data 发送缓冲区
 * @param len 传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int hal_spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 虚拟中断上半部回调 (ISR 内执行)
 * @param arg 参数 (hal_spi_dev*)
 * @param irq_num 虚拟中断号
 * @return VFS_IRQ_ENTRY_BOTTOM 需要下半部; VFS_IRQ_ENTRY_NOBOTTOM 不需要
 * @note  清除 TX+ RX DMA TC 标志 + SPI BSY 标志; 下半部由 VFS 层通过 g_spi_bottom_half_work 注册
 */
int  hal_virtual_spi_irq_callback(void* arg, uint16_t irq_num);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */
