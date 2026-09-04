/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-wwdg.h
 *@brief vfs-wwdg 头文件
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   WWDG VFS — 窗口看门狗 VFS 层
 *   架构位置: [VFS Layer (本文件)] → HAL Layer (无 bus)
 *   职责: file_operations + dev_lifecycle + DTS (window/counter/prescaler); open 首次 start, ioctl
 *   喂狗。 隔离: 定义 WWDG_VFS_IMPL 可调 hal_wwdg_*; 其他文件包含本头时 hal_wwdg_* 被 #pragma GCC
 *   poison。
 *   Driver 注册: vfs_wwdg / "wwdg"
 *   约束: 喂狗须在硬件窗口内; 由调用方保证时机。
 *   @see hal/wwdg/hal_wwdg.h
 *   --------------------------------------------------------------------------
 */

#ifndef VFS_WWDG_H
#define VFS_WWDG_H
#include "compiler_compat.h"
#include "hal_wwdg.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define WWDG_CMD_BASE MINI_MAGIC(WWDG)
#define WWDG_CMD_FEED (WWDG_CMD_BASE + 0x01) /**< 窗口内喂狗 */
#define WWDG_CMD_COUNT 1

#ifdef __cplusplus
}
#endif
#ifndef WWDG_VFS_IMPL
#pragma GCC poison hal_wwdg_init hal_wwdg_start hal_wwdg_feed
#endif
#endif
