/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file mpu6050_drv.h
 *@brief MPU6050 六轴 IMU 驱动 ioctl 命令与采样结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */

#ifndef MPU6050_DRV_H
#define MPU6050_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define MPU6050_CMD_BASE COMPAT_MAGIC(MPU6050)
/** 读取加速度/陀螺仪原始值（arg: struct mpu6050_sample*） */
#define MPU6050_CMD_READ_ACCEL_GYRO (MPU6050_CMD_BASE + 0x01)
/** 命令总数 */
#define MPU6050_CMD_COUNT 1

    /** @brief MPU6050 六轴原始采样 */
    struct mpu6050_sample
    {
        int16_t ax, ay, az; /**< 加速度计原始 LSB（量程由 DTS 配置） */
        int16_t gx, gy, gz; /**< 陀螺仪原始 LSB（量程由 DTS 配置） */
    };

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_DRV_H */
