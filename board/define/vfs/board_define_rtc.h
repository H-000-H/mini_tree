/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_rtc.h
 *@brief board define rtc 头文件
 *@author H-000-H
 *@details
 *   RTC VFS 板级配置宏 (vfs/rtc) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_RTC_H
#define BOARD_DEFINE_RTC_H

/* 池大小 = DTS "mt-rtc" 节点数 (缺省 1) */
/* include the dtc-lite generated truth table first: otherwise the default
   value below conflicts with the real one (macro redefinition) */
#if defined(__has_include)
#if __has_include("dt_config_gen.h")
#include "dt_config_gen.h"
#endif
#endif
#ifndef DTC_GEN_COUNT_MT_RTC
#define DTC_GEN_COUNT_MT_RTC 1
#endif
#ifndef RTC_VFS_POOL
#define RTC_VFS_POOL DTC_GEN_COUNT_MT_RTC
#endif

#endif /* BOARD_DEFINE_RTC_H */
