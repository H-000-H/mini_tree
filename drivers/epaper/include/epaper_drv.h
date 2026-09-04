/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file epaper_drv.h
 *@brief 电子纸驱动对外接口说明（实现见 src/epaper_drv.c）
 *@author H-000-H
 *@details
 *   命令与参数结构统一由 display_drv.h 提供：
 *   - 整帧/全屏刷新 → DISPLAY_CMD_FLUSH / DISPLAY_CMD_DRAW_AREA（MONO_1BPP）
 *   - 分辨率 → DISPLAY_CMD_GET_INFO（format = DISPLAY_FMT_MONO_1BPP）
 *   - 无亮度控制 → DISPLAY_CMD_SET_BRIGHTNESS 返回 MINI_ERR_NOTSUPP
 *   几何参数（width/height）由 DTS 提供，busy-timeout-ms 可选。
 *   compatible: "gooddisplay,epaper"（DRIVER_REGISTER 见 src/epaper_drv.c）
 */

#ifndef EPAPER_DRV_H
#define EPAPER_DRV_H

#include "display_drv.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* probe/remove 均为驱动内部 static，无需对外声明 */

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_DRV_H */
