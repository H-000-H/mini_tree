/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file spi_bus.h
 *@brief spi bus 头文件
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   SPI BUS — SPI 总线子系统 bus 层
 *   架构: VFS → [Bus (本文件)] → HAL; hal_spi_bus_host 嵌入 spi_bus_host (无 vtable)
 *   职责: host/client 池 + atomic ref_count + controller_ops (host 生命周期) +
 *   client I/O (open/close/transfer) + 同步传输 (poll, master)
 *   隔离: 未定义 SPI_BUS_IMPL 时 #pragma GCC poison 禁止外部调 hal_spi_* 与 spi_sync;
 *   允许 config 类型供 VFS 填充, 强制走 spi_bus API
 *   引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY
 *   @see bus/bus.h  通用总线框架
 *   --------------------------------------------------------------------------
 */

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include "compiler_compat.h"
#include "hal_spi.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;
    struct spi_bus_client;

#define SPI_BUS_ROLE_MASTER HAL_SPI_BUS_ROLE_MASTER
#define SPI_BUS_ROLE_SLAVE HAL_SPI_BUS_ROLE_SLAVE

    /* -------------------------------------------------------------------------- */
    /*Host API (VFS 层调用)*/
    /* -------------------------------------------------------------------------- */
    /**
     * @brief SPI host 初始化 (config 类型直接用 hal_spi_bus_config, bus 零翻译透传)
     * @param[in] pdev controller device (host)
     * @param[in] cfg host 配置 (VFS 填充 DTSI 硬件直投值)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_host_init(struct device* pdev,
                          const struct hal_spi_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief SPI host 反初始化 (ref_count > 0 时返回 BUSY)
     * @param[in] pdev controller device (host)
     * @return 成功返回 MINI_OK, BUSY 返回 MINI_ERR_BUSY, 失败返回 VFS_ERR_*
     */
    int spi_bus_host_deinit(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 查询 SPI host 角色 (master/slave)
     * @param[in] pdev controller device (host)
     * @return master 返回 SPI_BUS_ROLE_MASTER, slave 返回 SPI_BUS_ROLE_SLAVE, 失败返回 -1
     */
    int spi_bus_host_role(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /* -------------------------------------------------------------------------- */

    /*Client API (VFS 层调用)*/
    /* -------------------------------------------------------------------------- */
    /**
     * @brief SPI client 注册 (config 类型直接用 hal_spi_device_config, bus 零翻译透传)
     * @param[in] pdev client device
     * @param[in] cfg client 配置 (VFS 填充 DTSI 硬件直投值)
     * @param[out] out 输出 spi_bus_client 指针
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_client_register(struct device* pdev, const struct hal_spi_device_config* cfg,
                                struct spi_bus_client** out) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 注销 SPI client 并递减 host 引用计数 (ref_count -1, 清零槽位)
     * @param[in] pdev client device
     */
    void spi_bus_client_unregister(struct device* pdev);

    /**
     * @brief 打开 SPI client 硬件 (幂等)
     * @param[in] pdev client device
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_open(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 SPI client 硬件 (幂等)
     * @param[in] pdev client device
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_close(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief SPI 同步全双工传输
     * @param[in] pdev client device
     * @param[in] tx 发送缓冲区 (可 NULL)
     * @param[out] rx 接收缓冲区 (可 NULL)
     * @param[in] len 传输字节数
     * @param[in] timeout_ms 超时 (毫秒)
     * @param[in] xfer_mode HAL_SPI_XFER_AUTO / POLL / DMA
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                         uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief SPI slave 模式同步传输
     * @param[in] pdev client device
     * @param[in] tx 发送缓冲区
     * @param[out] rx 接收缓冲区
     * @param[in] len 传输字节数
     * @param[in] timeout_ms 超时 (毫秒)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_slave_sync(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                           uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief SPI slave 模式排队发送
     * @param[in] pdev client device
     * @param[in] data 发送数据
     * @param[in] len 数据长度
     * @param[in] timeout_ms 超时 (毫秒)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_slave_queue_tx(struct device* pdev, const uint8_t* data, size_t len,
                               uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief SPI slave 模式获取传输结果
     * @param[in] pdev client device
     * @param[out] rx_data 接收缓冲区
     * @param[out] rx_cap 接收缓冲区容量
     * @param[out] trans_len 输出实际传输长度
     * @param[in] timeout_ms 超时 (毫秒)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_slave_get_trans_result(struct device* pdev, uint8_t* rx_data, size_t rx_cap,
                                       size_t* trans_len,
                                       uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief SPI 异步传输 (master 模式, 回调在 ISR 触发)
     * @param[in] pdev client device
     * @param[in] tx 发送缓冲区
     * @param[out] rx 接收缓冲区
     * @param[in] len 传输字节数
     * @param[in] cb 传输完成回调
     * @param[in] userdata 回调用户数据
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_transfer_async(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                               void (*cb)(struct device* pdev, const void* trans, void* userdata),
                               void* userdata) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 轮询等待异步传输完成
     * @param[in] pdev client device
     * @param[in] timeout_ms 超时 (毫秒)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int spi_bus_transfer_poll(struct device* pdev, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#ifndef SPI_BUS_IMPL
/* 禁止 bus 层外部直接调用 HAL 函数 — 强制走 spi_bus API。
 * 允许 config 类型 (hal_spi_bus_config, hal_spi_device_config, hal_spi_pin_cfg 等)
 * 供 VFS 层填充 DTSI 值。 */
#pragma GCC poison hal_spi_bus_host_init hal_spi_bus_host_deinit
#pragma GCC poison hal_spi_dev_init hal_spi_dev_hw_open hal_spi_dev_hw_close
#pragma GCC poison hal_spi_dev_register hal_spi_dev_unregister
#pragma GCC poison spi_sync spi_slave_sync spi_slave_queue_tx
#pragma GCC poison hal_spi_sync hal_spi_slave_sync hal_spi_slave_queue_tx
#pragma GCC poison hal_spi_get_trans_result
#pragma GCC poison hal_spi_transfer_async hal_spi_transfer_poll
#pragma GCC poison hal_spi_callback_t
#endif

#endif /* SPI_BUS_H */
