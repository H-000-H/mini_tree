/**
 * SPDX-License-Identifier: Apache-2.0
 * @file a7670_drv.h
 * @brief A7670 4G 模块驱动 ioctl 命令与 AT 收发结构
 *
 * 挂在 UART 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 *
 * 通道划分:
 *   - ioctl AT_SEND/AT_RECV: AT 命令模式 (配置 APN / 拨号 ATD*99# 前置)。
 *   - fops read/write: 裸字节流透传 (PPP 模式)。拨号后模组进入 PPP 数据态,
 *     pppif 适配层经 device_read/device_write 直收发字节流, 喂给 lwIP pppos。
 *     AT 态与 PPP 态共用同一 UART(由调用方状态机保证)。
 */
#ifndef A7670_DRV_H
#define A7670_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define A7670_CMD_BASE COMPAT_MAGIC(A7670)
/** 发送 AT 命令（arg: struct a7670_at_buf*） */
#define A7670_CMD_AT_SEND (A7670_CMD_BASE + 0x01)
/** 接收 AT 应答（arg: struct a7670_at_buf*） */
#define A7670_CMD_AT_RECV (A7670_CMD_BASE + 0x02)
/** 命令总数 */
#define A7670_CMD_COUNT 2

    /** @brief AT 命令收发缓冲（SEND 用 tx，RECV 用 rx/rx_cap，rx_len 回填） */
    struct a7670_at_buf
    {
        const uint8_t* tx; /**< 发送缓冲 */
        size_t tx_len; /**< 发送长度 */
        uint8_t* rx; /**< 接收缓冲 */
        size_t rx_cap; /**< 接收容量 */
        size_t rx_len; /**< 实际接收长度（回填） */
    };
#ifdef __cplusplus
}
#endif
#endif /* A7670_DRV_H */
