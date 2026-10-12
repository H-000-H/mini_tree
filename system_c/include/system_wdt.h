/**
 * @file system_wdt.h
 * @author H-000-H
 * @brief system wdt 头文件
 * @note system_wdt (C 接口) — 看门狗喂狗与栈水位监控
 * @note IWDG 独立看门狗 (LSI) + TWDT 任务级软看门狗 + 栈水位巡检;
 * @note 实现见 system_c/src/system_wdt.c。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "mini_backend.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    mt_err_t system_wdt_init(uint32_t timeout_ms);
    mt_err_t system_wdt_subscribe(mini_task_handle_t task);
    mt_err_t system_wdt_unsubscribe(mini_task_handle_t task);
    void system_wdt_feed(void);

    mt_err_t system_wdt_init_iwdg(uint32_t timeout_ms);
    void system_wdt_feed_iwdg(void);
    void system_wdt_iwdg_set_long_timeout(void);
    void system_wdt_iwdg_restore_timeout(void);

    mt_err_t system_wdt_stack_monitor_register(mini_task_handle_t task, uint32_t alarm_threshold_bytes);
    void system_wdt_stack_check_all(void);

#ifdef __cplusplus
}
#endif
