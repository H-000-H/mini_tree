/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ssd1306_bridge.h
 * @brief SSD1306 ↔ u8g2/u8x8 / 应用层薄封装（不依赖 u8g2.h）
 *
 * u8g2 全缓冲模式可在 SendBuffer 后调用：
 *   ssd1306_u8g2_flush_fb(dev, u8g2_GetBufferPtr(&u8g2), SSD1306_FB_SIZE, 100);
 *
 * 低层 byte 回调可拆成 write_cmd / write_data。
 */
#ifndef SSD1306_BRIDGE_H
#define SSD1306_BRIDGE_H

#include "ssd1306_drv.h"
#include "device.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 写单条命令（u8g2 u8x8_cmd 回调用）
 * @param dev SSD1306 device
 * @param cmd 命令字节
 * @param timeout_ms 超时（ms）
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int ssd1306_write_cmd(struct device* dev, uint8_t cmd, uint32_t timeout_ms)
{
    struct ssd1306_byte a = { .value = cmd };
    if (!dev)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SSD1306_CMD_WRITE_CMD, &a, sizeof(a), timeout_ms);
}

/**
 * @brief 写显示数据
 * @param dev SSD1306 device
 * @param buf 数据缓冲
 * @param len 数据长度
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int ssd1306_write_data(struct device* dev, const uint8_t* buf, size_t len, uint32_t timeout_ms)
{
    struct ssd1306_data a = { .buf = buf, .len = len };
    if (!dev || !buf || len == 0U)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SSD1306_CMD_WRITE_DATA, &a, sizeof(a), timeout_ms);
}

/**
 * @brief 整帧刷新（u8g2 SendBuffer 后调用）
 * @param fb 帧缓冲指针（page-major）
 * @param len 帧缓冲长度（应为 SSD1306_FB_SIZE）
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int ssd1306_u8g2_flush_fb(struct device* dev, const uint8_t* fb, size_t len, uint32_t timeout_ms)
{
    struct ssd1306_fb a = { .buf = fb, .len = len };
    if (!dev || !fb)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SSD1306_CMD_FLUSH_FB, &a, sizeof(a), timeout_ms);
}

/**
 * @brief 获取面板几何信息
 * @param info 输出几何信息
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int ssd1306_get_info(struct device* dev, struct ssd1306_info* info)
{
    if (!dev || !info)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SSD1306_CMD_GET_INFO, info, sizeof(*info), 0);
}

/**
 * @brief 设置对比度
 * @param v 对比度 0..255
 * @return VFS_OK 或 VFS_ERR_*
 */
COMPAT_STATIC_INLINE int ssd1306_set_contrast(struct device* dev, uint8_t v, uint32_t timeout_ms)
{
    struct ssd1306_contrast a = { .value = v };
    if (!dev)
        return VFS_ERR_INVAL;
    return device_ioctl(dev, SSD1306_CMD_SET_CONTRAST, &a, sizeof(a), timeout_ms);
}

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_BRIDGE_H */
