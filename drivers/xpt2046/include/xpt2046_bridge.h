/**
 * SPDX-License-Identifier: Apache-2.0
 * @file xpt2046_bridge.h
 * @brief XPT2046 ↔ LVGL indev 读点薄封装（不依赖 lvgl.h）
 */
#ifndef XPT2046_BRIDGE_H
#define XPT2046_BRIDGE_H

#include "device.h"
#include "status.h"
#include "xpt2046_drv.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 读取坐标/按压状态（LVGL indev read_cb 用）
     * @param pdev XPT2046 device
     * @param out 输出坐标结果
     * @param timeout_ms 超时（ms）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int xpt2046_lvgl_read(struct device* pdev, struct xpt2046_xy* out,
                                               uint32_t timeout_ms)
    {
        if (!pdev || !out)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, XPT2046_CMD_READ_XY, out, sizeof(*out), timeout_ms);
    }

#ifdef __cplusplus
}
#endif

#endif /* XPT2046_BRIDGE_H */
