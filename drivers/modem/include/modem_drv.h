/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file modem_drv.h
 *@brief 4G/蜂窝模组统一抽象层 — ioctl 命令与 AT 收发结构（跨 a7670/air780e 等）
 *@author H-000-H
 *@details
 *   本头定义**唯一一套**模组命令, 供 A7670 / Air780E 等 4G 驱动统一实现。
 *   上层 (net/port/pppif.c 拨号前置) 只调用这套命令, 换模组仅需更换 device 节点。
 *   通道划分 (与具体驱动一致):
 *   - ioctl MODEM_CMD_AT_SEND/RECV: AT 命令模式 (配置 APN / 拨号 ATD*99# 前置)。
 *   - fops read/write: 裸字节流透传 (PPP 模式)。拨号后模组进入 PPP 数据态,
 *   适配层经 device_read/device_write 直收发字节流, 喂给 lwIP pppos。
 */

#ifndef MODEM_DRV_H
#define MODEM_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MODEM_CMD_BASE                                                                             \
    COMPAT_MAGIC(MODEM) /** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突）              \
                         */
#define MODEM_CMD_AT_SEND (MODEM_CMD_BASE + 0x01) /** 发送 AT 命令（arg: struct modem_at_buf*） */
#define MODEM_CMD_AT_RECV (MODEM_CMD_BASE + 0x02) /** 接收 AT 应答（arg: struct modem_at_buf*） */
#define MODEM_CMD_COUNT 2 /** 命令总数 */

    /** @brief AT 命令收发缓冲（SEND 用 tx，RECV 用 rx/rx_cap，rx_len 回填） */
    struct modem_at_buf
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
#endif /* MODEM_DRV_H */
