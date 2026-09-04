/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file pcf8574_drv.h
 *@brief PCF8574 GPIO 扩展芯片驱动 ioctl 命令
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef PCF8574_DRV_H
#define PCF8574_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define PCF8574_CMD_BASE MINI_MAGIC(PCF8574)
/** 写输出口（arg: uint8_t*，8bit 电平） */
#define PCF8574_CMD_WRITE (PCF8574_CMD_BASE + 0x01)
/** 读输入口（arg: uint8_t*，8bit 电平） */
#define PCF8574_CMD_READ (PCF8574_CMD_BASE + 0x02)
/** 命令总数 */
#define PCF8574_CMD_COUNT 2

#ifdef __cplusplus
}
#endif
#endif /* PCF8574_DRV_H */
