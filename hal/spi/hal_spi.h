/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SPI HAL 层 — 硬件抽象接口 (跨平台统一头)
 *
 * 设计: 硬件直投, DTSI 提供厂商宏值, HAL 零翻译透传给 LL 库/ESP-IDF driver。
 * - STM32: spi-base = <SPI1_BASE>, spi-clk = <LL_APB2_GRP1_PERIPH_SPI1>,
 *   mosi-port = <GPIOA_BASE>, mosi-pin = <GPIO_PIN_7>,
 *   mosi-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>, mosi-af = <GPIO_AF5_SPI1>
 * - WCH: spi-base = <SPI1_BASE>, spi-clk = <RCC_APB2Periph_SPI1>,
 *   mosi-port = <GPIOA_BASE>, mosi-pin = <GPIO_Pin_7>,
 *   mosi-clk  = <RCC_APB2Periph_GPIOA>, mosi-af = <GPIO_Mode_AF_PP>
 *   (WCH 的 af 字段承载 GPIOMode_TypeDef, mode+af 编码在一起)
 * - ESP32: spi-base = <SPI3_HOST>, spi-clk = <0>,
 *   mosi-port = <0>, mosi-pin = <11>, mosi-clk = <0>, mosi-af = <0>
 *   (ESP32 无 port/clk/af 概念, pin 为 SoC GPIO 编号)
 * - hal_spi_bus_host 嵌入 bus 层 spi_bus_host (非指针), HAL 无池管理
 * - STM32/WCH: slave/async 返回 VFS_ERR_NOTSUPP
 * - ESP32: 支持 async 传输 (spi_device_queue_trans + post_cb ISR callback)
 *
 * 头中立化: 本头不暴露任何 vendor 类型, 只用 uintptr_t/int/void*。
 * vendor 头由 hal_spi_*.c 内部 include。
 */
#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "compiler_compat.h"
#include "VFS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

#ifndef HAL_SPI_MAX_TRANSFER_BYTES
#define HAL_SPI_MAX_TRANSFER_BYTES  2048
#endif

/* 最大 host 数量 (per-host dummy buffer 防 DMA 踩踏) */
#ifndef HAL_SPI_HOST_MAX
#define HAL_SPI_HOST_MAX  4
#endif

#ifndef HAL_SPI_MAX_XFER
#define HAL_SPI_MAX_XFER  512U
#endif

/* 每个 master device 最大并发 async transfer 数 (ESP32 用) */
#ifndef HAL_SPI_MAX_ASYNC
#define HAL_SPI_MAX_ASYNC  4
#endif

/* forward decl */
struct hal_spi_dev;
struct bus_dma_chan;

/**
 * @brief SPI 传输完成 callback (ISR 上下文)
 * @note  STM32/WCH 不支持 async, 类型签名保持跨平台一致。
 *        ESP32 此 callback 在 DMA done 中断中调用, 严禁阻塞/睡眠/调 transfer。
 */
typedef void (*hal_spi_callback_t)(struct hal_spi_dev* dev,
                                   const void* trans, void* userdata);

/*============================================================================*/
/*                              引脚配置 (硬件直投)                            */
/*============================================================================*/
/* 纯数据实体: 所有字段由 DTSI 提供厂商宏值, HAL 零计算直接灌入 LL 库/标准外设库。
 * MOSI/MISO/SCLK 用此结构体 (含 af), CS 仅用 port/pin/clk_periph。
 * - STM32: af = GPIO_AF5_SPI1 等 (LL_GPIO AF 选择)
 * - WCH: af = GPIO_Mode_AF_PP 等 (GPIOMode_TypeDef, mode+af 编码在一起)
 * - ESP32: port=0, clk_periph=0, af=0, pin=SoC GPIO 编号 (无 AF 概念)
 */
struct hal_spi_pin_cfg
{
    uintptr_t port;
    uint16_t  pin;
    uint32_t  clk_periph;
    uint32_t  af;
};

/*============================================================================*/
/*                              Bus / Device 配置                             */
/*============================================================================*/
struct hal_spi_bus_config
{
    uintptr_t              spi;          /* STM32/WCH: SPI 基地址; ESP32: (uintptr_t)host_id */
    uint32_t               spi_clk_periph;
    struct hal_spi_pin_cfg mosi;
    struct hal_spi_pin_cfg miso;
    struct hal_spi_pin_cfg sclk;
    int                    max_transfer_sz;
    int                    dma_chan;
    int                    bus_role;
};

struct hal_spi_device_config
{
    int             mode;
    int             clock_speed_hz;
    /* CS 引脚: 用于配置变更检测 (多设备共线时不打架) */
    uintptr_t       cs_port;        /* ESP32: 0 */
    uint16_t        cs_pin;         /* ESP32: SoC GPIO 编号 */
    uint32_t        cs_clk_periph;  /* ESP32: 0 */
    int             queue_size;
};

/*============================================================================*/
/*                              HAL 对象 (嵌入 bus 层)                         */
/*============================================================================*/
/* 纯数据实体, 嵌入 bus 层 spi_bus_host (非指针), HAL 无池管理无 alloc/free。
 * 扁平字段替代旧 hw_priv_storage + hal_spi_*_priv 二级包装。
 *
 * 跨平台字段说明:
 * - spi: STM32/WCH 缓存 cfg.spi (fast path); ESP32 缓存 (uintptr_t)host_id
 * - sync_sem: STM32 DMA 同步信号量; WCH/ESP32 为 NULL
 * - hw_idx: STM32/WCH dummy buffer 索引; ESP32 device HW slot 索引
 */
struct hal_spi_bus_host
{
    struct hal_spi_bus_config       cfg;
    struct hal_spi_device_config    active_cfg;   /* 当前生效的 device 配置 */
    uintptr_t                       spi;          /* 缓存 cfg.spi, fast path */
    struct osal_sem*                sync_sem;     /* DMA 同步信号量 (STM32 用, 其他 NULL) */
    int                             hw_idx;       /* dummy buffer / HW slot 索引 */
    int                             ref_count;
    bool                            bus_ready;
    bool                            hw_inited;
};

struct hal_spi_dev
{
    struct hal_spi_bus_host*        ctlr;
    struct hal_spi_device_config    cfg;
    int                             pool_idx;
    int                             hw_open;
};

typedef struct hal_spi_device_config spi_device_config;
typedef struct hal_spi_bus_config    spi_bus_config;
typedef struct hal_spi_bus_host      spi_controller;
typedef struct hal_spi_dev           spi_device;

/*============================================================================*/
/*                              Host 管理 API                                 */
/*============================================================================*/
/* 对象由 bus 层提供 (嵌入), HAL 不做池管理。hw_idx 为 dummy buffer/HW slot 索引。 */
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host) COMPAT_WARN_UNUSED_RESULT;

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg);
int hal_spi_dev_hw_open(struct hal_spi_dev* dev) COMPAT_WARN_UNUSED_RESULT;
int hal_spi_dev_hw_close(struct hal_spi_dev* dev) COMPAT_WARN_UNUSED_RESULT;

/*============================================================================*/
/*                              同步传输 (Master)                             */
/*============================================================================*/
int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 同步传输 inline 包装: 转调 spi_sync
 * @param dev        SPI 设备对象指针
 * @param tx         发送缓冲区 (可为 NULL, 内部填 0xFF/dummy)
 * @param rx         接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len        传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL 或 VFS_ERR_TIMEOUT
 */
static inline int hal_spi_transfer(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                                   size_t len, uint32_t timeout_ms)
{
    return spi_sync(dev, tx, rx, len, timeout_ms);
}

/*============================================================================*/
/*                              异步传输 (ESP32 支持, 其他返回 NOTSUPP)        */
/*============================================================================*/
int hal_spi_transfer_async(struct hal_spi_dev* dev,
                           const uint8_t* tx, uint8_t* rx,
                           size_t len, hal_spi_callback_t cb,
                           void* userdata);

int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms);

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms);

/*============================================================================*/
/*                              Slave 传输 (ESP32 支持, 其他返回 NOTSUPP)      */
/*============================================================================*/
int spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms);

int spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms);

/*============================================================================*/
/*                              DMA 强制中止 (panic/reboot 路径)              */
/*============================================================================*/
/* 强行中止 SPI 上的 DMA (panic/reboot 全局入口, hal_dma_force_stop 调用)。
 * STM32: 空实现 (STM32 hal_dma_force_stop 直接 abort DMA stream, 不需 SPI 介入);
 * WCH:   中止 SPI1 DMA (WCH DMA 通道由 SPI HAL 管理, 需 SPI 介入);
 * ESP32: 空实现 (ESP32 GDMA 自动管理, 无需介入)。 */
void hal_spi_dma_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */
