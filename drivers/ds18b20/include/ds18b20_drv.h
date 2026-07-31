/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ds18b20_drv.h
 */
#ifndef DS18B20_DRV_H
#define DS18B20_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DS18B20_CMD_BASE COMPAT_MAGIC(DS18B20)
#define DS18B20_CMD_READ_TEMP (DS18B20_CMD_BASE+0x01)
#define DS18B20_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif
