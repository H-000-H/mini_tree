/**
 * SPDX-License-Identifier: Apache-2.0
 * @file st7789_bridge.h
 * @brief ST7789 ↔ LVGL / 应用层薄封装（不依赖 lvgl.h）
 *
 * 典型 LVGL9 flush_cb：
 * @code
 * static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
 * {
 *     st7789_lvgl_flush(g_lcd, a->x1, a->y1, a->x2, a->y2, px, 200);
 *     lv_display_flush_ready(disp);
 * }
 * @endcode
 */
#ifndef ST7789_BRIDGE_H
#define ST7789_BRIDGE_H

#include "device.h"
#include "st7789_drv.h"
#include "status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief LVGL 区域刷新（lv_area_t 含端点坐标）
     * @param pdev ST7789 device
     * @param x1/y1/x2/y2 区域（含端点）
     * @param color_map RGB565 打包缓冲
     * @param timeout_ms 超时（ms）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int st7789_lvgl_flush(struct device* pdev, int16_t x1, int16_t y1,
                                               int16_t x2, int16_t y2, const void* color_map,
                                               uint32_t timeout_ms)
    {
        struct st7789_flush_area_arg a;

        if (!pdev || !color_map || x2 < x1 || y2 < y1)
            return VFS_ERR_INVAL;
        a.x1 = x1;
        a.y1 = y1;
        a.x2 = x2;
        a.y2 = y2;
        a.color_map = (const uint8_t*)color_map;
        return device_ioctl(pdev, ST7789_CMD_FLUSH_AREA, &a, sizeof(a), timeout_ms);
    }

    /**
     * @brief 获取面板信息
     * @param info 输出面板信息
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int st7789_get_info(struct device* pdev, struct st7789_info_arg* info)
    {
        if (!pdev || !info)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, ST7789_CMD_GET_INFO, info, sizeof(*info), 0);
    }

    /**
     * @brief 设置背光亮度
     * @param brightness 亮度 0..255
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int st7789_set_backlight(struct device* pdev, uint8_t brightness)
    {
        struct st7789_backlight_arg a = {.brightness = brightness};
        if (!pdev)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, ST7789_CMD_SET_BACKLIGHT, &a, sizeof(a), 0);
    }

#ifdef __cplusplus
}
#endif

#endif /* ST7789_BRIDGE_H */
