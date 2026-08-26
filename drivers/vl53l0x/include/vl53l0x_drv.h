/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vl53l0x_drv.h
 *@brief VL53L0X 激光测距传感器驱动 ioctl 命令与采样结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */

#ifndef VL53L0X_DRV_H
#define VL53L0X_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define VL53L0X_CMD_BASE MINI_MAGIC(VL53L0X)
/** 读取测距结果（arg: struct vl53l0x_sample*） */
#define VL53L0X_CMD_READ_DISTANCE (VL53L0X_CMD_BASE + 0x01)
/** 命令总数 */
#define VL53L0X_CMD_COUNT 1

    /** @brief VL53L0X 测距结果 */
    struct vl53l0x_sample
    {
        uint16_t mm; /**< 距离，毫米 */
    };
#ifdef __cplusplus
}
#endif
#endif /* VL53L0X_DRV_H */
