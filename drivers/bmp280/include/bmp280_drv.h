/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bmp280_drv.h
 */
#ifndef BMP280_DRV_H
#define BMP280_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BMP280_CMD_BASE COMPAT_MAGIC(BMP280)
#define BMP280_CMD_READ_PRESS_TEMP (BMP280_CMD_BASE + 0x01)
#define BMP280_CMD_COUNT 1
struct bmp280_sample { int32_t press_pa; int16_t temp_c_x100; };
#ifdef __cplusplus
}
#endif
#endif
