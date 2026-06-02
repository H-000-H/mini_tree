#ifndef HAL_DMA_H
#define HAL_DMA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_dma_chan hal_dma_chan_t;

/* 传输方向 */
typedef enum
{
    HAL_DMA_DIR_MEM_TO_MEM,     /* 内存到内存 */
    HAL_DMA_DIR_MEM_TO_PERIPH,  /* 内存到外设 */
    HAL_DMA_DIR_PERIPH_TO_MEM,  /* 外设到内存 */
} hal_dma_dir_t;

/* 传输数据宽度 */
typedef enum
{
    HAL_DMA_WIDTH_BYTE = 1,     /* 1 字节 */
    HAL_DMA_WIDTH_HALF = 2,     /* 2 字节 */
    HAL_DMA_WIDTH_WORD = 4,     /* 4 字节 */
} hal_dma_width_t;

/* 地址递增模式 */
typedef enum
{
    HAL_DMA_INC_FIXED,          /* 地址固定 */
    HAL_DMA_INC_INCREMENT,      /* 地址递增 */
} hal_dma_inc_t;

/* DMA 通道配置 */
typedef struct
{
    int             dma_id;         /* DMA 控制器编号, 0 开始 */
    hal_dma_dir_t   dir;            /* 传输方向 */
    hal_dma_width_t src_width;      /* 源数据宽度 */
    hal_dma_width_t dst_width;      /* 目的数据宽度 */
    hal_dma_inc_t   src_inc;        /* 源地址模式 */
    hal_dma_inc_t   dst_inc;        /* 目的地址模式 */
    int             priority;       /* 优先级, 0 = 最低 */
    int             cir_mode;       /* 循环模式: 0 = 单次, 1 = 循环 */
    int             irq_enable;     /* 中断使能: 0 = 轮询, 1 = 完成中断 */
} hal_dma_config_t;

/* DMA 完成回调 */
typedef void (*hal_dma_callback_t)(hal_dma_chan_t* chan, void* user_data);

struct hal_dma_chan
{
    int (*init)(hal_dma_chan_t* chan, const hal_dma_config_t* cfg);
    int (*config)(hal_dma_chan_t* chan, const void* src, void* dst, size_t len);
    int (*start)(hal_dma_chan_t* chan);
    int (*stop)(hal_dma_chan_t* chan);
    int (*reset)(hal_dma_chan_t* chan);
    int (*deinit)(hal_dma_chan_t* chan);
    int (*set_callback)(hal_dma_chan_t* chan, hal_dma_callback_t cb, void* user_data);
    int (*busy)(hal_dma_chan_t* chan);      /* 查询传输是否完成: 0 = 空闲, 1 = 繁忙 */
    void* _impl;
};

void hal_dma_init_struct(hal_dma_chan_t* chan);
void hal_dma_force_stop(void);

/* ── DMA 块传输约束 ──
 *
 * DMA 个体传输的 setup 开销与传输量无关。传 1 字节和传 1KB
 * 消耗相同的中断/配置周期。byte-by-byte 传输效率极差。
 *
 * 所有 DMA 传输必须使用块模式：
 *   - 对齐: len 必须满足 width 对齐 (byte=1, half=2, word=4)
 *   - 最小块: 推荐 ≥ 32 字节，避免 setup 开销淹没带宽
 *
 * 上层应用应合并小包为块传输，而非逐字节调用 DMA。
 */
#ifdef DEBUG
#include <stdio.h>
#define HAL_DMA_ASSERT_BLOCK(len, width)                                     \
    do {                                                                     \
        if (((size_t)(len) & ((size_t)(width) - 1)) != 0) {                  \
            printf("[DMA] misaligned len=%zu (width=%d)\n",                  \
                   (size_t)(len), (int)(width));                             \
            while (1);                                                        \
        }                                                                    \
    } while (0)
#define HAL_DMA_ASSERT_BLOCK_MIN(len, min)                                   \
    do {                                                                     \
        if ((size_t)(len) < (size_t)(min)) {                                 \
            printf("[DMA] block too small: len=%zu < min=%zu\n",             \
                   (size_t)(len), (size_t)(min));                            \
            while (1);                                                        \
        }                                                                    \
    } while (0)
#else
#define HAL_DMA_ASSERT_BLOCK(len, width)      ((void)0)
#define HAL_DMA_ASSERT_BLOCK_MIN(len, min)    ((void)0)
#endif

/* 块传输配置包装 — 替代 chan->config() 的直接调用 */
static inline int hal_dma_config_block(hal_dma_chan_t* chan,
                                       const void* src, void* dst,
                                       size_t len, hal_dma_width_t width)
{
    HAL_DMA_ASSERT_BLOCK(len, width);
    HAL_DMA_ASSERT_BLOCK_MIN(len, 32);
    (void)width;
    return chan->config(chan, src, dst, len);
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_DMA_H */

