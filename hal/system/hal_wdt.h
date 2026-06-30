/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WDT HAL — 硬件/任务双看门狗抽象 (IEC 61508 SIL 4)
 *
 * RTC_WDT 使用独立时钟, CPU 卡死 SysTick 停摆仍可触发硬件复位
 * TWDT 基于任务超时, 支持 subscribe/unsubscribe 与 feed
 */
#ifndef HAL_WDT_H
#define HAL_WDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*RTC 硬件看门狗 API*/
/*===========================================================================================================================================================*/
/* 硬件看门狗抽象接口 (IEC 61508 SIL 4 §7.4.3.3)
 *
 * RTC_WDT: 使用芯片内部独立时钟, 完全独立于 CPU 总线.
 *           主 CPU 卡死导致 SysTick 停摆时仍能触发硬件复位.
 * TWDT:    Task Watchdog, 基于 OS 任务级别的超时监控.
 */
bool hal_wdt_init_rtc(uint32_t timeout_ms);
void hal_wdt_feed_rtc(void);
void hal_wdt_rtc_set_long_timeout(void);     /* 切换到长超时(进入低功耗前) */
void hal_wdt_rtc_restore_timeout(void);      /* 恢复原超时 */
/*===========================================================================================================================================================*/

                                                            /*任务看门狗 API*/
/*===========================================================================================================================================================*/
bool hal_wdt_init_twdt(uint32_t timeout_ms);
bool hal_wdt_subscribe(void* task_handle);
bool hal_wdt_unsubscribe(void* task_handle);
void hal_wdt_feed_twdt(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_WDT_H */
