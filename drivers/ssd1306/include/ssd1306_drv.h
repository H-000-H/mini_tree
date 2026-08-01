/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ssd1306_drv.h
 * @brief SSD1306 OLED ioctl
 *
 * 第三方库接入（驱动不 #include u8g2.h）：
 * - u8g2 / u8x8：SSD1306_CMD_WRITE_CMD / WRITE_DATA / FLUSH_FB
 *   或 ssd1306_u8g2_flush_fb() / ssd1306_write_cmd() / ssd1306_write_data()
 * - 简单位图：SSD1306_CMD_DRAW（原始 I2C 载荷，含 control byte）
 */
#ifndef SSD1306_DRV_H
#define SSD1306_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "ssd1306_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define SSD1306_CMD_BASE         COMPAT_MAGIC(SSD1306)
/** 初始化面板（含电荷泵等配置序列） */
#define SSD1306_CMD_INIT         (SSD1306_CMD_BASE + 0x01)
/** 全屏填充（arg: int*，0/1） */
#define SSD1306_CMD_FILL         (SSD1306_CMD_BASE + 0x02)
/** 原始 I2C 载荷直写（arg: struct ssd1306_draw*） */
#define SSD1306_CMD_DRAW         (SSD1306_CMD_BASE + 0x03)
/** 获取面板几何信息（arg: struct ssd1306_info*） */
#define SSD1306_CMD_GET_INFO     (SSD1306_CMD_BASE + 0x04)
/** 写单条命令（arg: struct ssd1306_byte*） */
#define SSD1306_CMD_WRITE_CMD    (SSD1306_CMD_BASE + 0x05)
/** 写显示数据（arg: struct ssd1306_data*） */
#define SSD1306_CMD_WRITE_DATA   (SSD1306_CMD_BASE + 0x06)
/** 整帧刷 GDDRAM（arg: struct ssd1306_fb*） */
#define SSD1306_CMD_FLUSH_FB     (SSD1306_CMD_BASE + 0x07)
/** 设置对比度（arg: struct ssd1306_contrast*） */
#define SSD1306_CMD_SET_CONTRAST (SSD1306_CMD_BASE + 0x08)
/** 命令总数 */
#define SSD1306_CMD_COUNT        8

/** 原始 I2C 载荷（首字节通常为 CTRL_CMD/DATA） */
struct ssd1306_draw
{
    const uint8_t* buf;  /**< 载荷缓冲 */
    size_t         len;  /**< 载荷长度 */
};

struct ssd1306_info
{
    uint16_t width;    /**< 宽度（像素） */
    uint16_t height;   /**< 高度（像素） */
    uint16_t pages;    /**< 页数（8 像素/页） */
    uint16_t fb_size;  /**< 整帧缓冲字节数 */
};

struct ssd1306_byte
{
    uint8_t value;  /**< 命令字节 */
};

struct ssd1306_data
{
    const uint8_t* buf;  /**< 数据缓冲 */
    size_t         len;  /**< 数据长度 */
};

/** 整帧 GDDRAM：len == SSD1306_FB_SIZE，按 page-major */
struct ssd1306_fb
{
    const uint8_t* buf;  /**< 帧缓冲指针 */
    size_t         len;  /**< 帧长度 */
};

struct ssd1306_contrast
{
    uint8_t value;  /**< 对比度 0..255 */
};

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_DRV_H */
