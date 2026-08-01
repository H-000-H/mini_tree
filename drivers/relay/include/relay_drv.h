/**
 * SPDX-License-Identifier: Apache-2.0
 * @file relay_drv.h
 * @brief 继电器驱动 ioctl 命令
 *
 * 挂在 GPIO 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef RELAY_DRV_H
#define RELAY_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define RELAY_CMD_BASE COMPAT_MAGIC(RELAY)
/** 设置继电器状态（arg: int*，0=断开 1=吸合） */
#define RELAY_CMD_SET (RELAY_CMD_BASE+0x01)
/** 命令总数 */
#define RELAY_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif /* RELAY_DRV_H */
