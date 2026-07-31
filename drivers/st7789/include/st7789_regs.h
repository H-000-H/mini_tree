/**
 * SPDX-License-Identifier: Apache-2.0
 * @file st7789_regs.h
 * @brief ST7789 面板命令 / 时序常量（避免 .c 内散落魔术字）
 */
#ifndef ST7789_REGS_H
#define ST7789_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 面板命令 */
#define ST7789_REG_SWRESET   0x01U
#define ST7789_REG_SLPOUT    0x11U
#define ST7789_REG_NORON     0x13U
#define ST7789_REG_INVON     0x21U
#define ST7789_REG_DISPOFF   0x28U
#define ST7789_REG_DISPON    0x29U
#define ST7789_REG_CASET     0x2AU
#define ST7789_REG_RASET     0x2BU
#define ST7789_REG_RAMWR     0x2CU
#define ST7789_REG_MADCTL    0x36U
#define ST7789_REG_COLMOD    0x3AU
#define ST7789_REG_RAMCTRL   0xB0U

/* COLMOD / MADCTL 常用值 */
#define ST7789_COLMOD_16BIT  0x55U
#define ST7789_MADCTL_DEFAULT 0x00U
#define ST7789_RAMCTRL_BE0   0x00U
#define ST7789_RAMCTRL_BE1   0xF0U

/* 驱动内部缓冲 / 超时 */
#define ST7789_BLOCK_BUF_SIZE     2048U
#define ST7789_DEFAULT_CHUNK      512U
#define ST7789_TIMEOUT_CMD_MS     100U
#define ST7789_TIMEOUT_IO_MS      500U
#define ST7789_MAX_WIDTH          320
#define ST7789_BL_ARR_FALLBACK    1023U

/* 像素格式（对接 LVGL COLOR_FORMAT_RGB565） */
#define ST7789_COLOR_FORMAT_RGB565  16
#define ST7789_BYTES_PER_PIXEL      2U

#ifdef __cplusplus
}
#endif

#endif /* ST7789_REGS_H */
