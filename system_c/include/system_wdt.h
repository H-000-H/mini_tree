#pragma once

#include "osal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool system_wdt_init(uint32_t timeout_ms);
bool system_wdt_subscribe(osal_task_handle_t task);
bool system_wdt_unsubscribe(osal_task_handle_t task);
void system_wdt_feed(void);
bool system_wdt_init_rtc(uint32_t timeout_ms);
void system_wdt_feed_rtc(void);
void system_wdt_rtc_set_long_timeout(void);
void system_wdt_rtc_restore_timeout(void);
bool system_wdt_stack_monitor_register(osal_task_handle_t task, uint32_t alarm_threshold_bytes);
void system_wdt_stack_check_all(void);

#ifdef __cplusplus
}
#endif
