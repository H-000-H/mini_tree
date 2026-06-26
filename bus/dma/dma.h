/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DMA Capability — Bus 层的传输加速器
 *
 * DMA 不是独立系统层，而是 Bus 在需要高速传输时选用的 backend。
 * 设计：
 *   - 对外：request_chan / submit / wait / abort
 *   - 对下：stm32 backend 封装具体寄存器/IRQ
 */
#ifndef BUS_DMA_H
#define BUS_DMA_H

#include <stdint.h>
#include <stddef.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct bus_dma_chan;

/* 传输方向 */
typedef enum {
    BUS_DMA_DIR_MEM_TO_PERIPH = 0,
    BUS_DMA_DIR_PERIPH_TO_MEM,
} bus_dma_dir_t;

/* 数据宽度 */
typedef enum {
    BUS_DMA_WIDTH_BYTE = 0,
    BUS_DMA_WIDTH_HALFWORD,
    BUS_DMA_WIDTH_WORD,
} bus_dma_width_t;

/* 地址自增 */
typedef enum {
    BUS_DMA_INC_FIXED = 0,
    BUS_DMA_INC_INCREMENT,
} bus_dma_inc_t;

/* 传输描述符 */
typedef struct {
    const void*      src;
    void*            dst;
    size_t           len;
    bus_dma_dir_t    dir;
    bus_dma_width_t  width;
    bus_dma_inc_t    src_inc;
    bus_dma_inc_t    dst_inc;
    uint32_t         flags;
} bus_dma_xfer_t;

typedef void (*bus_dma_callback_t)(struct bus_dma_chan* chan, void* user_data);

/* 申请/释放通道（通过 DTS phandle，如 "dma-tx"） */
int  bus_dma_request_chan(struct device* dev, const char* name,
                          struct bus_dma_chan** out) COMPAT_WARN_UNUSED_RESULT;
void bus_dma_release_chan(struct bus_dma_chan* chan);

/* 提交并启动 */
int bus_dma_submit(struct bus_dma_chan* chan,
                     const bus_dma_xfer_t* xfer) COMPAT_WARN_UNUSED_RESULT;
int bus_dma_wait(struct bus_dma_chan* chan, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
int bus_dma_abort(struct bus_dma_chan* chan) COMPAT_WARN_UNUSED_RESULT;

int bus_dma_set_callback(struct bus_dma_chan* chan,
                          bus_dma_callback_t cb, void* user_data);

int bus_dma_busy(struct bus_dma_chan* chan);

void bus_dma_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BUS_DMA_H */
