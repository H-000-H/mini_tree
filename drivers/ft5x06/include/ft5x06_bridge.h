/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ft5x06_bridge.h
 *@brief FT5x06 ↔ LVGL indev 读点薄封装（不依赖 lvgl.h）
 *@author H-000-H

 */

#ifndef FT5X06_BRIDGE_H
#define FT5X06_BRIDGE_H

#include "device.h"
#include "ft5x06_drv.h"
#include "status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 读取触摸点（LVGL indev read_cb 用）
     * @param pdev FT5x06 device
     * @param out 输出触摸结果
     * @param timeout_ms 超时（ms）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int ft5x06_lvgl_read(struct device* pdev, struct ft5x06_touch* out,
                                              uint32_t timeout_ms)
    {
        if (!pdev || !out)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, FT5X06_CMD_READ_TOUCH, out, sizeof(*out), timeout_ms);
    }

#ifdef __cplusplus
}
#endif

#endif /* FT5X06_BRIDGE_H */
