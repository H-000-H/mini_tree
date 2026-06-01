#ifndef HAL_DAC_H
#define HAL_DAC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_dac hal_dac_t;

/* 数据对齐模式 */
typedef enum
{
    HAL_DAC_ALIGN_8BIT,     /* 8位右对齐 */
    HAL_DAC_ALIGN_12BIT,    /* 12位右对齐 */
    HAL_DAC_ALIGN_16BIT,    /* 16位右对齐 */
} hal_dac_align_t;

/* DAC 通道配置 */
typedef struct
{
    int         channel;        /* 通道号, 0 = DAC1 */
    hal_dac_align_t align;      /* 数据对齐模式 */
    int         output_buf;     /* 输出缓冲: 0 = 不缓冲, 1 = 缓冲 */
    int         use_dma;        /* DMA 模式: 0 = 软件写入, 1 = DMA 自动输出 */
} hal_dac_config_t;

struct hal_dac
{
    int (*init)(hal_dac_t* dac, const hal_dac_config_t* cfg);
    int (*write)(hal_dac_t* dac, uint32_t val);
    int (*write_dma)(hal_dac_t* dac, const uint16_t* buf, size_t len);
    int (*stop_dma)(hal_dac_t* dac);
    int (*deinit)(hal_dac_t* dac);
    void* _impl;
};

void hal_dac_init_struct(hal_dac_t* dac);
void hal_dac_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DAC_H */
