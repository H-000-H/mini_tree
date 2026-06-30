/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_wdt — 工业/医疗级看门狗与栈监控接口
 *
 * TWDT 任务看门狗: 3 秒未喂狗触发 Core Dump + 硬件复位
 * RTC_WDT 独立 32kHz 时钟, IEC 61508 SIL4, CPU 总线停滞仍可冷启动
 * 栈水位监控: 注册任务阈值, system_wdt_stack_check_all 周期巡检预警
 */
#pragma once

#include "osal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 工业/医疗级 Task Watchdog (TWDT)
 * 超时 3 秒未喂狗 → Core Dump + 硬件复位
 */
bool system_wdt_init(uint32_t timeout_ms);

bool system_wdt_subscribe(osal_task_handle_t task);

bool system_wdt_unsubscribe(osal_task_handle_t task);

/*
 * 各 Task 主循环中周期性调用, 推荐间隔 < 1 秒
 */
void system_wdt_feed(void);

/* ──── RTC 硬件看门狗 (IEC 61508 SIL 4 §7.4.3.3) ──────────────
 *
 * RTC_WDT 使用芯片内部独立 32kHz 时钟, 完全独立于主 CPU 总线.
 * 主 CPU APB/AHB 卡死导致 SysTick 停摆时, SW WDT 随 CPU 同归于尽,
 * RTC_WDT 在物理底层直接切断电源触发冷启动.
 *
 * 调用时机: 必须在 system_runtime::init() 最早期调用,
 *           优先级高于所有软件初始化.
 */
bool system_wdt_init_rtc(uint32_t timeout_ms);

/*
 * RTC_WDT 喂狗, 必须在主循环中以 < timeout_ms/2 的间隔调用.
 */
void system_wdt_feed_rtc(void);

/*
 * OTA 安全: 擦写 4MB Flash 需 30~60s, 远超 RTC_WDT 正常超时.
 * 进入 OTA 前调用 set_long() 延长至 5 分钟, OTA 完成后 restore().
 */
void system_wdt_rtc_set_long_timeout(void);

void system_wdt_rtc_restore_timeout(void);

/* ──── 栈水位监控 (Stack High Water Mark Monitor) ──────────────
 *
 * 医疗/工业级栈溢出预警 (IEC 61508 §7.4.2.3):
 *   在开发阶段捕捉"差 512 字节就溢出"的边缘场景,
 *   而非等 vApplicationStackOverflowHook 事后熔断.
 *
 * 用法:
 *   system_wdt_stack_monitor_register(ui_handle, 512);
 *   system_wdt_stack_monitor_register(audio_handle, 512);
 *   // 在监控 Task 中每 5 秒调用:
 *   system_wdt_stack_check_all();
 */

bool system_wdt_stack_monitor_register(osal_task_handle_t task,
                                       uint32_t alarm_threshold_bytes);

void system_wdt_stack_check_all(void);

#ifdef __cplusplus
}
#endif