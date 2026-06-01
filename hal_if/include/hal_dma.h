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

#ifdef __cplusplus
}
#endif

#endif /* HAL_DMA_H */
