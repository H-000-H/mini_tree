/* SPDX-License-Identifier: Apache-2.0 */
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

typedef enum {
    BUS_DMA_DIR_MEM_TO_PERIPH = 0,
    BUS_DMA_DIR_PERIPH_TO_MEM,
} bus_dma_dir_t;

typedef enum {
    BUS_DMA_WIDTH_BYTE = 0,
    BUS_DMA_WIDTH_HALFWORD,
    BUS_DMA_WIDTH_WORD,
} bus_dma_width_t;

typedef enum {
    BUS_DMA_INC_FIXED = 0,
    BUS_DMA_INC_INCREMENT,
} bus_dma_inc_t;

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

/**
 * @brief 申请 DMA 通道 (通过 DTS phandle, 如 "dma-tx")
 * @param dev 使用 DMA 的 device
 * @param name DTS phandle 名称
 * @param out 输出 DMA 通道指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  bus_dma_request_chan(struct device* dev, const char* name,
                          struct bus_dma_chan** out) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 释放 DMA 通道 (归还到静态池)
 * @param chan DMA 通道指针
 */
void bus_dma_release_chan(struct bus_dma_chan* chan);

/**
 * @brief 提交并启动 DMA 传输
 * @param chan DMA 通道指针
 * @param xfer 传输描述符
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int bus_dma_submit(struct bus_dma_chan* chan,
                     const bus_dma_xfer_t* xfer) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 等待 DMA 传输完成 (阻塞)
 * @param chan DMA 通道指针
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT
 */
int bus_dma_wait(struct bus_dma_chan* chan, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 中止 DMA 传输
 * @param chan DMA 通道指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int bus_dma_abort(struct bus_dma_chan* chan) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 设置 DMA 传输完成回调
 * @param chan DMA 通道指针
 * @param cb 回调函数
 * @param user_data 回调用户数据
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int bus_dma_set_callback(struct bus_dma_chan* chan,
                          bus_dma_callback_t cb, void* user_data);

/**
 * @brief 查询 DMA 通道是否忙碌
 * @param chan DMA 通道指针
 * @return 忙碌返回 1, 空闲返回 0
 */
int bus_dma_busy(struct bus_dma_chan* chan);

/**
 * @brief 强制停止所有 DMA 传输 (紧急停止用)
 */
void bus_dma_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BUS_DMA_H */
