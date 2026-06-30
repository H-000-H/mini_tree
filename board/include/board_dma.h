/* SPDX-License-Identifier: Apache-2.0 */
/*
 * board_dma.h — 板级 DMA 通道注册头文件
 *
 * 声明 board_dma_id_from_phandle: 从设备树 phandle 属性解析 DMA 通道 reg ID.
 * 声明 board_dma_register_channels: 扫描 dma-channel 节点并注册到 HAL DMA 驱动,
 *   由 device_tree_init 调用; 无 DMA 平台为 stub 实现.
 */
#ifndef BOARD_DMA_H
#define BOARD_DMA_H

#include "compiler_compat.h"

struct device;

/* 从设备树 phandle 属性解析 DMA 通道 reg ID */
int board_dma_id_from_phandle(const struct device* dev, const char* prop, int* out_id)
    COMPAT_WARN_UNUSED_RESULT;

/* 扫描 stm32,dma-channel 节点并注册到 hal_dma_stm32 */
int board_dma_register_channels(void) COMPAT_WARN_UNUSED_RESULT;

#endif /* BOARD_DMA_H */
