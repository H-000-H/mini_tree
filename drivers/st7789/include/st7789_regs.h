/**
 * SPDX-License-Identifier: Apache-2.0
 * @file st7789_regs.h
 * @brief ST7789 面板命令 / 时序常量（避免 .c 内散落魔术字）
 */
#ifndef ST7789_REGS_H
#define ST7789_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 面板命令 */
#define ST7789_REG_SWRESET   0x01U  /**< 软件复位 */
#define ST7789_REG_SLPOUT    0x11U  /**< 退出睡眠 */
#define ST7789_REG_NORON     0x13U  /**< 正常显示模式 */
#define ST7789_REG_INVON     0x21U  /**< 反转显示 */
#define ST7789_REG_DISPOFF   0x28U  /**< 关闭显示 */
#define ST7789_REG_DISPON    0x29U  /**< 打开显示 */
#define ST7789_REG_CASET     0x2AU  /**< 列地址窗口 */
#define ST7789_REG_RASET     0x2BU  /**< 行地址窗口 */
#define ST7789_REG_RAMWR     0x2CU  /**< GRAM 写入 */
#define ST7789_REG_MADCTL    0x36U  /**< 内存访问控制（旋转/镜像） */
#define ST7789_REG_COLMOD    0x3AU  /**< 像素格式 */
#define ST7789_REG_RAMCTRL   0xB0U  /**< RAM 控制（字节序） */

/* COLMOD / MADCTL 常用值 */
#define ST7789_COLMOD_16BIT  0x55U  /**< RGB565 */
#define ST7789_MADCTL_DEFAULT 0x00U /**< 默认方向 */
#define ST7789_RAMCTRL_BE0   0x00U  /**< 小端 */
#define ST7789_RAMCTRL_BE1   0xF0U  /**< 大端（RGB565 常用） */

/* 驱动内部缓冲 / 超时 */
#define ST7789_BLOCK_BUF_SIZE     2048U /**< 分块填充缓冲大小 */
#define ST7789_DEFAULT_CHUNK      512U  /**< 默认单次 SPI 传输上限 */
#define ST7789_TIMEOUT_CMD_MS     100U  /**< 命令超时 */
#define ST7789_TIMEOUT_IO_MS      500U  /**< IO 超时 */
#define ST7789_MAX_WIDTH          320   /**< 面板最大宽度 */
#define ST7789_BL_ARR_FALLBACK    1023U /**< 背光 ARR 回退值 */

/* 像素格式（对接 LVGL COLOR_FORMAT_RGB565） */
#define ST7789_COLOR_FORMAT_RGB565  16  /**< LVGL RGB565 格式值 */
#define ST7789_BYTES_PER_PIXEL      2U  /**< 每像素字节数 */

#ifdef __cplusplus
}
#endif

#endif /* ST7789_REGS_H */
