/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_init.cpp — MiniTree 两阶段系统初始化实现
 *
 * Phase 1 Pre_OS_Init: 关中断 → bootloop 保护 → RTC_WDT → 设备树 → EventBus
 * Phase 2 Start_Tasks: 驱动探测 → TWDT → scrubber → 清 bootloop → seal EventBus
 * 全局中断由 system_init_complete() 释放, g_system_os_initialized 守护 SIOF
 */
#include "system_init.hpp"
#include "system_init.h"

#include "system_cfg.h"
#include "system_wdt.hpp"
#include "system_scrubber.hpp"
#include "safe_state.h"
#include "hal_cpu.h"
#include "compiler_compat.h"
#include "compiler_compat_poison.h"

#include "event_bus.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"

/* ── 启动期全局中断控制 (平台抽象) ──
 * 在 Pre_OS_Init 入口关全局中断, 阻断 ISR 抢跑访问未就绪的框架状态.
 * 直到用户显式调用 system_init_complete() 才重新打开.
 * FreeRTOS vTaskStartScheduler() 内部也会打开中断, 所以即使忘记调
 * system_init_complete(), 调度器启动后中断也会自动使能.
 */
#if defined(__ARM_ARCH_7EM__) || defined(__CORTEX_M) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
#define IRQ_DISABLE()  __asm__ volatile("cpsid i" ::: "memory")
#define IRQ_ENABLE()   __asm__ volatile("cpsie i" ::: "memory")
#elif defined(__riscv)
#define IRQ_DISABLE()  __asm__ volatile("csrci mstatus, 8" ::: "memory")
#define IRQ_ENABLE()   __asm__ volatile("csrsi mstatus, 8" ::: "memory")
#else
#define IRQ_DISABLE()  do {} while (0)
#define IRQ_ENABLE()   do {} while (0)
#endif

static constexpr const char* kTag = "SysInit";

/* SIOF 防御标志: OS + EventBus 就绪前为 false, 禁止全局构造函数偷跑 */
bool g_system_os_initialized = false;

/* ═══════════════════════════════════════════════════════════════════════════
 *  阶段 1: 预操作系统初始化
 *
 *  在 vTaskStartScheduler() 之前调用。完成以下操作:
 *    - 启动循环保护
 *    - RTC 硬件看门狗 (独立时钟源)
 *    - 设备树数据结构初始化
 *    - 事件总线初始化 (创建 FreeRTOS 队列)
 *
 *  不创建任务、启动服务或探测驱动 —
 *  这些属于阶段 2, 在用户注册其 HAL 之后进行.
 * ═══════════════════════════════════════════════════════════════════════════ */
void MiniTree::System_Pre_OS_Init(void)
{
    IRQ_DISABLE();  /* 关全局中断 — ISR 不得在框架就绪前触发 */
    SYS_LOGI(kTag, "=== MiniTree Phase 1: Pre-OS Init ===");

    /* 启动循环保护: >= 5 次连续崩溃 → 永久安全锁死 */
    if (!safe_state_check_bootloop())
    {
        SYS_LOGE(kTag, "bootloop protection triggered — system halted");
        return;
    }

    /* RTC 硬件看门狗: 独立时钟, 在 CPU 总线停滞时仍存活 */
#ifdef CONFIG_ENABLE_WDT
    system_wdt_init_rtc(8000);
#endif

    /* 设备树初始化 (编译时生成的节点表) */
    if (device_tree_init() != VFS_OK)
    {
        SYS_LOGW(kTag, "device_tree_init failed (non-fatal)");
    }

    /* 事件总线两阶段初始化 (SIOF 防御) */
    if (!event_bus_init())
    {
        SYS_LOGE(kTag, "EventBus init failed — entering safe state");
        enter_safe_state("EventBus init failed");
        return;
    }
    COMPAT_IGNORE_RESULT(event_bus_post(EVENT_SYS_BOOT, 0));

    /* SIOF 防御就绪: 此后 EventBus post/subscribe 可正常通行 */
    g_system_os_initialized = true;

    /*
     * ─── 用户服务初始化钩子点 ───
     * 用户工程应在此处调用自身的服务 init(),
     * 在 MiniTree::System_Pre_OS_Init() 之后且
     * MiniTree::System_Start_Tasks() 之前。示例:
     *
     *   MiniTree::System_Pre_OS_Init();
     *   MyApp::AudioService::getInstance().init();
     *   MyApp::UiService::getInstance().init();
     *   MiniTree::System_Start_Tasks();
     */

    SYS_LOGI(kTag, "=== MiniTree Phase 1 complete ===");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  阶段 2: 创建框架任务
 *
 *  在用户驱动注册之后、vTaskStartScheduler() 之前调用。
 *  完成以下操作:
 *    - 驱动探测 (设备树 ←→ HAL 驱动匹配)
 *    - TWDT 初始化
 *    - Flash 位腐烂巡检启动
 *    - 启动循环计数器清除
 *
 *  用户工程在 MiniTree::System_Start_Tasks() 之后、
 *  vTaskStartScheduler() 之前创建自身的业务任务 (UI、云、音频等).
 * ═══════════════════════════════════════════════════════════════════════════ */
void MiniTree::System_Start_Tasks(void)
{
    SYS_LOGI(kTag, "=== MiniTree Phase 2: Start Tasks ===");

    event_bus_start();

    /* 驱动探测 (用户驱动在阶段 1 和阶段 2 之间注册) */
    int probe_fail = board_driver_probe_all();
    if (probe_fail != 0)
    {
        SYS_LOGW(kTag, "board_driver_probe_all: %d device(s) failed", probe_fail);
    }

    /* TWDT 初始化 */
#ifdef CONFIG_ENABLE_WDT
    system_wdt_init(3000);
#endif

    /* Flash 位腐烂巡检 */
    /* Flash 位腐烂巡检 */
#ifdef CONFIG_ENABLE_FLASH_SCRUBBER
    system_scrubber_init();
    system_scrubber_start();
#endif

    /* 启动循环计数器清除 */
    safe_state_clear_bootloop();

    COMPAT_IGNORE_RESULT(event_bus_post(EVENT_SYS_READY, 0));

    /* 封表: 此后 subscribe() 全部失败, ISR 中 post() 遍历只读静态表 */
    event_bus_seal();

#if CONFIG_CPU_CORES > 1
    hal_cpu_secondary_startup();
#endif

    /*
     * ─── 用户任务创建钩子点 ───
     * 用户工程在此处创建自身的任务:
     *
     *   MiniTree::System_Start_Tasks();
     *   xTaskCreate(my_app_task, "app", 2048, NULL, 1, NULL);
     *   vTaskStartScheduler();
     */

    SYS_LOGI(kTag, "=== MiniTree Phase 2 complete ===");
}

/* ── 初始化完成 — 释放全局中断 ──
 * 在 vTaskStartScheduler() 之前调用, 允许 ISR 开始响应.
 * FreeRTOS 的 vTaskStartScheduler 在首次上下文切换时也会自动使能中断,
 * 所以即使忘记调用此函数, 系统启动后中断仍能正常工作.
 */
extern "C" void system_init_complete(void)
{
    IRQ_ENABLE();
}

extern "C" void mini_tree_pre_os_init(void)
{
    MiniTree::System_Pre_OS_Init();
}

extern "C" void mini_tree_start_tasks(void)
{
    MiniTree::System_Start_Tasks();
}

extern "C" void mini_tree_system_loop(void)
{
#ifdef CONFIG_ENABLE_WDT
    system_wdt_feed();
    system_wdt_feed_rtc();
#endif
}
