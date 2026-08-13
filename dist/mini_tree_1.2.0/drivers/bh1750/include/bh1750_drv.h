/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bh1750_drv.h
 * @brief BH1750 数字光照传感器驱动 ioctl 命令
 *
 * 挂在 I2C 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */
#ifndef BH1750_DRV_H
#define BH1750_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define BH1750_CMD_BASE COMPAT_MAGIC(BH1750)
/** 读取光照强度（arg: uint16_t*，单位 lux） */
#define BH1750_CMD_READ_LUX (BH1750_CMD_BASE + 0x01)
/** 命令总数 */
#define BH1750_CMD_COUNT 1

#ifdef __cplusplus
}
#endif

#endif /* BH1750_DRV_H */
