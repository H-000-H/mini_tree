/**
 * SPDX-License-Identifier: Apache-2.0
 * @file xpt2046_drv.h
 * @note LVGL indev：见 xpt2046_bridge.h → xpt2046_lvgl_read()
 */
#ifndef XPT2046_DRV_H
#define XPT2046_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XPT2046_CMD_BASE COMPAT_MAGIC(XPT2046)
#define XPT2046_CMD_READ_XY (XPT2046_CMD_BASE+0x01)
#define XPT2046_CMD_COUNT 1
struct xpt2046_xy { uint16_t x; uint16_t y; int pressed; };
#ifdef __cplusplus
}
#endif
#endif
