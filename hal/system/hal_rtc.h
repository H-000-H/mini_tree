/* SPDX-License-Identifier: Apache-2.0 */
/*
 * RTC HAL — 实时时钟抽象接口
 *
 * 提供时间读写、闹钟回调与秒级唤醒定时器
 * 支持 12/24 时制配置及 force_stop 安全停机
 */
#ifndef HAL_RTC_H
#define HAL_RTC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*RTC 时间结构*/
/*===========================================================================================================================================================*/
struct hal_rtc_time
{
    uint16_t year;      /* 年, 如 2026 */
    uint8_t  month;     /* 月, 1-12 */
    uint8_t  day;       /* 日, 1-31 */
    uint8_t  hour;      /* 时, 0-23 */
    uint8_t  minute;    /* 分, 0-59 */
    uint8_t  second;    /* 秒, 0-59 */
    uint8_t  weekday;   /* 星期, 1 = 周一 */
};
/*===========================================================================================================================================================*/

                                                            /*RTC 配置*/
/*===========================================================================================================================================================*/
struct hal_rtc_config
{
    int     rtc_id;         /* RTC 编号 */
    int     format_24h;     /* 时制: 0 = 12小时, 1 = 24小时 */
};
/*===========================================================================================================================================================*/

                                                            /*RTC 实体与 API*/
/*===========================================================================================================================================================*/
struct hal_rtc;

typedef void (*hal_rtc_alarm_callback_t)(struct hal_rtc* rtc, void* user_data);

struct hal_rtc
{
    int (*init)(struct hal_rtc* rtc, const struct hal_rtc_config* cfg);
    int (*set_time)(struct hal_rtc* rtc, const struct hal_rtc_time* time);
    int (*get_time)(struct hal_rtc* rtc, struct hal_rtc_time* time);
    int (*set_alarm)(struct hal_rtc* rtc, const struct hal_rtc_time* alarm,
                     hal_rtc_alarm_callback_t cb, void* user_data);
    int (*cancel_alarm)(struct hal_rtc* rtc);
    int (*set_wakeup_timer)(struct hal_rtc* rtc, uint32_t seconds);  /* 唤醒定时器(秒) */
    int (*deinit)(struct hal_rtc* rtc);
    void* _impl;
};

void hal_rtc_init_struct(struct hal_rtc* rtc);
/*===========================================================================================================================================================*/

                                                            /*安全停机*/
/*===========================================================================================================================================================*/
void hal_rtc_force_stop(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_RTC_H */
