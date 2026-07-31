/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ssd1306_drv.h
 * @brief SSD1306 OLED ioctl
 *
 * 第三方库接入（驱动不 #include u8g2.h）：
 * - u8g2 / u8x8：SSD1306_CMD_WRITE_CMD / WRITE_DATA / FLUSH_FB
 *   或 ssd1306_u8g2_flush_fb() / ssd1306_write_cmd() / ssd1306_write_data()
 * - 简单位图：SSD1306_CMD_DRAW（原始 I2C 载荷，含 control byte）
 */
#ifndef SSD1306_DRV_H
#define SSD1306_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "ssd1306_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_CMD_BASE         COMPAT_MAGIC(SSD1306)
#define SSD1306_CMD_INIT         (SSD1306_CMD_BASE + 0x01)
#define SSD1306_CMD_FILL         (SSD1306_CMD_BASE + 0x02)
#define SSD1306_CMD_DRAW         (SSD1306_CMD_BASE + 0x03)
#define SSD1306_CMD_GET_INFO     (SSD1306_CMD_BASE + 0x04)
#define SSD1306_CMD_WRITE_CMD    (SSD1306_CMD_BASE + 0x05)
#define SSD1306_CMD_WRITE_DATA   (SSD1306_CMD_BASE + 0x06)
#define SSD1306_CMD_FLUSH_FB     (SSD1306_CMD_BASE + 0x07)
#define SSD1306_CMD_SET_CONTRAST (SSD1306_CMD_BASE + 0x08)
#define SSD1306_CMD_COUNT        8

/** 原始 I2C 载荷（首字节通常为 CTRL_CMD/DATA） */
struct ssd1306_draw
{
    const uint8_t* buf;
    size_t         len;
};

struct ssd1306_info
{
    uint16_t width;
    uint16_t height;
    uint16_t pages;
    uint16_t fb_size;
};

struct ssd1306_byte
{
    uint8_t value;
};

struct ssd1306_data
{
    const uint8_t* buf;
    size_t         len;
};

/** 整帧 GDDRAM：len == SSD1306_FB_SIZE，按 page-major */
struct ssd1306_fb
{
    const uint8_t* buf;
    size_t         len;
};

struct ssd1306_contrast
{
    uint8_t value; /* 0..255 */
};

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_DRV_H */
