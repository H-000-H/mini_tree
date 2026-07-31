/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sg90_drv.h
 */
#ifndef SG90_DRV_H
#define SG90_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SG90_CMD_BASE COMPAT_MAGIC(SG90)
#define SG90_CMD_SET_ANGLE (SG90_CMD_BASE+0x01)
#define SG90_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif
