/**
 * SPDX-License-Identifier: Apache-2.0
 * @file drv8833_drv.h
 */
#ifndef DRV8833_DRV_H
#define DRV8833_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DRV8833_CMD_BASE COMPAT_MAGIC(DRV8833)
#define DRV8833_CMD_SET_MOTOR (DRV8833_CMD_BASE+0x01)
#define DRV8833_CMD_COUNT 1
struct drv8833_motor { int motor; int fwd; };
#ifdef __cplusplus
}
#endif
#endif
