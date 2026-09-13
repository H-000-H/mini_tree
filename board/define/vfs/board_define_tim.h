/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_tim.h
 *@brief board define tim 头文件
 *@author H-000-H
 *@details
 *   TIM VFS 板级配置宏 (vfs/tim) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_TIM_H
#define BOARD_DEFINE_TIM_H

/* 池大小 = DTS "mt-tim" 节点数 (缺省 1) */
/* include the dtc-lite generated truth table first: otherwise the default
   value below conflicts with the real one (macro redefinition) */
#if defined(__has_include)
#if __has_include("dt_config_gen.h")
#include "dt_config_gen.h"
#endif
#endif
#ifndef DTC_GEN_COUNT_MT_TIM
#define DTC_GEN_COUNT_MT_TIM 1
#endif
#ifndef TIM_VFS_PRIV_COUNT
#define TIM_VFS_PRIV_COUNT DTC_GEN_COUNT_MT_TIM
#endif

/* pin 属性数组元素数 (对应 hal_tim_pin_cfg 字段数) */
#ifndef VFS_TIM_PIN_FIELD_COUNT
#define VFS_TIM_PIN_FIELD_COUNT 8
#endif

/* DTS 属性键最长字符数 */
#ifndef VFS_TIM_KEY_MAX
#define VFS_TIM_KEY_MAX 40
#endif

#endif /* BOARD_DEFINE_TIM_H */
