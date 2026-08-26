/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file xpt2046_drv.h
 *@brief XPT2046 电阻触摸驱动 ioctl 命令与采样结构
 *@author H-000-H
 *@details
 *   挂在 SPI 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 *   @note LVGL indev：见 xpt2046_bridge.h → xpt2046_lvgl_read()
 */

#ifndef XPT2046_DRV_H
#define XPT2046_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define XPT2046_CMD_BASE MINI_MAGIC(XPT2046)
/** 读取坐标/按压状态（arg: struct xpt2046_xy*） */
#define XPT2046_CMD_READ_XY (XPT2046_CMD_BASE + 0x01)
/** 命令总数 */
#define XPT2046_CMD_COUNT 1

    /** @brief XPT2046 采样结果 */
    struct xpt2046_xy
    {
        uint16_t pos_x; /**< X 坐标 */
        uint16_t pos_y; /**< Y 坐标 */
        int pressed; /**< 按压状态：1=按下，0=抬起 */
    };
#ifdef __cplusplus
}
#endif
#endif /* XPT2046_DRV_H */
