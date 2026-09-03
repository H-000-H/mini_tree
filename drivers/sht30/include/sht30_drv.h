/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sht30_drv.h
 *@brief SHT30 温湿度传感器驱动 ioctl 命令与采样结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */

#ifndef SHT30_DRV_H
#define SHT30_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define SHT30_CMD_BASE MINI_MAGIC(SHT30)
/** 读取温度/湿度（arg: struct sht30_sample*） */
#define SHT30_CMD_READ_TEMP_RH (SHT30_CMD_BASE + 0x01)
/** 命令总数 */
#define SHT30_CMD_COUNT 1

/** @brief SHT30 采样结果 */
struct sht30_sample
{
    int16_t  temp_c_x100; /**< 摄氏度 ×100 */
    uint16_t rh_x100;     /**< 相对湿度 ×100 (0..10000) */
};

#ifdef __cplusplus
}
#endif

#endif /* SHT30_DRV_H */
