/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file buzzer_drv.h
 *@brief 蜂鸣器驱动 ioctl 命令
 *@author H-000-H
 *@details
 *   挂在 GPIO 或 TIM（PWM）下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef BUZZER_DRV_H
#define BUZZER_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define BUZZER_CMD_BASE MINI_MAGIC(BUZZER)
/** 鸣响控制（arg: int*，0=关 1=开） */
#define BUZZER_CMD_BEEP (BUZZER_CMD_BASE + 0x01)
/** 命令总数 */
#define BUZZER_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif /* BUZZER_DRV_H */
