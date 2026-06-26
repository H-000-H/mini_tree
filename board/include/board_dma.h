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
