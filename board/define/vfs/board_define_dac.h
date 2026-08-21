/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_dac.h
 *@brief board define dac 头文件
 *@author H-000-H
 *@details
 *   DAC VFS 板级配置宏 (vfs/dac) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 *   字段宽度宏须与板级 DTS 属性数组实际元素数一致。
 */

#ifndef BOARD_DEFINE_DAC_H
#define BOARD_DEFINE_DAC_H

/* 池大小 = DTS "dac" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_DAC
#define DTC_GEN_COUNT_DAC 1
#endif
#ifndef DAC_VFS_DEVICE_COUNT
#define DAC_VFS_DEVICE_COUNT DTC_GEN_COUNT_DAC
#endif

/* DMA 缓冲深度 (元素数) */
#ifndef DAC_DMA_BUFFER_SIZE
#define DAC_DMA_BUFFER_SIZE 256
#endif

/* gpio-pin 数组元素数 */
#ifndef VFS_DAC_PIN_FIELD_COUNT
#define VFS_DAC_PIN_FIELD_COUNT 8
#endif

/* dma-cfg 数组元素数 */
#ifndef VFS_DAC_DMA_FIELD_COUNT
#define VFS_DAC_DMA_FIELD_COUNT 7
#endif

#endif /* BOARD_DEFINE_DAC_H */
