/**
 * SPDX-License-Identifier: Apache-2.0
 * @file max7219_drv.h
 * @brief MAX7219 LED 点阵驱动 ioctl 命令与参数结构
 *
 * 挂在 SPI 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef MAX7219_DRV_H
#define MAX7219_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "max7219_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define MAX7219_CMD_BASE      COMPAT_MAGIC(MAX7219)
/** 初始化（含扫描/亮度/解码配置） */
#define MAX7219_CMD_INIT      (MAX7219_CMD_BASE + 0x01)
/** 设置单个 digit（arg: struct max7219_digit*） */
#define MAX7219_CMD_SET_DIGIT (MAX7219_CMD_BASE + 0x02)
/** 清屏 */
#define MAX7219_CMD_CLEAR     (MAX7219_CMD_BASE + 0x03)
/** 整帧刷点阵（arg: struct max7219_fb*） */
#define MAX7219_CMD_FLUSH_FB  (MAX7219_CMD_BASE + 0x04)
/** 命令总数 */
#define MAX7219_CMD_COUNT     4

/** @brief 单 digit 写入参数 */
struct max7219_digit
{
    uint8_t digit;  /**< 位号 1..8 */
    uint8_t value;  /**< 段值（按芯片段编码） */
};

/** 8 行点阵，每字节一行（MSB=左） */
struct max7219_fb
{
    const uint8_t* rows;  /**< 长度 MAX7219_MATRIX_BYTES */
    size_t         len;   /**< 帧长度 */
};

#ifdef __cplusplus
}
#endif

#endif /* MAX7219_DRV_H */
