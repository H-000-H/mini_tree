#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_CMD_READ_RAW 0x70
#define ADC_CMD_STOP     0x71

typedef struct
{
    int channel;
    int atten;
    int bitwidth;
    int* out_raw;
} adc_read_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */
