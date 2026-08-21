/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file display_ui_bridge.h
 *@brief 屏幕统一抽象层 — LVGL / u8g2 等第三方 UI 库桥接
 *@author H-000-H
 *@details
 *   本头提供**面向 UI 库回调**的入口函数，走统一 DISPLAY_CMD_* 命令，不依赖具体
 *   屏幕驱动，也不 include lvgl.h / u8g2.h（第三方库为可选依赖，未链接时不下载
 *   源码，故本头保持零依赖，仅用基础类型签名）。
 *   应用在使用 UI 库时，把本头函数作为 flush 回调（类型由应用按 UI 库签名适配）：
 *   - LVGL flush_cb   → display_lvgl_flush_cb()
 *   - u8g2 SendBuffer → display_u8g2_flush_fb()
 *   换屏仅需更换 device 指针；像素格式由调用方按面板信息（DISPLAY_CMD_GET_INFO）指定。
 */

#ifndef DISPLAY_UI_BRIDGE_H
#define DISPLAY_UI_BRIDGE_H

#include "device.h"
#include "display_drv.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief LVGL flush_cb 用：刷新一个区域（含端点坐标，对齐 lv_area_t）
     * @param disp 屏幕 device（应用将 struct device* 强转为 void* 传入）
     * @param x1/y1/x2/y2 区域（含端点）
     * @param px 像素缓冲（RGB565 或单色，取决于 format）
     * @param format enum display_color_format
     * @param timeout_ms 超时（ms）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int display_lvgl_flush_cb(void* disp, int16_t x1, int16_t y1, int16_t x2,
                                                   int16_t y2, const void* px, uint8_t format,
                                                   uint32_t timeout_ms)
    {
        int16_t w = (int16_t)(x2 - x1 + 1);
        int16_t h = (int16_t)(y2 - y1 + 1);
        struct display_draw_arg draw_arg;

        if (!disp || !px || x2 < x1 || y2 < y1)
            return VFS_ERR_INVAL;
        draw_arg.x = x1;
        draw_arg.y = y1;
        draw_arg.w = w;
        draw_arg.h = h;
        draw_arg.format = format;
        draw_arg.data = (const uint8_t*)px;
        return device_ioctl((struct device*)disp, DISPLAY_CMD_FLUSH, &draw_arg, sizeof(draw_arg),
                            timeout_ms);
    }

    /**
     * @brief u8g2 SendBuffer 后调用：整帧单色位图刷新
     * @param disp 屏幕 device（void*）
     * @param fb 整帧缓冲（page-major 单色）
     * @param len 缓冲长度（应为 w*h/8）
     * @param timeout_ms 超时（ms）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int display_u8g2_flush_fb(void* disp, const uint8_t* fb, size_t len,
                                                   uint32_t timeout_ms)
    {
        struct display_info_arg info;
        struct display_draw_arg draw_arg;

        if (!disp || !fb || len == 0U)
            return VFS_ERR_INVAL;
        if (device_ioctl((struct device*)disp, DISPLAY_CMD_GET_INFO, &info, sizeof(info), 0) !=
            VFS_OK)
            return VFS_ERR_IO;
        if (info.format != DISPLAY_FMT_MONO_1BPP)
            return VFS_ERR_NOTSUPP;
        if (len != (size_t)info.width * (size_t)info.height / 8U)
            return VFS_ERR_INVAL;
        draw_arg.x = 0;
        draw_arg.y = 0;
        draw_arg.w = (int16_t)info.width;
        draw_arg.h = (int16_t)info.height;
        draw_arg.format = info.format;
        draw_arg.data = fb;
        return device_ioctl((struct device*)disp, DISPLAY_CMD_FLUSH, &draw_arg, sizeof(draw_arg),
                            timeout_ms);
    }

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UI_BRIDGE_H */