#ifndef HAL_RTC_H
#define HAL_RTC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_rtc hal_rtc_t;

/* RTC 时间结构体 */
typedef struct
{
    uint16_t year;      /* 年, 如 2026 */
    uint8_t  month;     /* 月, 1-12 */
    uint8_t  day;       /* 日, 1-31 */
    uint8_t  hour;      /* 时, 0-23 */
    uint8_t  minute;    /* 分, 0-59 */
    uint8_t  second;    /* 秒, 0-59 */
    uint8_t  weekday;   /* 星期, 1 = 周一 */
} hal_rtc_time_t;

/* RTC 配置 */
typedef struct
{
    int     rtc_id;         /* RTC 编号 */
    int     format_24h;     /* 时制: 0 = 12小时, 1 = 24小时 */
} hal_rtc_config_t;

/* 闹钟回调 */
typedef void (*hal_rtc_alarm_callback_t)(hal_rtc_t* rtc, void* user_data);

struct hal_rtc
{
    int (*init)(hal_rtc_t* rtc, const hal_rtc_config_t* cfg);
    int (*set_time)(hal_rtc_t* rtc, const hal_rtc_time_t* time);
    int (*get_time)(hal_rtc_t* rtc, hal_rtc_time_t* time);
    int (*set_alarm)(hal_rtc_t* rtc, const hal_rtc_time_t* alarm,
                     hal_rtc_alarm_callback_t cb, void* user_data);
    int (*cancel_alarm)(hal_rtc_t* rtc);
    int (*set_wakeup_timer)(hal_rtc_t* rtc, uint32_t seconds);  /* 唤醒定时器(秒) */
    int (*deinit)(hal_rtc_t* rtc);
    void* _impl;
};

void hal_rtc_init_struct(hal_rtc_t* rtc);
void hal_rtc_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RTC_H */
