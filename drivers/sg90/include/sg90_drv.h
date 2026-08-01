/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sg90_drv.h
 * @brief SG90 舵机驱动 ioctl 命令
 *
 * 挂在 TIM（PWM）下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef SG90_DRV_H
#define SG90_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define SG90_CMD_BASE COMPAT_MAGIC(SG90)
/** 设置角度（arg: int*，0..180 度） */
#define SG90_CMD_SET_ANGLE (SG90_CMD_BASE+0x01)
/** 命令总数 */
#define SG90_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif /* SG90_DRV_H */
