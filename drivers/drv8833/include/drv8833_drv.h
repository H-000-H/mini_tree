/**
 * SPDX-License-Identifier: Apache-2.0
 * @file drv8833_drv.h
 * @brief DRV8833 双路电机驱动 ioctl 命令与参数结构
 *
 * 挂在 GPIO 或 TIM（PWM）下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef DRV8833_DRV_H
#define DRV8833_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define DRV8833_CMD_BASE COMPAT_MAGIC(DRV8833)
/** 设置电机方向（arg: struct drv8833_motor*） */
#define DRV8833_CMD_SET_MOTOR (DRV8833_CMD_BASE + 0x01)
/** 命令总数 */
#define DRV8833_CMD_COUNT 1

    /** @brief 电机选择与方向参数 */
    struct drv8833_motor
    {
        int motor; /**< 电机编号（0/1） */
        int fwd; /**< 方向：1=正转，0=反转 */
    };
#ifdef __cplusplus
}
#endif
#endif /* DRV8833_DRV_H */
