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

COMPAT_STATIC_INLINE int epaper_flush_bitmap(struct device* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    struct epaper_bitmap a = { .data = data, .len = len };
    if (!dev || !data || len == 0U)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, EPAPER_CMD_DRAW_BITMAP, &a, sizeof(a), timeout_ms);
}

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
