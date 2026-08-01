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

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define ST7789_CMD_BASE              COMPAT_MAGIC(ST7789)
/** 矩形填充（arg: struct st7789_fill_rect_arg*） */
#define ST7789_CMD_FILL_RECT         (ST7789_CMD_BASE + 0x01)
/** 全屏填充（arg: struct st7789_fill_screen_arg*） */
#define ST7789_CMD_FILL_SCREEN       (ST7789_CMD_BASE + 0x02)
/** 位图绘制（arg: struct st7789_draw_bitmap_arg*） */
#define ST7789_CMD_DRAW_BITMAP       (ST7789_CMD_BASE + 0x03)
/** 设置背光（arg: struct st7789_backlight_arg*） */
#define ST7789_CMD_SET_BACKLIGHT     (ST7789_CMD_BASE + 0x04)
/** 获取面板信息（arg: struct st7789_info_arg*） */
#define ST7789_CMD_GET_INFO          (ST7789_CMD_BASE + 0x05)
/** LVGL 风格区域刷新：x1..x2 / y1..y2 含端点，像素 RGB565（arg: struct st7789_flush_area_arg*） */
#define ST7789_CMD_FLUSH_AREA        (ST7789_CMD_BASE + 0x06)
/** 命令总数 */
#define ST7789_CMD_COUNT             6

/** @brief 矩形填充参数 (RGB565) */
struct st7789_fill_rect_arg
{
    int16_t  x;      /**< 左上角 X */
    int16_t  y;      /**< 左上角 Y */
    int16_t  w;      /**< 宽（像素） */
    int16_t  h;      /**< 高（像素） */
    uint16_t color;  /**< RGB565 颜色 */
};

/** @brief 全屏填充参数 (RGB565) */
struct st7789_fill_screen_arg
{
    uint16_t color;  /**< RGB565 颜色 */
};

/** @brief 位图绘制参数 (RGB565，字节序与面板一致) */
struct st7789_draw_bitmap_arg
{
    int16_t        x;      /**< 左上角 X */
    int16_t        y;      /**< 左上角 Y */
    int16_t        w;      /**< 宽（像素） */
    int16_t        h;      /**< 高（像素） */
    const uint8_t* data;   /**< 长度 = w * h * ST7789_BYTES_PER_PIXEL */
};

/**
 * @brief LVGL flush 区域（含端点坐标，对齐 lv_area_t）
 * @note color_map 为 RGB565 打包缓冲，长度 = (x2-x1+1)*(y2-y1+1)*2
 */
struct st7789_flush_area_arg
{
    int16_t        x1;         /**< 区域左（含） */
    int16_t        y1;         /**< 区域上（含） */
    int16_t        x2;         /**< 区域右（含） */
    int16_t        y2;         /**< 区域下（含） */
    const uint8_t* color_map;  /**< RGB565 打包缓冲 */
};

/** @brief 背光亮度 (0..255) */
struct st7789_backlight_arg
{
    uint8_t brightness;  /**< 亮度 0..255 */
};

/** @brief 面板信息 */
struct st7789_info_arg
{
    int16_t  width;         /**< 宽（像素） */
    int16_t  height;        /**< 高（像素） */
    uint16_t color_format;  /**< ST7789_COLOR_FORMAT_RGB565 */
};

#ifdef __cplusplus
}
#endif

#endif /* ST7789_DRV_H */
