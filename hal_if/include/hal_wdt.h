#ifndef HAL_WDT_H
#define HAL_WDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 硬件看门狗抽象接口 (IEC 61508 SIL 4 §7.4.3.3)
 *
 * RTC_WDT: 使用芯片内部独立时钟, 完全独立于 CPU 总线.
 *           主 CPU 卡死导致 SysTick 停摆时仍能触发硬件复位.
 *
 * TWDT:    Task Watchdog, 基于 FreeRTOS 任务级别的超时监控.
 *
 * 平台实现由宿主工程提供 (如 soc_port_mcu 或 stm32_hal_port)
 */

/* ─── RTC 硬件看门狗 ─── */
bool hal_wdt_init_rtc(uint32_t timeout_ms);
void hal_wdt_feed_rtc(void);
void hal_wdt_rtc_set_long_timeout(void);
void hal_wdt_rtc_restore_timeout(void);

/* ─── 任务看门狗 (TWDT) ─── */
bool hal_wdt_init_twdt(uint32_t timeout_ms);
bool hal_wdt_subscribe(void* task_handle);
bool hal_wdt_unsubscribe(void* task_handle);
void hal_wdt_feed_twdt(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_WDT_H */
