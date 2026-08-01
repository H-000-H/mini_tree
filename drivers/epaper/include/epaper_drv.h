/**
 * SPDX-License-Identifier: Apache-2.0
 * @file epaper_drv.h
 * @brief 电子纸 — 可接 LVGL 单色 flush / 自绘缓冲
 */
#ifndef EPAPER_DRV_H
#define EPAPER_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "epaper_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define EPAPER_CMD_BASE         COMPAT_MAGIC(EPAPER)
/** 初始化面板（含复位与 LUT 装载） */
#define EPAPER_CMD_INIT         (EPAPER_CMD_BASE + 0x01)
/** 全屏刷新（白/黑） */
#define EPAPER_CMD_CLEAR        (EPAPER_CMD_BASE + 0x02)
/** 刷入整帧位图（arg: struct epaper_bitmap*） */
#define EPAPER_CMD_DRAW_BITMAP  (EPAPER_CMD_BASE + 0x03)
/** 获取面板信息（arg: struct epaper_info*） */
#define EPAPER_CMD_GET_INFO     (EPAPER_CMD_BASE + 0x04)
/** 命令总数 */
#define EPAPER_CMD_COUNT        4

/** @brief 整帧位图参数（bpp 由 GET_INFO 决定） */
struct epaper_bitmap
{
    const uint8_t* data;  /**< 位图缓冲 */
    size_t         len;   /**< 位图长度 */
};

/** @brief 面板信息 */
struct epaper_info
{
    uint16_t width;   /**< 宽（像素） */
    uint16_t height;  /**< 高（像素） */
    uint16_t bpp;     /**< 每像素比特数 */
};

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_DRV_H */
