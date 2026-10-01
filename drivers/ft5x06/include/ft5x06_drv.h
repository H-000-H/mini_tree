/**
 * @file ft5x06_drv.h
 * @author H-000-H
 * @brief FT5x06 电容触摸驱动 ioctl 命令与采样结构
 * @note 挂在 I2C 总线 client 下的 VFS 设备驱动；
 * @note 业务经 device_open/ioctl/close 访问。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef FT5X06_DRV_H
#define FT5X06_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define FT5X06_CMD_BASE MINI_MAGIC(FT5X06)
/** 读取触摸点（arg: struct ft5x06_touch*） */
#define FT5X06_CMD_READ_TOUCH (FT5X06_CMD_BASE + 0x01)
/** 命令总数 */
#define FT5X06_CMD_COUNT 1

/** @brief FT5x06 触摸结果（首触点坐标） */
struct ft5x06_touch
{
    uint8_t  points; /**< 有效触点数量（0=无触摸） */
    uint16_t pos_x;  /**< 首触点 X 坐标 */
    uint16_t pos_y;  /**< 首触点 Y 坐标 */
};
#ifdef __cplusplus
}
#endif
#endif /* FT5X06_DRV_H */
