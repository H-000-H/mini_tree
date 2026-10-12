/**
 * @file vfs-wwdg.h
 * @author H-000-H
 * @brief vfs-wwdg 头文件
 * @note WWDG VFS — 窗口看门狗 VFS 层
 * @note 架构位置: [VFS Layer (本文件)] → HAL Layer (无 bus)
 * @note 职责: file_operations + dev_lifecycle + DTS (window/counter/prescaler); open 首次 start, ioctl
 * @copyright SPDX-License-Identifier: Apache-2.0
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
