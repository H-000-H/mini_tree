/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_init.c
 *@brief system init 实现
 *@author H-000-H
 *@details
 *   system_init (C 实现) — 两阶段启动流程
 *   Phase 1 (Pre-OS): 关中断 → bootloop 检查 → IWDG → 设备树 → EventBus
 *   Phase 2 (Start-Tasks): 驱动探测 → TWDT → scrubber → seal EventBus → AMP 副核
 */

#include "system_init.h"

#include "compiler_compat.h"
#include "config.h"
#include "device.h"
#include "driver.h"
#include "event_bus.h"
#include "hal_amp.h"
#include "interrupt.h"
#include "safe_state.h"
#include "status.h"
#include "system_cfg.h"
#include "system_scrubber.h"
#include "system_wdt.h"

#include "compiler_compat_poison.h"
#ifdef CONFIG_OSAL_NULL
#include "xtask.h"
#endif

/* -------------------------------------------------------------------------- */
/* 启动期全局中断控制 */
/* -------------------------------------------------------------------------- */
#if defined(__ARM_ARCH_7EM__) || defined(__CORTEX_M) || defined(__ARM_ARCH_6M__) ||                \
    defined(__ARM_ARCH_8M_BASE__)
#define IRQ_DISABLE() __asm__ volatile("cpsid i" ::: "memory")
#define IRQ_ENABLE() __asm__ volatile("cpsie i" ::: "memory")
#elif defined(__riscv)
#define IRQ_DISABLE() __asm__ volatile("csrci mstatus, 8" ::: "memory")
#define IRQ_ENABLE() __asm__ volatile("csrsi mstatus, 8" ::: "memory")
#else
#define IRQ_DISABLE()                                                                              \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#define IRQ_ENABLE()                                                                               \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

static const char* k_tag = "SysInit";

/* SIOF 防御标志: OS + EventBus 就绪前为 false, 禁止全局构造函数偷跑 */
volatile bool g_system_os_initialized = false;

/**
 * @brief Phase1 Pre-OS 初始化
 */
void mini_tree_pre_os_init(void)
{
    IRQ_DISABLE(); /* 关全局中断 — ISR 不得在框架就绪前触发 */
    SYS_LOGI(k_tag, "=== mini_tree Phase 1: Pre-OS Init ===");

    if (!safe_state_check_bootloop())
    {
        SYS_LOGE(k_tag, "bootloop protection triggered — system halted");
        return;
    }

#ifdef CONFIG_SYSTEM_WDT
    MINI_IGNORE_RESULT(system_wdt_init_iwdg(8000));
#endif

    if (device_tree_init() != MINI_OK)
        SYS_LOGW(k_tag, "device_tree_init failed (non-fatal)");

#ifdef CONFIG_EVENT_BUS
    if (event_bus_init() != MINI_OK)
    {
        SYS_LOGE(k_tag, "EventBus init failed — entering safe state");
        enter_safe_state("EventBus init failed");
        return;
    }
    MINI_IGNORE_RESULT(event_bus_post(EVENT_SYS_BOOT, 0));
#endif

    /* SIOF 防御就绪: 此后 EventBus post/subscribe 可正常通行 */
    g_system_os_initialized = true;

    SYS_LOGI(k_tag, "=== mini_tree Phase 1 complete ===");
}

/**
 * @brief Phase2 启动任务
 */
void mini_tree_start_tasks(void)
{
    SYS_LOGI(k_tag, "=== mini_tree Phase 2: Start Tasks ===");

#ifdef CONFIG_EVENT_BUS
    event_bus_start();
#endif

    int probe_fail = board_driver_probe_all();
    if (probe_fail != 0)
        SYS_LOGW(k_tag, "board_driver_probe_all: %d device(s) failed", probe_fail);

#ifdef CONFIG_SYSTEM_WDT
    MINI_IGNORE_RESULT(system_wdt_init(3000));
#endif

#ifdef CONFIG_SYSTEM_SCRUBBER
    MINI_IGNORE_RESULT(system_scrubber_init());
    MINI_IGNORE_RESULT(system_scrubber_start());
#endif

    safe_state_clear_bootloop();

#ifdef CONFIG_EVENT_BUS
    MINI_IGNORE_RESULT(event_bus_post(EVENT_SYS_READY, 0));

    /* 封表: 此后 subscribe() 全部失败, ISR 中 post() 遍历只读静态表 */
    event_bus_seal();
#endif

#if CONFIG_CPU_CORES > 1
    /* AMP: 启动副核心 (Core 1 跑 hal_cpu_baremetal_entry) */
    hal_cpu_secondary_startup();
#endif

    SYS_LOGI(k_tag, "=== mini_tree Phase 2 complete ===");
}

/**
 * @brief 裸机主循环喂狗
 */
void mini_tree_system_loop(void)
{
#ifdef CONFIG_SYSTEM_WDT
    system_wdt_feed();
    system_wdt_feed_iwdg();
#endif
#ifdef CONFIG_OSAL_NULL
#ifdef CONFIG_VIRQ
    interrupt_bottom_half_poll();
#endif
    x_scheduler_poll();
#endif
}

/**
 * @brief 系统初始化完成, 释放全局中断
 */
void system_init_complete(void) { IRQ_ENABLE(); }
