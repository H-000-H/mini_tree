/* SPDX-License-Identifier: GPL-2.0-or-later */
/*@=========================================================================================================================*
 * UART VFS — UART 总线子系统 VFS 层
 *
 * 架构位置: [VFS Layer (本文件)] → Bus Layer → HAL Layer
 *
 * 职责:
 *   - file_operations 挂载 (open/close/read/write/ioctl)
 *   - dev_lifecycle (open/close/io 互斥, 引用计数)
 *   - DTS 参数解析 (host 级 + client 级)
 *   - I/O 全走 bus 层接口, 不直接碰 HAL
 *
 * 隔离机制:
 *   - uart_vfs.c 定义 UART_VFS_IMPL, 可调用 uart_bus API
 *   - 其他文件包含本头, uart_bus 符号被 #pragma GCC poison, 强制走 uart_vfs API
 *
 * Driver 注册:
 *   - uart_host_vfs:        compatible="stm32,uart1" / "ch32,uart1" / "esp32,uart1" (host)
 *   - uart_vfs:             compatible="stm32,uart-client" (client)
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

int uart_vfs_probe(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
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
#pragma GCC poison uart_bus_host_config uart_bus_client_config
#pragma GCC poison UART_PARITY_NONE UART_PARITY_EVEN UART_PARITY_ODD
#pragma GCC poison UART_STOP_BITS_1 UART_STOP_BITS_1_5 UART_STOP_BITS_2
#pragma GCC poison UART_DATA_BITS_5 UART_DATA_BITS_6 UART_DATA_BITS_7 UART_DATA_BITS_8
#endif

#endif /* UART_VFS_H */
