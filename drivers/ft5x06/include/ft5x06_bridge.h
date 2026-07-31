/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ft5x06_bridge.h
 * @brief FT5x06 ↔ LVGL indev 读点薄封装（不依赖 lvgl.h）
 */
#ifndef FT5X06_BRIDGE_H
#define FT5X06_BRIDGE_H

#include "ft5x06_drv.h"
#include "device.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

COMPAT_STATIC_INLINE int ft5x06_lvgl_read(struct device* dev, struct ft5x06_touch* out, uint32_t timeout_ms)
{
    if (!dev || !out)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, FT5X06_CMD_READ_TOUCH, out, sizeof(*out), timeout_ms);
}

#ifdef __cplusplus
}
#endif

#endif /* FT5X06_BRIDGE_H */
