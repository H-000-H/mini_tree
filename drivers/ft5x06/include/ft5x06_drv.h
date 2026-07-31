/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ft5x06_drv.h
 * @note LVGL indev：见 ft5x06_bridge.h → ft5x06_lvgl_read()
 */
#ifndef FT5X06_DRV_H
#define FT5X06_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FT5X06_CMD_BASE COMPAT_MAGIC(FT5X06)
#define FT5X06_CMD_READ_TOUCH (FT5X06_CMD_BASE+0x01)
#define FT5X06_CMD_COUNT 1
struct ft5x06_touch { uint8_t points; uint16_t x; uint16_t y; };
#ifdef __cplusplus
}
#endif
#endif
