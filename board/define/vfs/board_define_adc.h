/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_adc.h
 *@brief board define adc 头文件
 *@author H-000-H
 *@details
 *   ADC VFS 板级配置宏 (vfs/adc) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 *   字段宽度宏须与板级 DTS 属性数组实际元素数一致, 否则读取越界。
 *   Board config macros for ADC VFS — middleware defaults, board override
 *   by editing this file or -D<NAME>=<N>. Field-width macros must match
 *   the actual element counts in the board DTS property arrays.
 */

#ifndef BOARD_DEFINE_ADC_H
#define BOARD_DEFINE_ADC_H

/* ── 池大小: DTS "adc" 节点数 (缺省 1); 板级可改固定值预留更多实例 ── */
#ifndef DTC_GEN_COUNT_ADC
#define DTC_GEN_COUNT_ADC 1
#endif
#ifndef ADC_VFS_PRIV_COUNT
#define ADC_VFS_PRIV_COUNT DTC_GEN_COUNT_ADC
#endif

/* ── 字段宽度 (数组属性元素数) ── */

/* gpio-pin 数组: <port pin clk af otype speed mode pull> */
#ifndef DTS_ADC_PIN_FIELD_COUNT
#define DTS_ADC_PIN_FIELD_COUNT 8
#endif

/* channelN 数组: <id rank sample-time diff-mode attenuation> */
#ifndef DTS_ADC_CHANNEL_FIELD_COUNT
#define DTS_ADC_CHANNEL_FIELD_COUNT 5
#endif

/* multi-cfg 数组: <multimode common-clock multi-dma sampling-delay> */
#ifndef DTS_ADC_MULTI_FIELD_COUNT
#define DTS_ADC_MULTI_FIELD_COUNT 4
#endif

/* dma-cfg 数组: <handle stream chan prio memsz it-en dma-en dir mode ...> */
#ifndef DTS_ADC_DMA_FIELD_COUNT
#define DTS_ADC_DMA_FIELD_COUNT 16
#endif

/* DTS 属性键最长字符数 (如 "channel12") */
#ifndef DTS_ADC_KEY_MAX
#define DTS_ADC_KEY_MAX 40
#endif

/* ── DMA 双缓冲深度 (hal/adc) ── */
/* DMA 采样缓冲元素数; 默认 256, 音频/波形捕获可加大 */
#ifndef DMA_BUFFER_SIZE
#define DMA_BUFFER_SIZE 256
#endif

#endif /* BOARD_DEFINE_ADC_H */
