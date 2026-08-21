/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file display_drv.h
 *@brief 屏幕统一抽象层 — ioctl 命令与参数结构（跨彩色/单色屏）
 *@author H-000-H
 *@details
 *   本头定义**唯一一套**屏幕命令，供 ST7789 / SSD1306 / SH1106 / EPAPER 等驱动
 *   统一实现。上层（LVGL / u8g2 / 应用）只调用这套命令，换屏仅需更换 device 节点。
 *   像素格式用 color_format 表达（见 enum display_color_format），驱动按格式
 *   各自解析像素，上层无需关心屏是 RGB 还是单色。
 *   第三方库接入：
 *   - LVGL flush_cb → DISPLAY_CMD_FLUSH / DISPLAY_CMD_DRAW_AREA
 *   - 全屏/局部位图 → DISPLAY_CMD_DRAW_AREA
 *   - 分辨率/格式 → DISPLAY_CMD_GET_INFO
 *   - 背光/对比度 → DISPLAY_CMD_SET_BRIGHTNESS
 */

#ifndef DISPLAY_DRV_H
#define DISPLAY_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** 屏幕像素格式枚举 */
    enum display_color_format
    {
        DISPLAY_FMT_MONO_1BPP = 0, /**< 1bpp 单色位图（SSD1306 / SH1106 / EPAPER） */
        DISPLAY_FMT_RGB565, /**< 16bpp 彩色 RGB565（ST7789 等 TFT） */
    };

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define DISPLAY_CMD_BASE COMPAT_MAGIC(DISPLAY)
/** 获取面板信息（arg: struct display_info_arg*） */
#define DISPLAY_CMD_GET_INFO (DISPLAY_CMD_BASE + 0x01)
/** 全屏清屏（arg: struct display_clear_arg*） */
#define DISPLAY_CMD_CLEAR (DISPLAY_CMD_BASE + 0x02)
/** 矩形填充（arg: struct display_rect_arg*） */
#define DISPLAY_CMD_FILL_RECT (DISPLAY_CMD_BASE + 0x03)
/** 区域位图绘制（arg: struct display_draw_arg*，含像素格式） */
#define DISPLAY_CMD_DRAW_AREA (DISPLAY_CMD_BASE + 0x04)
/** 整帧/区域刷新（arg: struct display_draw_arg*） */
#define DISPLAY_CMD_FLUSH (DISPLAY_CMD_BASE + 0x05)
/** 设置亮度（arg: struct display_bright_arg*） */
#define DISPLAY_CMD_SET_BRIGHTNESS (DISPLAY_CMD_BASE + 0x06)
/** 命令总数 */
#define DISPLAY_CMD_COUNT 6

    /** @brief 面板信息 */
    struct display_info_arg
    {
        uint16_t width; /**< 宽（像素） */
        uint16_t height; /**< 高（像素） */
        uint8_t format; /**< enum display_color_format */
    };

    /** @brief 全屏清屏参数 */
    struct display_clear_arg
    {
        uint8_t value; /**< 单色屏：0=灭 1=亮；彩色屏：取低字节作为颜色 */
    };

    /** @brief 矩形填充参数 */
    struct display_rect_arg
    {
        int16_t x; /**< 左上角 X */
        int16_t y; /**< 左上角 Y */
        int16_t w; /**< 宽（像素） */
        int16_t h; /**< 高（像素） */
        uint16_t color; /**< 彩色屏：RGB565；单色屏：0/1 */
    };

    /**
     * @brief 区域位图绘制参数
     * @note format 决定 data 解析方式：MONO_1BPP 为 page-major 单色，
     *       RGB565 为每像素 2 字节打包。单色屏仅支持全屏（x=0,y=0,w=宽,h=高）。
     */
    struct display_draw_arg
    {
        int16_t x; /**< 左上角 X */
        int16_t y; /**< 左上角 Y */
        int16_t w; /**< 宽（像素） */
        int16_t h; /**< 高（像素） */
        uint8_t format; /**< enum display_color_format */
        const uint8_t* data; /**< 像素缓冲 */
    };

    /** @brief 亮度参数（0..255） */
    struct display_bright_arg
    {
        uint8_t value; /**< 亮度 0..255；OLED 映射为对比度 */
    };

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_DRV_H */
