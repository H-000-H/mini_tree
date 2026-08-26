/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hc05_drv.h
 *@brief HC-05 蓝牙串口模块驱动 ioctl 命令与 AT 发送结构
 *@author H-000-H
 *@details
 *   挂在 UART 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef HC05_DRV_H
#define HC05_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define HC05_CMD_BASE MINI_MAGIC(HC05)
/** 发送 AT 命令（arg: struct hc05_at*） */
#define HC05_CMD_AT_SEND (HC05_CMD_BASE + 0x01)
/** 命令总数 */
#define HC05_CMD_COUNT 1

    /** @brief AT 命令发送参数 */
    struct hc05_at
    {
        const uint8_t* tx; /**< 发送缓冲 */
        size_t tx_len; /**< 发送长度 */
    };
#ifdef __cplusplus
}
#endif
#endif /* HC05_DRV_H */
