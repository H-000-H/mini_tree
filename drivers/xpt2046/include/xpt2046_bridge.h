/**
 * SPDX-License-Identifier: Apache-2.0
 * @file xpt2046_bridge.h
 * @brief XPT2046 ↔ LVGL indev 读点薄封装（不依赖 lvgl.h）
 */
#ifndef XPT2046_BRIDGE_H
#define XPT2046_BRIDGE_H

#include "xpt2046_drv.h"
#include "device.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

COMPAT_STATIC_INLINE int xpt2046_lvgl_read(struct device* dev, struct xpt2046_xy* out, uint32_t timeout_ms)
{
    if (!dev || !out)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, XPT2046_CMD_READ_XY, out, sizeof(*out), timeout_ms);
}

#ifdef __cplusplus
}
#endif

#endif /* XPT2046_BRIDGE_H */
