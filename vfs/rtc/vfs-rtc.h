/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * RTC VFS — 实时时钟 VFS 层
 *
 * 架构位置: [VFS Layer (本文件)] → HAL Layer (无 bus, 对齐 TIM)
 * 职责: file_operations + dev_lifecycle + DTS; ioctl 设置/读取时间、闹钟、唤醒定时器。
 * 隔离: 定义 RTC_VFS_IMPL 可调 hal_rtc_*; 其他文件包含本头时 hal_rtc_* 被 #pragma GCC poison。
 *
 * Driver 注册: vfs_rtc / "rtc"
 * 约束: 闹钟寄存器路径可用; alarm callback 的 NVIC/ISR 派发尚未完整。
 *
 * @see hal/rtc/hal_rtc.h
 *@=========================================================================================================================*/
#ifndef VFS_RTC_H
#define VFS_RTC_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"
#include "hal_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTC_CMD_BASE              COMPAT_MAGIC(RTC)
#define RTC_CMD_SET_TIME          (RTC_CMD_BASE + 0x01)  /**< arg: rtc_time_arg */
#define RTC_CMD_GET_TIME          (RTC_CMD_BASE + 0x02)  /**< arg: rtc_time_arg */
#define RTC_CMD_SET_ALARM         (RTC_CMD_BASE + 0x03)  /**< arg: rtc_time_arg (callback 派发待补) */
#define RTC_CMD_CANCEL_ALARM      (RTC_CMD_BASE + 0x04)
#define RTC_CMD_SET_WAKEUP        (RTC_CMD_BASE + 0x05)  /**< arg: rtc_wakeup_arg */
#define RTC_CMD_CANCEL_WAKEUP     (RTC_CMD_BASE + 0x06)
#define RTC_CMD_FORCE_STOP        (RTC_CMD_BASE + 0x07)
#define RTC_CMD_COUNT             7

struct rtc_time_arg
{
    struct hal_rtc_time time;  /**< 时间值 (年月日时分秒) */
};

struct rtc_wakeup_arg
{
    uint32_t seconds;          /**< 唤醒倒计时秒数 */
};

#ifdef __cplusplus
}
#endif

#ifndef RTC_VFS_IMPL
#pragma GCC poison hal_rtc_init hal_rtc_deinit hal_rtc_open hal_rtc_close
#pragma GCC poison hal_rtc_set_time hal_rtc_get_time
#pragma GCC poison hal_rtc_set_alarm hal_rtc_cancel_alarm
#pragma GCC poison hal_rtc_set_wakeup_timer hal_rtc_cancel_wakeup_timer
#endif

#endif /* VFS_RTC_H */
