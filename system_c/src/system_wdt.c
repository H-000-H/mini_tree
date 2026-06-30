/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_wdt (C 实现) — 看门狗与栈水位监控
 *
 * RTC_WDT: 独立硬件看门狗, OTA 时可延长超时。
 * TWDT: 任务级看门狗, 超时触发 panic。
 * 栈监控: 注册任务水位阈值, 周期检查 overflow/critical/warn。
 */
#include "system_wdt.h"

#include "hal_wdt.h"
#include "board_config.h"
#include "system_cfg.h"
#include "compiler_compat_poison.h"

static const char* kTag = "SysWDT";
static bool s_initialized = false;

/* ═══════ RTC 硬件看门狗 ═══════ */

static bool s_rtc_wdt_active = false;
static uint32_t s_rtc_normal_timeout_ms = 8000;

bool system_wdt_init_rtc(uint32_t timeout_ms)
{
    if (s_rtc_wdt_active) return true;

    s_rtc_normal_timeout_ms = timeout_ms;
    if (!hal_wdt_init_rtc(timeout_ms)) return false;

    s_rtc_wdt_active = true;
    SYS_LOGI(kTag, "RTC_WDT started, timeout=%ums", (unsigned)timeout_ms);
    return true;
}

void system_wdt_rtc_set_long_timeout(void)
{
    if (!s_rtc_wdt_active) return;
    hal_wdt_rtc_set_long_timeout();
    SYS_LOGI(kTag, "RTC_WDT extended to 5min for OTA");
}

void system_wdt_rtc_restore_timeout(void)
{
    if (!s_rtc_wdt_active) return;
    hal_wdt_rtc_restore_timeout();
    SYS_LOGI(kTag, "RTC_WDT restored to %ums", (unsigned)s_rtc_normal_timeout_ms);
}

void system_wdt_feed_rtc(void)
{
    if (s_rtc_wdt_active)
    {
        hal_wdt_feed_rtc();
    }
}

/* ═══════ 栈水位监控 ═══════ */

struct StackMonitorEntry
{
    osal_task_handle_t task;
    uint32_t alarm_threshold_bytes;
};

static struct StackMonitorEntry s_stack_entries[BOARD_STACK_MONITOR_MAX_TASKS];
static size_t s_stack_entry_count = 0;

bool system_wdt_stack_monitor_register(osal_task_handle_t task, uint32_t alarm_threshold_bytes)
{
    if (task == NULL || alarm_threshold_bytes == 0) return false;
    if (s_stack_entry_count >= BOARD_STACK_MONITOR_MAX_TASKS)
    {
        SYS_LOGE(kTag, "stack monitor: max entries (%d) reached",
                 BOARD_STACK_MONITOR_MAX_TASKS);
        return false;
    }

    s_stack_entries[s_stack_entry_count].task = task;
    s_stack_entries[s_stack_entry_count].alarm_threshold_bytes = alarm_threshold_bytes;
    s_stack_entry_count++;
    return true;
}

void system_wdt_stack_check_all(void)
{
    for (size_t i = 0; i < s_stack_entry_count; i++)
    {
        const struct StackMonitorEntry* entry = &s_stack_entries[i];
        if (entry->task == NULL) continue;

        uint32_t wm_bytes = osal_task_get_stack_watermark(entry->task);

        if (wm_bytes == 0)
        {
            SYS_LOGE(kTag, "FAIL: task '%s' stack overflowed (wm=0)!",
                     osal_task_get_name(entry->task));
            continue;
        }

        if (wm_bytes < entry->alarm_threshold_bytes)
        {
            SYS_LOGE(kTag, "STACK CRITICAL: '%s' watermark %u bytes < alarm %u",
                     osal_task_get_name(entry->task),
                     (unsigned)wm_bytes, (unsigned)entry->alarm_threshold_bytes);
        }
        else if (wm_bytes < entry->alarm_threshold_bytes * 2)
        {
            SYS_LOGW(kTag, "STACK WARN: '%s' watermark %u bytes (alarm=%u)",
                     osal_task_get_name(entry->task),
                     (unsigned)wm_bytes, (unsigned)entry->alarm_threshold_bytes);
        }
    }
}

/* ═══════ TWDT 硬件看门狗 ═══════ */

bool system_wdt_init(uint32_t timeout_ms)
{
    if (s_initialized) return true;

    if (!hal_wdt_init_twdt(timeout_ms))
    {
        SYS_LOGE(kTag, "TWDT init failed");
        return false;
    }

    s_initialized = true;
    SYS_LOGI(kTag, "TWDT started, timeout=%ums, panic on timeout", (unsigned)timeout_ms);
    return true;
}

bool system_wdt_subscribe(osal_task_handle_t task)
{
    if (!s_initialized || task == NULL) return false;

    if (!hal_wdt_subscribe((void*)task))
    {
        SYS_LOGW(kTag, "TWDT subscribe failed for task %s", osal_task_get_name(task));
        return false;
    }
    return true;
}

bool system_wdt_unsubscribe(osal_task_handle_t task)
{
    if (!s_initialized || task == NULL) return false;

    if (!hal_wdt_unsubscribe((void*)task))
    {
        SYS_LOGW(kTag, "TWDT unsubscribe failed for task %s", osal_task_get_name(task));
        return false;
    }
    return true;
}

void system_wdt_feed(void)
{
    if (s_initialized)
    {
        hal_wdt_feed_twdt();
    }
}
