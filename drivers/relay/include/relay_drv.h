/**
 * SPDX-License-Identifier: Apache-2.0
 * @file relay_drv.h
 */
#ifndef RELAY_DRV_H
#define RELAY_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RELAY_CMD_BASE COMPAT_MAGIC(RELAY)
#define RELAY_CMD_SET (RELAY_CMD_BASE+0x01)
#define RELAY_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif
