/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sh1106_drv.h
 * @brief SH1106 OLED — 接口对齐 SSD1306，便于 u8g2 共用胶水层
 */
#ifndef SH1106_DRV_H
#define SH1106_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "sh1106_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define SH1106_CMD_BASE         COMPAT_MAGIC(SH1106)
/** 初始化面板（含电荷泵等配置序列） */
#define SH1106_CMD_INIT         (SH1106_CMD_BASE + 0x01)
/** 全屏填充（arg: struct sh1106_byte*，0x00/0xFF） */
#define SH1106_CMD_FILL         (SH1106_CMD_BASE + 0x02)
/** 获取面板几何信息（arg: struct sh1106_info*） */
#define SH1106_CMD_GET_INFO     (SH1106_CMD_BASE + 0x03)
/** 写单条命令（arg: struct sh1106_byte*） */
#define SH1106_CMD_WRITE_CMD    (SH1106_CMD_BASE + 0x04)
/** 写显示数据（arg: struct sh1106_data*） */
#define SH1106_CMD_WRITE_DATA   (SH1106_CMD_BASE + 0x05)
/** 整帧刷 GDDRAM（arg: struct sh1106_fb*） */
#define SH1106_CMD_FLUSH_FB     (SH1106_CMD_BASE + 0x06)
/** 设置对比度（arg: struct sh1106_contrast*） */
#define SH1106_CMD_SET_CONTRAST (SH1106_CMD_BASE + 0x07)
/** 命令总数 */
#define SH1106_CMD_COUNT        7

/** @brief SH1106 面板几何信息 */
struct sh1106_info
{
    uint16_t width;       /**< 宽度（像素） */
    uint16_t height;      /**< 高度（像素） */
    uint16_t pages;       /**< 页数（8 像素/页） */
    uint16_t fb_size;     /**< 整帧缓冲字节数 */
    uint16_t col_offset;  /**< 列偏移（RAM 与屏列错位补偿） */
};

struct sh1106_byte { uint8_t value; };                                /**< 单命令字节 */
struct sh1106_data { const uint8_t* buf; size_t len; };               /**< 显示数据载荷 */
struct sh1106_fb   { const uint8_t* buf; size_t len; };               /**< 整帧 GDDRAM（len == SH1106_FB_SIZE） */
struct sh1106_contrast { uint8_t value; };                            /**< 对比度 0..255 */

#ifdef __cplusplus
}
#endif

#endif /* SH1106_DRV_H */
