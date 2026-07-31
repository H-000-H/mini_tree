/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ina219_drv.h
 */
#ifndef INA219_DRV_H
#define INA219_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define INA219_CMD_BASE COMPAT_MAGIC(INA219)
#define INA219_CMD_READ_POWER (INA219_CMD_BASE + 0x01)
#define INA219_CMD_COUNT 1
struct ina219_sample { int16_t bus_mV; int16_t current_mA; int16_t power_mW; };
#ifdef __cplusplus
}
#endif
#endif
