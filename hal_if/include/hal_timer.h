#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_timer hal_timer_t;

/* 定时器工作模式 */
typedef enum
{
    HAL_TIMER_MODE_ONESHOT,     /* 单次模式 */
    HAL_TIMER_MODE_PERIODIC,    /* 周期模式 */
} hal_timer_mode_t;

/* 定时器通道模式 */
typedef enum
{
    HAL_TIMER_CH_OUTPUT_COMPARE,    /* 输出比较 */
    HAL_TIMER_CH_PWM,               /* PWM 输出 */
    HAL_TIMER_CH_INPUT_CAPTURE,     /* 输入捕获 */
    HAL_TIMER_CH_ENCODER,           /* 编码器模式 */
} hal_timer_ch_mode_t;

/* 定时器配置 */
typedef struct
{
    int             timer_id;       /* 定时器编号, 如 0 = TIM1 */
    uint32_t        freq_hz;        /* 定时器基础时钟(Hz), 用于计算分频 */
    hal_timer_mode_t mode;          /* 单次/周期模式 */
    uint32_t        period_us;      /* 周期/超时(微秒) */
    uint32_t        dead_time_ns;   /* 死区时间(纳秒), 0 = 不使能 */
} hal_timer_config_t;

/* 通道配置 */
typedef struct
{
    int channel;                    /* 通道号, 如 0 = CH1 */
    hal_timer_ch_mode_t mode;       /* 通道工作模式 */
    uint32_t pulse;                 /* 脉冲宽度/比较值(与 period_us 同单位) */
    int polarity;                   /* 输出极性: 0 = 高有效, 1 = 低有效 */
} hal_timer_channel_config_t;

/* 定时器中断/事件回调 */
typedef void (*hal_timer_callback_t)(hal_timer_t* timer, void* user_data);

struct hal_timer
{
    int (*init)(hal_timer_t* timer, const hal_timer_config_t* cfg);
    int (*config_channel)(hal_timer_t* timer, const hal_timer_channel_config_t* ch_cfg);
    int (*start)(hal_timer_t* timer);
    int (*stop)(hal_timer_t* timer);
    int (*reset)(hal_timer_t* timer);
    int (*set_callback)(hal_timer_t* timer, hal_timer_callback_t cb, void* user_data);
    int (*get_counter)(hal_timer_t* timer, uint32_t* val);  /* 读取当前计数值 */
    int (*deinit)(hal_timer_t* timer);
    void* _impl;
};

void hal_timer_init_struct(hal_timer_t* timer);
void hal_timer_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIMER_H */
