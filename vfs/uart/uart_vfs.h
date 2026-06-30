/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * UART VFS — UART 总线子系统 VFS 层
 *
 * 架构位置: [VFS Layer (本文件)] → Bus Layer → HAL Layer
 * 职责: file_operations 挂载 + dev_lifecycle (互斥/引用计数) + DTS 解析; I/O 全走 bus 层。
 * 隔离: 本文件定义 UART_VFS_IMPL 可调 uart_bus API; 其他文件包含本头时 uart_bus 符号被 #pragma GCC poison。
 *
 * Driver 注册:
 *   - uart_host_vfs: "uart" (host)
 *   - uart_vfs:      "uart-client" (client)
 *
 * @see bus/uart/uart_bus.h  bus 层接口
 * @see bus/bus.h           通用总线框架
 *@=========================================================================================================================*/
#ifndef UART_VFS_H
#define UART_VFS_H

#include <stdint.h>
#include <stddef.h>
#include "device.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_CMD_BASE     COMPAT_MAGIC(UART)
#define UART_CMD_TRANSFER UART_CMD_BASE + 0x01

struct uart_transfer_arg {
    const uint8_t* tx;
    uint8_t*       rx;
    size_t         tx_len;
    size_t         rx_len;
};

/**
 * @brief UART Client 设备探测: 申请池槽/互斥锁, 注册 client, 绑定 fops 与生命周期
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_vfs_probe(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief UART Client 设备移除: 拒新 IO, 排空已有 IO, 注销 client, 释放池槽与互斥锁
 * @param dev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int uart_vfs_remove(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef UART_VFS_IMPL
/* 禁止 VFS 层外部直接调用 uart_bus 层任何符号 — 强制走 uart_vfs API */
#pragma GCC poison uart_bus_host_init uart_bus_host_deinit
#pragma GCC poison uart_bus_client_register uart_bus_client_unregister
#pragma GCC poison uart_bus_open uart_bus_close uart_bus_transfer
#pragma GCC poison uart_bus_write uart_bus_read
#endif

#endif /* UART_VFS_H */
