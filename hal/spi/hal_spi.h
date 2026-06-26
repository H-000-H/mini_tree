/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI HAL 层 — 硬件抽象接口 (STM32/CH32, Master only)
 *
 * 结构与 API 与 ESP32 hal_spi.h 对齐, 便于 bus 层统一调用。
 * slave / async 相关 API 存在但返回 VFS_ERR_NOTSUPP。
 *
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 * 需要中间缓冲的场景在 VFS 层通过 buffer_pool 处理。
 */
#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "hal_gpio.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

#ifndef HAL_SPI_MAX_TRANSFER_BYTES
#define HAL_SPI_MAX_TRANSFER_BYTES  2048
#endif

#define HAL_SPI_MAX_ASYNC           4

/* 平台私有存储大小 (足够容纳各平台 HAL 私有结构体) */
#ifndef HAL_SPI_HW_PRIV_SIZE
#define HAL_SPI_HW_PRIV_SIZE  32
#endif

/* forward decl */
struct hal_spi_dev;
struct bus_dma_chan;

/**
 * @brief SPI 传输完成 callback (ISR 上下文)
 * @note  st/ch 平台不支持 async, 此类型仅供 API 兼容。
 */
typedef void (*hal_spi_callback_t)(struct hal_spi_dev* dev,
                                   const void* trans, void* userdata);

struct hal_spi_device_config {
    int         mode;
    int         clock_speed_hz;
    hal_pin_t   cs_pin;
    int         queue_size;
};

struct hal_spi_bus_config {
    int         host_id;
    hal_pin_t   mosi;
    hal_pin_t   miso;
    hal_pin_t   sclk;
    int         max_transfer_sz;
    int         dma_chan;
    int         bus_role;
};

struct hal_spi_bus_host {
    struct hal_spi_bus_config       cfg;
    int                             ref_count;
    int                             bus_ready;
    int                             hw_inited;
    struct hal_spi_device_config    active_cfg;
    /* 平台私有存储 (全静态, 避免动态分配) */
    uint8_t                         hw_priv_storage[HAL_SPI_HW_PRIV_SIZE];
};

struct hal_spi_dev {
    struct hal_spi_bus_host*        ctlr;
    struct hal_spi_device_config    cfg;
    int                             pool_idx;
    int                             hw_open;
};

typedef struct hal_spi_device_config spi_device_config;
typedef struct hal_spi_bus_config    spi_bus_config;
typedef struct hal_spi_bus_host      spi_controller;
typedef struct hal_spi_dev           spi_device;

/* ===== Host 管理 API (与 ESP32 对齐) ===== */
int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg);
int hal_spi_bus_host_deinit(int host_id);
int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out) COMPAT_WARN_UNUSED_RESULT;

/* ===== Device 管理 API (与 ESP32 对齐) ===== */
void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg);
int hal_spi_dev_hw_open(struct hal_spi_dev* dev);
int hal_spi_dev_hw_close(struct hal_spi_dev* dev);

/* ===== 同步传输 (Master) ===== */
int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms);

static inline int hal_spi_transfer(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                                   size_t len, uint32_t timeout_ms)
{
    return spi_sync(dev, tx, rx, len, timeout_ms);
}

/* ===== Slave / Async API: st/ch 不支持, bus 层直接返回 NOTSUPP ===== */

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */
