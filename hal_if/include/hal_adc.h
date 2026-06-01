#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_adc hal_adc_t;

/* ADC 转换精度 */
typedef enum
{
    HAL_ADC_WIDTH_8BIT,
    HAL_ADC_WIDTH_10BIT,
    HAL_ADC_WIDTH_12BIT,
    HAL_ADC_WIDTH_14BIT,
    HAL_ADC_WIDTH_16BIT,
} hal_adc_width_t;

/* ADC 通道配置 */
typedef struct
{
    int             channel;    /* ADC 通道号 */
    hal_adc_width_t width;      /* 转换精度 */
    int             vref_mv;    /* 参考电压(mV), 0 = 使用硬件默认 */
} hal_adc_config_t;

struct hal_adc
{
    int (*init)(hal_adc_t* adc, const hal_adc_config_t* cfg);
    int (*read_raw)(hal_adc_t* adc, uint32_t* val);    /* 读取原始值 */
    int (*read_mv)(hal_adc_t* adc, uint32_t* mv);      /* 读取转换结果(mV) */
    int (*deinit)(hal_adc_t* adc);
    void* _impl;
};

void hal_adc_init_struct(hal_adc_t* adc);
void hal_adc_force_stop(void);

/* ioctl 兼容层 (已弃用) */
#define ADC_CMD_READ_RAW 0x70
#define ADC_CMD_STOP     0x71

typedef struct
{
    int channel;
    int atten;          /* (已弃用) 参考电压衰减, 新代码请使用 vref_mv */
    int bitwidth;
    int* out_raw;
} adc_read_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */
