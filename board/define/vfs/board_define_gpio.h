/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_gpio.h
 *@brief board define gpio 头文件
 *@author H-000-H
 *@details
 *   GPIO VFS 板级配置宏 (vfs/gpio) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_GPIO_H
#define BOARD_DEFINE_GPIO_H

/* 池大小 = DTS "mt-gpios" 节点数 (缺省 1) */
/* include the dtc-lite generated truth table first: otherwise the default
   value below conflicts with the real one (macro redefinition) */
#if defined(__has_include)
#if __has_include("dt_config_gen.h")
#include "dt_config_gen.h"
#endif
#endif
#ifndef DTC_GEN_COUNT_MT_GPIOS
#define DTC_GEN_COUNT_MT_GPIOS 1
#endif
#ifndef VFS_GPIO_PIN_COUNT
#define VFS_GPIO_PIN_COUNT DTC_GEN_COUNT_MT_GPIOS
#endif

#endif /* BOARD_DEFINE_GPIO_H */
