/* SPDX-License-Identifier: Apache-2.0 */
/*
 * RTC VFS 板级配置宏 (vfs/rtc) — 中间件默认值 + 板级覆盖入口
 * 覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */
#ifndef BOARD_DEFINE_RTC_H
#define BOARD_DEFINE_RTC_H

/* 池大小 = DTS "rtc" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_RTC
#define DTC_GEN_COUNT_RTC 1
#endif
#ifndef RTC_VFS_POOL
#define RTC_VFS_POOL DTC_GEN_COUNT_RTC
#endif

#endif /* BOARD_DEFINE_RTC_H */
