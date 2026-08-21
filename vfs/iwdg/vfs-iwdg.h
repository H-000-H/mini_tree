/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-iwdg.h
 *@brief vfs-iwdg 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   IWDG VFS — 独立看门狗 VFS 层
 *   架构位置: [VFS Layer (本文件)] → HAL Layer (无 bus)
 *   职责: file_operations + dev_lifecycle + DTS timeout-ms; open 首次启动 IWDG, ioctl 喂狗/改超时。
 *   隔离: 定义 IWDG_VFS_IMPL 可调 hal_iwdg_*; 其他文件包含本头时 hal_iwdg_* 被 #pragma GCC poison。
 *   Driver 注册: vfs_iwdg / "iwdg"
 *   约束: IWDG 一旦启动硬件不可真正关闭; close 仅释放 lifecycle。
 *   @see hal/iwdg/hal_iwdg.h
 *   @=========================================================================================================================
 */

#ifndef VFS_IWDG_H
#define VFS_IWDG_H
#include "compiler_compat.h"
#include "hal_iwdg.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

#define IWDG_CMD_BASE COMPAT_MAGIC(IWDG)
#define IWDG_CMD_FEED (IWDG_CMD_BASE + 0x01) /**< 喂狗 */
#define IWDG_CMD_SET_TIMEOUT (IWDG_CMD_BASE + 0x02) /**< arg: iwdg_timeout_arg */
#define IWDG_CMD_SET_LONG (IWDG_CMD_BASE + 0x03) /**< 拉长至硬件上限 (~32768ms) */
#define IWDG_CMD_RESTORE (IWDG_CMD_BASE + 0x04) /**< 恢复 probe/init 超时 */
#define IWDG_CMD_COUNT 4

    /** IWDG_CMD_SET_TIMEOUT 参数 */
    struct iwdg_timeout_arg
    {
        uint32_t timeout_ms;
    };

#ifdef __cplusplus
}
#endif
#ifndef IWDG_VFS_IMPL
#pragma GCC poison hal_iwdg_init hal_iwdg_start hal_iwdg_feed
#pragma GCC poison hal_iwdg_set_timeout_ms hal_iwdg_set_long_timeout hal_iwdg_restore_timeout
#endif
#endif
