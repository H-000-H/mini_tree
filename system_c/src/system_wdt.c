/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_wdt.c
 *@brief system wdt 实现
 *@author H-000-H
 *@details
 *   system_wdt (C) — IWDG 喂狗与栈水位监控
 */

#include "system_wdt.h"

#include "board_config.h"
#include "hal_iwdg.h"
#include "system_cfg.h"

#include "compiler_compat_poison.h"

static const char* k_tag = "SysWDT";
static bool s_initialized = false;

static struct hal_iwdg_dev s_iwdg;
static bool s_iwdg_active = false;

/**
 * @brief 启动 IWDG
 * @param[in] timeout_ms 超时
 * @return MINI_OK 成功; MINI_ERR_IO 硬件初始化/启动失败
 */
int system_wdt_init_iwdg(uint32_t timeout_ms)
{
    struct hal_iwdg_config cfg;

    if (s_iwdg_active)
        return MINI_OK;

    cfg.timeout_ms = timeout_ms;
    cfg.prer = 0xFFFFFFFFU;
    cfg.rlr = 0xFFFFFFFFU;
    if (hal_iwdg_init(&s_iwdg, &cfg) != 0)
        return MINI_ERR_IO;
    if (hal_iwdg_start(&s_iwdg) != 0)
        return MINI_ERR_IO;

    s_iwdg_active = true;
    SYS_LOGI(k_tag, "IWDG started, timeout=%ums", (unsigned)timeout_ms);
    return MINI_OK;
}

/**
 * @brief 延长 IWDG
 */
void system_wdt_iwdg_set_long_timeout(void)
{
    if (!s_iwdg_active)
        return;
    COMPAT_IGNORE_RESULT(hal_iwdg_set_long_timeout(&s_iwdg));
    SYS_LOGI(k_tag, "IWDG extended to hardware max (~32768ms) for OTA");
}

/**
 * @brief 恢复 IWDG 超时
 */
void system_wdt_iwdg_restore_timeout(void)
{
    if (!s_iwdg_active)
        return;
    COMPAT_IGNORE_RESULT(hal_iwdg_restore_timeout(&s_iwdg));
    SYS_LOGI(k_tag, "IWDG restored to %ums", (unsigned)s_iwdg.normal_timeout_ms);
}

/**
 * @brief 喂 IWDG
 */
void system_wdt_feed_iwdg(void)
{
    if (s_iwdg_active)
        COMPAT_IGNORE_RESULT(hal_iwdg_feed(&s_iwdg));
}

struct stack_monitor_entry
{
    osal_task_handle_t task; /**< 被监控任务句柄 */
    uint32_t alarm_threshold_bytes; /**< 栈剩余报警阈值 (字节) */
};

static struct stack_monitor_entry s_stack_entries[BOARD_STACK_MONITOR_MAX_TASKS];
static size_t s_stack_entry_count = 0;

/**
 * @brief 注册栈监控
 * @param[in] task 任务
 * @param[in] alarm_threshold_bytes 阈值
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_NOSPC 表满
 */
int system_wdt_stack_monitor_register(osal_task_handle_t task, uint32_t alarm_threshold_bytes)
{
    if (task == NULL || alarm_threshold_bytes == 0)
        return MINI_ERR_INVAL;
    if (s_stack_entry_count >= BOARD_STACK_MONITOR_MAX_TASKS)
    {
        SYS_LOGE(k_tag, "stack monitor: max entries (%d) reached", BOARD_STACK_MONITOR_MAX_TASKS);
        return MINI_ERR_NOSPC;
    }

    s_stack_entries[s_stack_entry_count].task = task;
    s_stack_entries[s_stack_entry_count].alarm_threshold_bytes = alarm_threshold_bytes;
    s_stack_entry_count++;
    return MINI_OK;
}

/**
 * @brief 检查全部栈
 */
void system_wdt_stack_check_all(void)
{
    for (size_t i = 0; i < s_stack_entry_count; i++)
    {
        const struct stack_monitor_entry* entry = &s_stack_entries[i];
        if (entry->task == NULL)
            continue;

        uint32_t wm_bytes = osal_task_get_stack_watermark(entry->task);

        if (wm_bytes == 0)
        {
            SYS_LOGE(k_tag, "FAIL: task '%s' stack overflowed (wm=0)!",
                     osal_task_get_name(entry->task));
            continue;
        }

        if (wm_bytes < entry->alarm_threshold_bytes)
            SYS_LOGE(k_tag, "STACK CRITICAL: '%s' watermark %u bytes < alarm %u",
                     osal_task_get_name(entry->task), (unsigned)wm_bytes,
                     (unsigned)entry->alarm_threshold_bytes);
        else if (wm_bytes < entry->alarm_threshold_bytes * 2)
            SYS_LOGW(k_tag, "STACK WARN: '%s' watermark %u bytes (alarm=%u)",
                     osal_task_get_name(entry->task), (unsigned)wm_bytes,
                     (unsigned)entry->alarm_threshold_bytes);
    }
}

/**
 * @brief TWDT 占位
 * @param[in] timeout_ms 忽略
 * @return MINI_OK 成功
 */
int system_wdt_init(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_initialized)
        return MINI_OK;
    s_initialized = true;
    SYS_LOGI(k_tag, "TWDT placeholder started");
    return MINI_OK;
}

/**
 * @brief TWDT 订阅
 * @param[in] task 任务
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_AGAIN 未初始化
 */
int system_wdt_subscribe(osal_task_handle_t task)
{
    if (task == NULL)
        return MINI_ERR_INVAL;
    if (!s_initialized)
        return MINI_ERR_AGAIN;
    return MINI_OK;
}

/**
 * @brief TWDT 取消
 * @param[in] task 任务
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_AGAIN 未初始化
 */
int system_wdt_unsubscribe(osal_task_handle_t task)
{
    if (task == NULL)
        return MINI_ERR_INVAL;
    if (!s_initialized)
        return MINI_ERR_AGAIN;
    return MINI_OK;
}

/**
 * @brief TWDT 喂狗
 */
void system_wdt_feed(void) {}
