/**
 * SPDX-License-Identifier: Apache-2.0
 * @file air780e_drv.h
 * @brief Air780E 4G 模块驱动 ioctl 命令与 AT 收发结构
 *
 * 挂在 UART 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef AIR780E_DRV_H
#define AIR780E_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define AIR780E_CMD_BASE COMPAT_MAGIC(AIR780E)
/** 发送 AT 命令（arg: struct air780e_at*） */
#define AIR780E_CMD_AT_SEND (AIR780E_CMD_BASE+0x01)
/** 接收 AT 应答（arg: struct air780e_at*） */
#define AIR780E_CMD_AT_RECV (AIR780E_CMD_BASE+0x02)
/** 命令总数 */
#define AIR780E_CMD_COUNT 2

/** @brief AT 命令收发缓冲（SEND 用 tx，RECV 用 rx/rx_cap，rx_len 回填） */
struct air780e_at
{
    const uint8_t* tx;      /**< 发送缓冲 */
    size_t         tx_len;  /**< 发送长度 */
    uint8_t*       rx;      /**< 接收缓冲 */
    size_t         rx_cap;  /**< 接收容量 */
    size_t         rx_len;  /**< 实际接收长度（回填） */
};
#ifdef __cplusplus
}
#endif
#endif /* AIR780E_DRV_H */
