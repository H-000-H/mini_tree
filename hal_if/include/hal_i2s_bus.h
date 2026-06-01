#ifndef HAL_I2S_BUS_H
#define HAL_I2S_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_i2s_bus hal_i2s_bus_t;

typedef struct
{
    int ws_pin;
    int bclk_pin;
    int dout_pin;
    int din_pin;
    int sample_rate;
    int bits_per_sample; /* 16 or 24 */
    int channel_format;  /* 0 = mono, 1 = stereo */
} hal_i2s_config_t;

struct hal_i2s_bus
{
    int (*init)(hal_i2s_bus_t* bus, const hal_i2s_config_t* cfg);
    int (*write)(hal_i2s_bus_t* bus, const int16_t* samples, size_t bytes,
                 size_t* written, uint32_t timeout_ms);
    int (*deinit)(hal_i2s_bus_t* bus);
    void* _impl;
};

void hal_i2s_bus_init_struct(hal_i2s_bus_t* bus);

/*
 * 安全状态强制停机: 直接复位所有 I2S 外设 (含 DMA 引擎),
 * 切断任何进行中的 PCM 传输, 防止 Safe State 中扬声器爆音。
 * 调用时机: 进入 enter_safe_state() 之前, 此时 RTOS 尚未冻结。
 */
void hal_i2s_force_stop(void);

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
