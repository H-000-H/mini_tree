#ifndef VFS_ADC_H
#define VFS_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 与 hal_if/include/hal_adc.h 类型相同，二者选一。 */
#ifndef HAL_ADC_H

#define ADC_CMD_READ_RAW 0x70
#define ADC_CMD_STOP     0x71

typedef struct
{
    int channel;
    int atten;
    int bitwidth;
    int* out_raw;
} adc_read_arg_t;

#endif /* ndef HAL_ADC_H */

#ifdef __cplusplus
}
#endif

#endif /* VFS_ADC_H */
