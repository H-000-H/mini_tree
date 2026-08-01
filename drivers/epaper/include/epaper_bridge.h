/**
 * SPDX-License-Identifier: Apache-2.0
 * @file epaper_bridge.h
 * @brief 电子纸 ↔ LVGL 单色 / 应用缓冲薄封装
 */
#ifndef EPAPER_BRIDGE_H
#define EPAPER_BRIDGE_H

#include "epaper_drv.h"
#include "device.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 刷入整帧位图（LVGL flush / 自绘缓冲用）
 * @param dev 电子纸 device
 * @param data 位图缓冲
 * @param len 位图长度
 * @param timeout_ms 超时（ms）
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int epaper_flush_bitmap(struct device* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct epaper_bitmap a = { .data = data, .len = len };
    if (!dev || !data || len == 0U)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, EPAPER_CMD_DRAW_BITMAP, &a, sizeof(a), timeout_ms);
}

/**
 * @brief 获取面板信息（宽/高/bpp）
 * @param info 输出面板信息
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int epaper_get_info(struct device* dev, struct epaper_info* info)
{
    if (!dev || !info)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, EPAPER_CMD_GET_INFO, info, sizeof(*info), 0);
}

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_BRIDGE_H */
