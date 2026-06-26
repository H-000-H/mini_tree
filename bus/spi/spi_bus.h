/* SPDX-License-Identifier: GPL-2.0-or-later */
/*@=========================================================================================================================*
 * SPI BUS — SPI 总线子系统 bus 层
 *
 * 架构位置: VFS Layer → [Bus Layer (本文件)] → HAL Layer
 *
 * 职责:
 *   - host/client 池管理 (静态数组, O(1) 查找 via board_dev_find)
 *   - atomic ref_count (无锁引用计数, ISR 安全)
 *   - controller_ops (host 级生命周期: init/deinit/role/client_register/unregister)
 *   - bus_ops (client 级 I/O: open/close/transfer/write/read)
 *   - HAL vtable 封装 (hal_spi_bus_host_get/hal_spi_bus_slave_get)
 *   - 异步传输 (async/poll, master only, DMA + ISR callback)
 *
 * 隔离机制:
 *   - 本文件 (spi_bus.h) 在未定义 SPI_BUS_IMPL 时, 用 #pragma GCC poison
 *     禁止外部直接调用 hal 层任何符号 (hal_spi_*, HAL_SPI_*, 等)
 *   - 强制走 spi_bus_host_init / spi_bus_client_register 等 bus API
 *
 * 引用计数:
 *   - host->ref_count 为 atomic_int, client_register +1, unregister -1
 *   - host_deinit 检查 ref_count > 0 时返回 VFS_ERR_BUSY, 拒绝销毁
 *   - state 变更 (host init/deinit) 由上层 (spi_vfs.c probe/remove) 序列化
 *
 * controller_ops 设计:
 *   - init/deinit: host 生命周期 (绑定/解绑 bus_controller)
 *   - role: 返回 SPI_BUS_ROLE_MASTER / SPI_BUS_ROLE_SLAVE
 *   - client_register/unregister: client 挂载/卸载
 *
 * 异步传输 (async/poll):
 *   - spi_bus_transfer_async: 非阻塞提交, 立即返回
 *   - spi_bus_transfer_poll: 阻塞回收 trans 资源
 *   - callback 在 ISR 上下文调用, 严禁阻塞/睡眠/调 transfer
 *   - bridge 池 (s_bridge_pool) 静态分配, 避免 ISR 触发时栈帧 UAF
 *
 * @see bus/bus.h  通用总线框架 (bus_controller_ops, bus_ops, bus_controller_bind_full)
 *@=========================================================================================================================*/
#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct spi_bus_client;

#define SPI_BUS_ROLE_MASTER 0
#define SPI_BUS_ROLE_SLAVE  1

struct spi_bus_host_config {
    int host_id;
    int mosi_pin;
    int miso_pin;
    int sclk_pin;
    int max_transfer_sz;
    int dma_chan;
    int bus_role;
};

struct spi_bus_client_config {
    int mode;
    int clock_speed_hz;
    int cs_pin;
    int queue_size;
};

/*===========================================================================================================================================================*/
                                                              /* Host API (VFS 层调用) */
/*===========================================================================================================================================================*/
int  spi_bus_host_init(struct device* dev,
                       const struct spi_bus_host_config* cfg)
    COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_host_deinit(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_host_role(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                              /* Client API (VFS 层调用) */
/*===========================================================================================================================================================*/
int  spi_bus_client_register(struct device* dev,
                             const struct spi_bus_client_config* cfg,
                             struct spi_bus_client** out)
    COMPAT_WARN_UNUSED_RESULT;
void spi_bus_client_unregister(struct device* dev);

int  spi_bus_open(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_transfer(struct device* dev,
                      const uint8_t* tx, uint8_t* rx,
                      size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_slave_sync(struct device* dev,
                        const uint8_t* tx, uint8_t* rx,
                        size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_slave_queue_tx(struct device* dev,
                            const uint8_t* data, size_t len,
                            uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int  spi_bus_slave_get_trans_result(struct device* dev,
                                    uint8_t* rx_data, size_t rx_cap,
                                    size_t* trans_len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 异步提交 SPI 传输 (master only, 非阻塞)
 *
 * 提交后立即返回。传输完成时在 ISR 上下文调用 cb。
 * 调用者随后必须调 spi_bus_transfer_poll 回收 trans 资源。
 *
 * @param dev      SPI device (master)
 * @param tx       发送缓冲 (NULL = 只收)
 * @param rx       接收缓冲 (NULL = 只发, poll 前必须保持有效)
 * @param len      传输字节数
 * @param cb       传输完成 callback (ISR 上下文, 可 NULL)
 * @param userdata 传递给 cb 的用户数据
 * @return 成功返回 0, 失败返回 VFS_ERR_*
 */
int  spi_bus_transfer_async(struct device* dev,
                            const uint8_t* tx, uint8_t* rx,
                            size_t len, void (*cb)(struct device* dev,
                                                   const void* trans,
                                                   void* userdata),
                            void* userdata)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 回收已完成的 async trans (阻塞等待)
 *
 * @param dev        SPI device (master)
 * @param timeout_ms 超时
 * @return 成功返回 0, 超时返回 VFS_ERR_BUSY
 */
int  spi_bus_transfer_poll(struct device* dev, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#ifndef SPI_BUS_IMPL
/* 禁止 bus 层外部直接调用 HAL 层任何符号 — 强制走 spi_bus API */
#pragma GCC poison hal_spi_bus_host_init hal_spi_bus_host_deinit hal_spi_bus_host_get
#pragma GCC poison hal_spi_dev_init hal_spi_dev_hw_open hal_spi_dev_hw_close
#pragma GCC poison hal_spi_dev_register hal_spi_dev_unregister
#pragma GCC poison spi_sync spi_slave_sync spi_slave_queue_tx hal_spi_get_trans_result
#pragma GCC poison hal_spi_transfer
#pragma GCC poison hal_spi_transfer_async hal_spi_transfer_poll
#pragma GCC poison HAL_SPI_BUS_ROLE_SLAVE HAL_SPI_BUS_ROLE_MASTER HAL_SPI_MAX_TRANSFER_BYTES HAL_SPI_MAX_ASYNC
#pragma GCC poison hal_spi_device_config hal_spi_bus_config hal_spi_bus_host hal_spi_dev
#pragma GCC poison hal_spi_callback_t
#pragma GCC poison spi_device_config spi_bus_config spi_controller spi_device
#endif

#endif /* SPI_BUS_H */
