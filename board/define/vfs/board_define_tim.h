/* SPDX-License-Identifier: Apache-2.0 */
/*
 * TIM VFS 板级配置宏 (vfs/tim) — 中间件默认值 + 板级覆盖入口
 * 覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */
#ifndef BOARD_DEFINE_TIM_H
#define BOARD_DEFINE_TIM_H

/* 池大小 = DTS "tim" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_TIM
#define DTC_GEN_COUNT_TIM 1
#endif
#ifndef TIM_VFS_PRIV_COUNT
#define TIM_VFS_PRIV_COUNT DTC_GEN_COUNT_TIM
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
