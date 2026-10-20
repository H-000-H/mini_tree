/**
 * @file system_wdt.c
 * @author H-000-H
 * @brief system wdt 实现
 * @note system_wdt (C) — IWDG 喂狗与栈水位监控
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "system_wdt.h"

#include "board_config.h"
#include "hal_iwdg.h"
#include "system_cfg.h"

#include "compiler_compat_poison.h"

static const char *tag	       = "SysWDT";
static bool	   initialized = false;

static struct hal_iwdg_dev iwdg;
static bool		   iwdg_active = false;

/**
 * @brief 启动 IWDG
 * @param[in] timeout_ms 超时
 * @return MINI_OK 成功; MINI_ERR_IO 硬件初始化/启动失败
 */
mt_err_t system_wdt_init_iwdg(uint32_t timeout_ms)
{
	struct hal_iwdg_config cfg;

	if (iwdg_active)
		return MINI_OK;

	cfg.timeout_ms = timeout_ms;
	cfg.prer       = 0xFFFFFFFFU;
	cfg.rlr	       = 0xFFFFFFFFU;
	if (hal_iwdg_init(&iwdg, &cfg) != 0)
		return MINI_ERR_IO;
	if (hal_iwdg_start(&iwdg) != 0)
		return MINI_ERR_IO;

	iwdg_active = true;
	MT_LOG_INFO(tag, "IWDG started, timeout=%ums", (unsigned)timeout_ms);
	return MINI_OK;
}

/**
 * @brief 延长 IWDG
 */
void system_wdt_iwdg_set_long_timeout(void)
{
	if (!iwdg_active)
		return;
	MINI_IGNORE_RESULT(hal_iwdg_set_long_timeout(&iwdg));
	MT_LOG_INFO(tag, "IWDG extended to hardware max (~32768ms) for OTA");
}

/**
 * @brief 恢复 IWDG 超时
 */
void system_wdt_iwdg_restore_timeout(void)
{
	if (!iwdg_active)
		return;
	MINI_IGNORE_RESULT(hal_iwdg_restore_timeout(&iwdg));
	MT_LOG_INFO(tag, "IWDG restored to %ums", (unsigned)iwdg.normal_timeout_ms);
}

/**
 * @brief 喂 IWDG
 */
void system_wdt_feed_iwdg(void)
{
	if (iwdg_active)
		MINI_IGNORE_RESULT(hal_iwdg_feed(&iwdg));
}

struct stack_monitor_entry {
	mini_task_handle_t task;		  /**< 被监控任务句柄 */
	uint32_t	   alarm_threshold_bytes; /**< 栈剩余报警阈值 (字节) */
};

static struct stack_monitor_entry stack_entries[BOARD_STACK_MONITOR_MAX_TASKS];
static size_t			  stack_entry_count = 0;

/**
 * @brief 注册栈监控
 * @param[in] task 任务
 * @param[in] alarm_threshold_bytes 阈值
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_NOSPC 表满
 */
mt_err_t system_wdt_stack_monitor_register(mini_task_handle_t task, uint32_t alarm_threshold_bytes)
{
	if (task == NULL || alarm_threshold_bytes == 0)
		return MINI_ERR_INVAL;
	if (stack_entry_count >= BOARD_STACK_MONITOR_MAX_TASKS) {
		MT_LOG_ERROR(tag, "stack monitor: max entries (%d) reached",
			     BOARD_STACK_MONITOR_MAX_TASKS);
		return MINI_ERR_NOSPC;
	}

	stack_entries[stack_entry_count].task		       = task;
	stack_entries[stack_entry_count].alarm_threshold_bytes = alarm_threshold_bytes;
	stack_entry_count++;
	return MINI_OK;
}

/**
 * @brief 检查全部栈
 */
void system_wdt_stack_check_all(void)
{
	for (size_t index = 0; index < stack_entry_count; index++) {
		const struct stack_monitor_entry *entry = &stack_entries[index];
		if (entry->task == NULL)
			continue;

		uint32_t wm_bytes = mini_task_get_stack_watermark(entry->task);

		if (wm_bytes == 0) {
			MT_LOG_ERROR(tag, "FAIL: task '%s' stack overflowed (wm=0)!",
				     mini_task_get_name(entry->task));
			continue;
		}

		if (wm_bytes < entry->alarm_threshold_bytes)
			MT_LOG_ERROR(tag, "STACK CRITICAL: '%s' watermark %u bytes < alarm %u",
				     mini_task_get_name(entry->task), (unsigned)wm_bytes,
				     (unsigned)entry->alarm_threshold_bytes);
		else if (wm_bytes < entry->alarm_threshold_bytes * 2)
			MT_LOG_WARN(tag, "STACK WARN: '%s' watermark %u bytes (alarm=%u)",
				    mini_task_get_name(entry->task), (unsigned)wm_bytes,
				    (unsigned)entry->alarm_threshold_bytes);
	}
}

/**
 * @brief TWDT 占位
 * @param[in] timeout_ms 忽略
 * @return MINI_OK 成功
 */
mt_err_t system_wdt_init(uint32_t timeout_ms)
{
	(void)timeout_ms;
	if (initialized)
		return MINI_OK;
	initialized = true;
	MT_LOG_INFO(tag, "TWDT placeholder started");
	return MINI_OK;
}

/**
 * @brief TWDT 订阅
 * @param[in] task 任务
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_AGAIN 未初始化
 */
mt_err_t system_wdt_subscribe(mini_task_handle_t task)
{
	if (task == NULL)
		return MINI_ERR_INVAL;
	if (!initialized)
		return MINI_ERR_AGAIN;
	return MINI_OK;
}

/**
 * @brief TWDT 取消
 * @param[in] task 任务
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_AGAIN 未初始化
 */
mt_err_t system_wdt_unsubscribe(mini_task_handle_t task)
{
	if (task == NULL)
		return MINI_ERR_INVAL;
	if (!initialized)
		return MINI_ERR_AGAIN;
	return MINI_OK;
}

/**
 * @brief TWDT 喂狗
 */
void system_wdt_feed(void)
{
}
