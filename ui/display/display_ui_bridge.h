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
 *   - LVGL flush_cb   → display_lvgl_flush_callback()
 *   - u8g2 SendBuffer → display_u8g2_flush_frame_buffer()
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
     * @param[in] display_device 屏幕 device（应用将 struct device* 强转为 void* 传入）
     * @param[in] start_x 区域左上角 X（含）
     * @param[in] start_y 区域左上角 Y（含）
     * @param[in] end_x 区域右下角 X（含）
     * @param[in] end_y 区域右下角 Y（含）
     * @param[in] pixel_buffer 像素缓冲（RGB565 或单色，取决于 pixel_format）
     * @param[in] pixel_format enum display_color_format
     * @param[in] timeout_ms 超时（ms）
     * @return MINI_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int display_lvgl_flush_callback(void* display_device, int16_t start_x,
                                                         int16_t start_y, int16_t end_x,
                                                         int16_t end_y, const void* pixel_buffer,
                                                         uint8_t pixel_format, uint32_t timeout_ms)
    {
        int16_t area_width = (int16_t)(end_x - start_x + 1);
        int16_t area_height = (int16_t)(end_y - start_y + 1);
        struct display_draw_arg draw_argument;

        if (!display_device || !pixel_buffer || end_x < start_x || end_y < start_y)
            return MINI_ERR_INVAL;
        draw_argument.x = start_x;
        draw_argument.y = start_y;
        draw_argument.w = area_width;
        draw_argument.h = area_height;
        draw_argument.format = pixel_format;
        draw_argument.data = (const uint8_t*)pixel_buffer;
        return device_ioctl((struct device*)display_device, DISPLAY_CMD_FLUSH, &draw_argument,
                            sizeof(draw_argument), timeout_ms);
    }

    /**
     * @brief u8g2 SendBuffer 后调用：整帧单色位图刷新
     * @param[in] display_device 屏幕 device（void*）
     * @param[in] frame_buffer 整帧缓冲（page-major 单色）
     * @param[in] buffer_length 缓冲长度（应为 宽*高/8）
     * @param[in] timeout_ms 超时（ms）
     * @return MINI_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int display_u8g2_flush_frame_buffer(void* display_device,
                                                             const uint8_t* frame_buffer,
                                                             size_t buffer_length,
                                                             uint32_t timeout_ms)
    {
        struct display_info_arg panel_info;
        struct display_draw_arg draw_argument;

        if (!display_device || !frame_buffer || buffer_length == 0U)
            return MINI_ERR_INVAL;
        if (device_ioctl((struct device*)display_device, DISPLAY_CMD_GET_INFO, &panel_info,
                         sizeof(panel_info), 0) != MINI_OK)
            return MINI_ERR_IO;
        if (panel_info.format != DISPLAY_FMT_MONO_1BPP)
            return MINI_ERR_NOTSUPP;
        if (buffer_length != (size_t)panel_info.width * (size_t)panel_info.height / 8U)
            return MINI_ERR_INVAL;
        draw_argument.x = 0;
        draw_argument.y = 0;
        draw_argument.w = (int16_t)panel_info.width;
        draw_argument.h = (int16_t)panel_info.height;
        draw_argument.format = panel_info.format;
        draw_argument.data = frame_buffer;
        return device_ioctl((struct device*)display_device, DISPLAY_CMD_FLUSH, &draw_argument,
                            sizeof(draw_argument), timeout_ms);
    }

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UI_BRIDGE_H */
