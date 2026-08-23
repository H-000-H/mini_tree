/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-uart.h
 *@brief vfs-uart 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   UART VFS — UART 总线子系统 VFS 层
 *   架构位置: [VFS Layer (本文件)] → Bus Layer → HAL Layer
 *   职责: file_operations 挂载 + dev_lifecycle (互斥/引用计数) + DTS 解析; I/O 全走 bus 层。
 *   隔离: 本文件定义 UART_VFS_IMPL 可调 uart_bus API; 其他文件包含本头时 uart_bus 符号被 #pragma
 *GCC poison。 Driver 注册:
 *   - vfs_uart_priv: "uart" (host)
 *   - uart_vfs:      "uart-client" (client)
 *   @see bus/uart/uart_bus.h  bus 层接口
 *   @see bus/bus.h           通用总线框架
 *   @=========================================================================================================================
 */

#ifndef UART_VFS_H
#define UART_VFS_H

#include "compiler_compat.h"
#include "device.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UART_CMD_BASE COMPAT_MAGIC(UART)
#define UART_CMD_TRANSFER UART_CMD_BASE + 0x01
#define UART_CMD_COUNT 1

    /** @brief UART 收发参数 (ioctl TRANSFER) */
    struct uart_transfer_arg
    {
        const uint8_t* tx; /**< 发送缓冲区 (可为 NULL) */
        uint8_t* rx; /**< 接收缓冲区 (可为 NULL) */
        size_t tx_len; /**< 发送长度 */
        size_t rx_len; /**< 接收长度 */
    };

    /**
     * @brief UART Client 设备探测: 申请池槽, 注册 client, 绑定 fops 与生命周期
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int uart_vfs_probe(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief UART Client 设备移除: 拒新 IO, 排空已有 IO, 注销 client, 释放池槽
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int uart_vfs_remove(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef UART_VFS_IMPL
/* 禁止 VFS 层外部直接调用 uart_bus 层任何符号 — 强制走 uart_vfs API */
#pragma GCC poison uart_bus_host_init uart_bus_host_deinit
#pragma GCC poison uart_bus_client_register uart_bus_client_unregister
#pragma GCC poison uart_bus_open uart_bus_close
#pragma GCC poison uart_bus_write uart_bus_read uart_bus_transfer
#endif

#endif /* UART_VFS_H */
