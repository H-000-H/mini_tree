/**
 * SPDX-License-Identifier: Apache-2.0
 * @file vl53l0x_drv.h
 */
#ifndef VL53L0X_DRV_H
#define VL53L0X_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VL53L0X_CMD_BASE COMPAT_MAGIC(VL53L0X)
#define VL53L0X_CMD_READ_DISTANCE (VL53L0X_CMD_BASE + 0x01)
#define VL53L0X_CMD_COUNT 1
struct vl53l0x_sample { uint16_t mm; };
#ifdef __cplusplus
}
#endif
#endif
