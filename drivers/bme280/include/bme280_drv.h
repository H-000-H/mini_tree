/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bme280_drv.h
 */
#ifndef BME280_DRV_H
#define BME280_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BME280_CMD_BASE COMPAT_MAGIC(BME280)
#define BME280_CMD_READ_ENV (BME280_CMD_BASE+0x01)
#define BME280_CMD_COUNT 1
struct bme280_env { int16_t temp_c_x100; uint16_t humidity_x100; uint32_t pressure; };
#ifdef __cplusplus
}
#endif
#endif
