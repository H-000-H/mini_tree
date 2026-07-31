/**
 * SPDX-License-Identifier: Apache-2.0
 * @file mpu6050_drv.h
 */
#ifndef MPU6050_DRV_H
#define MPU6050_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MPU6050_CMD_BASE            COMPAT_MAGIC(MPU6050)
#define MPU6050_CMD_READ_ACCEL_GYRO (MPU6050_CMD_BASE + 0x01)
#define MPU6050_CMD_COUNT           1

struct mpu6050_sample {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
};

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_DRV_H */
