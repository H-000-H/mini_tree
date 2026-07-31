/**
 * SPDX-License-Identifier: Apache-2.0
 * @file pcf8574_drv.h
 */
#ifndef PCF8574_DRV_H
#define PCF8574_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PCF8574_CMD_BASE COMPAT_MAGIC(PCF8574)
#define PCF8574_CMD_WRITE (PCF8574_CMD_BASE + 0x01)
#define PCF8574_CMD_READ  (PCF8574_CMD_BASE + 0x02)
#define PCF8574_CMD_COUNT 2

#ifdef __cplusplus
}
#endif
#endif
