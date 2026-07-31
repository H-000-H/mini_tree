/**
 * SPDX-License-Identifier: Apache-2.0
 * @file neo_m8n_drv.h
 */
#ifndef NEO_M8N_DRV_H
#define NEO_M8N_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NEO_M8N_CMD_BASE COMPAT_MAGIC(NEO_M8N)
#define NEO_M8N_CMD_READ_NMEA (NEO_M8N_CMD_BASE+0x01)
#define NEO_M8N_CMD_COUNT 1
struct neo_m8n_buf { char* data; size_t cap; size_t len; };
#ifdef __cplusplus
}
#endif
#endif
