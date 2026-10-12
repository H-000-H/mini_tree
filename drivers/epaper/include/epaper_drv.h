/**
 * @file epaper_drv.h
 * @author H-000-H
 * @brief 电子纸驱动对外接口说明（实现见 src/epaper_drv.c）
 * @note 命令与参数结构统一由 display_drv.h 提供：
 * @note - 整帧/全屏刷新 → DISPLAY_CMD_FLUSH / DISPLAY_CMD_DRAW_AREA（MONO_1BPP）
 * @note - 分辨率 → DISPLAY_CMD_GET_INFO（format = DISPLAY_FMT_MONO_1BPP）
 * @copyright SPDX-License-Identifier: Apache-2.0
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
