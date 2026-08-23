/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_wdt — 看门狗与栈监控
 *
 * IWDG: 独立看门狗 (LSI), 主循环喂狗; OTA 前可延长超时
 * TWDT: 任务级软看门狗占位 (本平台无独立任务 WDT 硬件)
 * 栈水位: 注册阈值后周期巡检
 */
#pragma once

#include "osal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int system_wdt_init(uint32_t timeout_ms);
    int system_wdt_subscribe(osal_task_handle_t task);
    int system_wdt_unsubscribe(osal_task_handle_t task);
    void system_wdt_feed(void);

    int system_wdt_init_iwdg(uint32_t timeout_ms);
    void system_wdt_feed_iwdg(void);
    void system_wdt_iwdg_set_long_timeout(void);
    void system_wdt_iwdg_restore_timeout(void);

    int system_wdt_stack_monitor_register(osal_task_handle_t task, uint32_t alarm_threshold_bytes);
    void system_wdt_stack_check_all(void);

#ifdef __cplusplus
}
#endif
