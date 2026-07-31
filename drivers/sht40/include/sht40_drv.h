/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sht40_drv.h
 */
#ifndef SHT40_DRV_H
#define SHT40_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SHT40_CMD_BASE COMPAT_MAGIC(SHT40)
#define SHT40_CMD_READ_TEMP_RH (SHT40_CMD_BASE + 0x01)
#define SHT40_CMD_COUNT 1
struct sht40_sample { int16_t temp_c_x100; uint16_t rh_x100; };
#ifdef __cplusplus
}
#endif
#endif
