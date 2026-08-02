/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sh1106_bridge.h
 * @brief SH1106 ↔ u8g2 薄封装（不依赖 u8g2.h）
 */
#ifndef SH1106_BRIDGE_H
#define SH1106_BRIDGE_H

#include "device.h"
#include "sh1106_drv.h"
#include "status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 写单条命令（u8g2 u8x8_cmd 回调用）
     * @param pdev SH1106 device
     * @param cmd 命令字节
     * @param timeout_ms 超时（ms）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int sh1106_write_cmd(struct device* pdev, uint8_t cmd, uint32_t timeout_ms)
    {
        struct sh1106_byte a = {.value = cmd};
        if (!pdev)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, SH1106_CMD_WRITE_CMD, &a, sizeof(a), timeout_ms);
    }

    /**
     * @brief 写显示数据
     * @param pdev SH1106 device
     * @param buf 数据缓冲
     * @param len 数据长度
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int sh1106_write_data(struct device* pdev, const uint8_t* buf, size_t len,
                                               uint32_t timeout_ms)
    {
        struct sh1106_data a = {.buf = buf, .len = len};
        if (!pdev || !buf || len == 0U)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, SH1106_CMD_WRITE_DATA, &a, sizeof(a), timeout_ms);
    }

    /**
     * @brief 整帧刷新（u8g2 SendBuffer 后调用）
     * @param fb 帧缓冲指针（page-major）
     * @param len 帧缓冲长度（应为 SH1106_FB_SIZE）
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int sh1106_u8g2_flush_fb(struct device* pdev, const uint8_t* fb,
                                                  size_t len, uint32_t timeout_ms)
    {
        struct sh1106_fb a = {.buf = fb, .len = len};
        if (!pdev || !fb)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, SH1106_CMD_FLUSH_FB, &a, sizeof(a), timeout_ms);
    }

    /**
     * @brief 获取面板几何信息
     * @param info 输出几何信息
     * @return VFS_OK 或 VFS_ERR_*
     */
    COMPAT_STATIC_INLINE int sh1106_get_info(struct device* pdev, struct sh1106_info* info)
    {
        if (!pdev || !info)
            return VFS_ERR_INVAL;
        return device_ioctl(pdev, SH1106_CMD_GET_INFO, info, sizeof(*info), 0);
    }

#ifdef __cplusplus
}
#endif

#endif /* SH1106_BRIDGE_H */
