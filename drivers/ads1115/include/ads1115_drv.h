/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ads1115_drv.h
 */
#ifndef ADS1115_DRV_H
#define ADS1115_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define ADS1115_CMD_BASE COMPAT_MAGIC(ADS1115)
#define ADS1115_CMD_READ_CHANNEL (ADS1115_CMD_BASE + 0x01)
#define ADS1115_CMD_COUNT 1
struct ads1115_sample { int channel; int16_t raw; };
#ifdef __cplusplus
}
#endif
#endif
