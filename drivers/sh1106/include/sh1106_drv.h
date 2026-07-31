/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sh1106_drv.h
 * @brief SH1106 OLED — 接口对齐 SSD1306，便于 u8g2 共用胶水层
 */
#ifndef SH1106_DRV_H
#define SH1106_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "sh1106_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH1106_CMD_BASE         COMPAT_MAGIC(SH1106)
#define SH1106_CMD_INIT         (SH1106_CMD_BASE + 0x01)
#define SH1106_CMD_FILL         (SH1106_CMD_BASE + 0x02)
#define SH1106_CMD_GET_INFO     (SH1106_CMD_BASE + 0x03)
#define SH1106_CMD_WRITE_CMD    (SH1106_CMD_BASE + 0x04)
#define SH1106_CMD_WRITE_DATA   (SH1106_CMD_BASE + 0x05)
#define SH1106_CMD_FLUSH_FB     (SH1106_CMD_BASE + 0x06)
#define SH1106_CMD_SET_CONTRAST (SH1106_CMD_BASE + 0x07)
#define SH1106_CMD_COUNT        7

struct sh1106_info
{
    uint16_t width;
    uint16_t height;
    uint16_t pages;
    uint16_t fb_size;
    uint16_t col_offset;
};

struct sh1106_byte { uint8_t value; };
struct sh1106_data { const uint8_t* buf; size_t len; };
struct sh1106_fb   { const uint8_t* buf; size_t len; };
struct sh1106_contrast { uint8_t value; };

#ifdef __cplusplus
}
#endif

#endif /* SH1106_DRV_H */
