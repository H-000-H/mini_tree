/**
 * SPDX-License-Identifier: Apache-2.0
 * @file aht20_drv.h
 */
#ifndef AHT20_DRV_H
#define AHT20_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AHT20_CMD_BASE COMPAT_MAGIC(AHT20)
#define AHT20_CMD_READ_TEMP_RH (AHT20_CMD_BASE + 0x01)
#define AHT20_CMD_COUNT 1
struct aht20_sample { int16_t temp_c_x100; uint16_t rh_x100; };
#ifdef __cplusplus
}
#endif
#endif
