/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-spi.h
 *@brief vfs-spi 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   SPI VFS — SPI 总线子系统 VFS 层
 *   架构位置: [VFS Layer (本文件)] → Bus Layer → HAL Layer
 *   职责: file_operations 挂载 + dev_lifecycle (互斥/引用计数) + DTS 解析; I/O 全走 bus 层。
 *   隔离: 本文件定义 SPI_VFS_IMPL 可调 spi_bus API; 其他文件包含本头时 spi_bus 符号被 #pragma GCC
 *   poison。
 *   Driver 注册:
 *   - spi_host_master: "spi-master" (host controller)
 *   - spi_host_slave:  "spi-slave" (slave host controller)
 *   - spi_vfs_master:  "heterogeneous,spi-master-client" (bus client)
 *   - spi_vfs_slave:   "heterogeneous,spi-slave-client" (bus client)
 *   @see bus/spi/spi_bus.h  bus 层接口
 *   @see bus/bus.h          通用总线框架
 *   @=========================================================================================================================
 */

#ifndef SPI_VFS_H
#define SPI_VFS_H

#include "compiler_compat.h"
#include "device.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*SPI VFS ioctl 命令与参数*/
/*===========================================================================================================================================================*/
/** @defgroup spi_vfs_ioctl SPI VFS ioctl 命令
 *  @{
 */
#define SPI_CMD_BASE COMPAT_MAGIC(SPI)
#define SPI_CMD_TRANSFER SPI_CMD_BASE + 0x01 /**< Master 同步全双工 (arg.xfer_mode 可选路径) */
#define SPI_CMD_QUEUE_TX SPI_CMD_BASE + 0x02 /**< Slave: 入队发送 */
#define SPI_CMD_GET_TRANS_RESULT SPI_CMD_BASE + 0x03 /**< Slave: 取传输结果 */
#define SPI_CMD_SET_XFER_MODE                                                                      \
    SPI_CMD_BASE + 0x04 /**< 设置后续 write/read/transfer 的 xfer_mode                        \
                         */
#define SPI_CMD_GET_XFER_MODE SPI_CMD_BASE + 0x05 /**< 查询当前 xfer_mode */
#define SPI_CMD_TRANSFER_ASYNC SPI_CMD_BASE + 0x06 /**< Master 异步提交 (完成走 cb, 非阻塞) */
#define SPI_CMD_ASYNC_WAIT SPI_CMD_BASE + 0x07 /**< 等待异步传输完成 (timeout_ms) */
#define SPI_CMD_COUNT 7 /**< 命令映射表大小 */

/** 与 HAL_SPI_XFER_* 同值, VFS/业务侧使用 (仅同步路径) */
#define SPI_XFER_AUTO 0U /**< 隐式: DMA 可用则 DMA, 否则 poll (write/read 默认) */
#define SPI_XFER_POLL 1U /**< 强制 poll */
#define SPI_XFER_DMA 2U /**< 强制 DMA, 不可用返回 NOTSUPP */

    /** @brief 异步完成回调 (可能在 ISR 上下文) */
    typedef void (*spi_async_cb_t)(struct device* pdev, const void* trans, void* userdata);

    /** @brief 全双工同步传输参数 */
    struct spi_transfer_arg
    {
        const uint8_t* tx; /**< 发送缓冲, NULL 表示只收 */
        uint8_t* rx; /**< 接收缓冲, NULL 表示只发 */
        size_t len; /**< 传输字节数 */
        uint32_t xfer_mode; /**< SPI_XFER_AUTO/POLL/DMA; AUTO 时用 client 当前偏好 */
    };

    /** @brief 异步传输参数 (提交即返回; 完成经 cb) */
    struct spi_transfer_async_arg
    {
        const uint8_t* tx; /**< 发送缓冲, NULL 表示只收 */
        uint8_t* rx; /**< 接收缓冲, NULL 表示只发 */
        size_t len; /**< 传输字节数 */
        spi_async_cb_t cb; /**< 完成回调; NULL 为 fire-and-forget (平台相关) */
        void* userdata; /**< 回传给 cb */
    };

    /** @brief 设置/查询同步传输路径 */
    struct spi_xfer_mode_arg
    {
        uint32_t xfer_mode; /**< SPI_XFER_AUTO / POLL / DMA */
    };

    /** @brief Slave 队列发送参数 */
    struct spi_queue_arg
    {
        const uint8_t* data; /**< 数据指针 */
        size_t len; /**< 数据长度 */
    };

    /** @brief Slave 取传输结果参数 */
    struct spi_trans_result_arg
    {
        uint8_t* data; /**< 接收缓冲 */
        size_t len; /**< 缓冲大小 */
        size_t* trans_len; /**< 实际接收长度 (输出) */
    };
    /** @} */
    /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#ifndef SPI_VFS_IMPL
/* 禁止 VFS 层外部直接调用 spi_bus — 上层经 device_write/read/ioctl */
#pragma GCC poison spi_bus_host_init spi_bus_host_deinit spi_bus_host_role
#pragma GCC poison spi_bus_client_register spi_bus_client_unregister
#pragma GCC poison spi_bus_open spi_bus_close spi_bus_transfer
#pragma GCC poison spi_bus_slave_sync spi_bus_slave_queue_tx spi_bus_slave_get_trans_result
#pragma GCC poison spi_bus_transfer_async spi_bus_transfer_poll
#pragma GCC poison SPI_BUS_ROLE_MASTER SPI_BUS_ROLE_SLAVE
#endif

#endif /* SPI_VFS_H */
