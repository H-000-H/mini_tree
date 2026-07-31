/**
 * SPDX-License-Identifier: Apache-2.0
 * @file epaper_drv.h
 * @brief 电子纸 — 可接 LVGL 单色 flush / 自绘缓冲
 */
#ifndef EPAPER_DRV_H
#define EPAPER_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "epaper_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EPAPER_CMD_BASE         COMPAT_MAGIC(EPAPER)
#define EPAPER_CMD_INIT         (EPAPER_CMD_BASE + 0x01)
#define EPAPER_CMD_CLEAR        (EPAPER_CMD_BASE + 0x02)
#define EPAPER_CMD_DRAW_BITMAP  (EPAPER_CMD_BASE + 0x03)
#define EPAPER_CMD_GET_INFO     (EPAPER_CMD_BASE + 0x04)
#define EPAPER_CMD_COUNT        4

struct epaper_bitmap
{
    const uint8_t* data;
    size_t         len;
};

struct epaper_info
{
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
};

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_DRV_H */
