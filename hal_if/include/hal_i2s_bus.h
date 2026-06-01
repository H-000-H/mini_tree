#ifndef HAL_I2S_BUS_H
#define HAL_I2S_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_i2s_bus hal_i2s_bus_t;

/* I2S 总线配置 */
typedef struct
{
    int ws_pin;
    int bclk_pin;
    int dout_pin;
    int din_pin;            /* -1 = 仅输出 */
    int sample_rate;        /* 采样率(Hz), 如 44100 */
    int bits_per_sample;    /* 位深: 16 或 24 */
    int channel_format;     /* 声道: 0 = 单声道, 1 = 立体声 */
} hal_i2s_config_t;

struct hal_i2s_bus
{
    int (*init)(hal_i2s_bus_t* bus, const hal_i2s_config_t* cfg);
    int (*write)(hal_i2s_bus_t* bus, const int16_t* samples, size_t bytes,
                 size_t* written, uint32_t timeout_ms);
    int (*read)(hal_i2s_bus_t* bus, int16_t* samples, size_t bytes,
                size_t* bytes_read, uint32_t timeout_ms);
    int (*deinit)(hal_i2s_bus_t* bus);
    void* _impl;
};

void hal_i2s_bus_init_struct(hal_i2s_bus_t* bus);

/* 安全停机: 复位所有 I2S 外设 (含 DMA 引擎) */
void hal_i2s_force_stop(void);

/* ioctl 兼容层 */
#define I2S_CMD_WRITE       0x50
#define I2S_CMD_DEINIT      0x51

typedef struct
{
    const int16_t* samples;
    size_t bytes;
    size_t* written;
    uint32_t timeout_ms;
} i2s_write_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2S_BUS_H */
