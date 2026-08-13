/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GPIO VFS 板级配置宏 (vfs/gpio) — 中间件默认值 + 板级覆盖入口
 * 覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */
#ifndef BOARD_DEFINE_GPIO_H
#define BOARD_DEFINE_GPIO_H

/* 池大小 = DTS "heterogeneous,gpios" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_GPIOS
#define DTC_GEN_COUNT_HETEROGENEOUS_GPIOS 1
#endif
#ifndef VFS_GPIO_PIN_COUNT
#define VFS_GPIO_PIN_COUNT DTC_GEN_COUNT_HETEROGENEOUS_GPIOS
#endif

#endif /* BOARD_DEFINE_GPIO_H */
