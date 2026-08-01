/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ds18b20_drv.h
 * @brief DS18B20 单总线温度传感器驱动 ioctl 命令
 *
 * 挂在 GPIO 单总线（OW）下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef DS18B20_DRV_H
#define DS18B20_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define DS18B20_CMD_BASE COMPAT_MAGIC(DS18B20)
/** 读取温度（arg: int16_t*，摄氏度 ×100） */
#define DS18B20_CMD_READ_TEMP (DS18B20_CMD_BASE+0x01)
/** 命令总数 */
#define DS18B20_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif /* DS18B20_DRV_H */
