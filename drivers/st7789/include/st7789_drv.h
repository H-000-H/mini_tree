/**
 * SPDX-License-Identifier: Apache-2.0
 * @file st7789_drv.h
 * @brief ST7789 LCD — ioctl 接口（有 CS / 无 CS 共用）
 *
 * 第三方库接入（驱动本身不 #include LVGL）：
 * - LVGL flush_cb → ST7789_CMD_FLUSH_AREA / st7789_lvgl_flush()
 * - 全屏/局部位图 → ST7789_CMD_DRAW_BITMAP
 * - 分辨率 → ST7789_CMD_GET_INFO
 * - 背光 → ST7789_CMD_SET_BACKLIGHT
 */
#ifndef ST7789_DRV_H
#define ST7789_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "st7789_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST7789_CMD_BASE              COMPAT_MAGIC(ST7789)
#define ST7789_CMD_FILL_RECT         (ST7789_CMD_BASE + 0x01)
#define ST7789_CMD_FILL_SCREEN       (ST7789_CMD_BASE + 0x02)
#define ST7789_CMD_DRAW_BITMAP       (ST7789_CMD_BASE + 0x03)
#define ST7789_CMD_SET_BACKLIGHT     (ST7789_CMD_BASE + 0x04)
#define ST7789_CMD_GET_INFO          (ST7789_CMD_BASE + 0x05)
/** LVGL 风格区域刷新：x1..x2 / y1..y2 含端点，像素 RGB565 */
#define ST7789_CMD_FLUSH_AREA        (ST7789_CMD_BASE + 0x06)
#define ST7789_CMD_COUNT             6

/** @brief 矩形填充参数 (RGB565) */
struct st7789_fill_rect_arg
{
    int16_t  x;
    int16_t  y;
    int16_t  w;
    int16_t  h;
    uint16_t color;
};

/** @brief 全屏填充参数 (RGB565) */
struct st7789_fill_screen_arg
{
    uint16_t color;
};

/** @brief 位图绘制参数 (RGB565，字节序与面板一致) */
struct st7789_draw_bitmap_arg
{
    int16_t        x;
    int16_t        y;
    int16_t        w;
    int16_t        h;
    const uint8_t* data; /**< 长度 = w * h * ST7789_BYTES_PER_PIXEL */
};

/**
 * @brief LVGL flush 区域（含端点坐标，对齐 lv_area_t）
 * @note color_map 为 RGB565 打包缓冲，长度 = (x2-x1+1)*(y2-y1+1)*2
 */
struct st7789_flush_area_arg
{
    int16_t        x1;
    int16_t        y1;
    int16_t        x2;
    int16_t        y2;
    const uint8_t* color_map;
};

/** @brief 背光亮度 (0..255) */
struct st7789_backlight_arg
{
    uint8_t brightness;
};

/** @brief 面板信息 */
struct st7789_info_arg
{
    int16_t  width;
    int16_t  height;
    uint16_t color_format; /**< ST7789_COLOR_FORMAT_RGB565 */
};

#ifdef __cplusplus
}
#endif

#endif /* ST7789_DRV_H */
