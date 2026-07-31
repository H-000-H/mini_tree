/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sh1106_bridge.h
 * @brief SH1106 ↔ u8g2 薄封装（不依赖 u8g2.h）
 */
#ifndef SH1106_BRIDGE_H
#define SH1106_BRIDGE_H

#include "sh1106_drv.h"
#include "device.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

COMPAT_STATIC_INLINE int sh1106_write_cmd(struct device* dev, uint8_t cmd, uint32_t timeout_ms)
{
    struct sh1106_byte a = { .value = cmd };
    if (!dev)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SH1106_CMD_WRITE_CMD, &a, sizeof(a), timeout_ms);
}

COMPAT_STATIC_INLINE int sh1106_write_data(struct device* dev, const uint8_t* buf, size_t len, uint32_t timeout_ms)
{
    struct sh1106_data a = { .buf = buf, .len = len };
    if (!dev || !buf || len == 0U)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SH1106_CMD_WRITE_DATA, &a, sizeof(a), timeout_ms);
}

COMPAT_STATIC_INLINE int sh1106_u8g2_flush_fb(struct device* dev, const uint8_t* fb, size_t len, uint32_t timeout_ms)
{
    struct sh1106_fb a = { .buf = fb, .len = len };
    if (!dev || !fb)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SH1106_CMD_FLUSH_FB, &a, sizeof(a), timeout_ms);
}

COMPAT_STATIC_INLINE int sh1106_get_info(struct device* dev, struct sh1106_info* info)
{
    if (!dev || !info)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SH1106_CMD_GET_INFO, info, sizeof(*info), 0);
}

#ifdef __cplusplus
}
#endif

#endif /* SH1106_BRIDGE_H */
