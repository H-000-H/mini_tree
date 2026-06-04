#include "system_init.h"
#include "system_cfg.h"

#include "event_bus.hpp"
#include "device.h"
#include "driver.h"
#include "safe_state.h"
#include "system_wdt.h"
#include "system_scrubber.h"
#include "hal_cpu.h"

/* ── 启动期全局中断控制 ── */
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

static const char* kTag = "SysInit";

/* SIOF 防御标志: OS + EventBus 就绪前为 false, 禁止全局构造函数偷跑 */
bool g_system_os_initialized = false;

void mini_tree_pre_os_init(void)
{
    IRQ_DISABLE();  /* 关全局中断 — ISR 不得在框架就绪前触发 */
    SYS_LOGI(kTag, "=== MiniTree Phase 1: Pre-OS Init ===");

    if (!safe_state_check_bootloop())
    {
        SYS_LOGE(kTag, "bootloop protection triggered — system halted");
        return;
    }

#ifdef CONFIG_ENABLE_WDT
    system_wdt_init_rtc(8000);
#endif

    if (device_tree_init() != 0)
    {
        SYS_LOGW(kTag, "device_tree_init returned non-zero (non-fatal)");
    }

    if (!event_bus_init())
    {
        SYS_LOGE(kTag, "EventBus init failed — entering safe state");
        enter_safe_state("EventBus init failed");
        return;
    }
    event_bus_post(EVENT_SYS_BOOT, 0);

    /* SIOF 防御就绪: 此后 EventBus post/subscribe 可正常通行 */
    g_system_os_initialized = true;

    SYS_LOGI(kTag, "=== MiniTree Phase 1 complete ===");
}

void mini_tree_start_tasks(void)
{
    SYS_LOGI(kTag, "=== MiniTree Phase 2: Start Tasks ===");

    event_bus_start();

    int probe_fail = board_driver_probe_all();
    if (probe_fail != 0)
    {
        SYS_LOGW(kTag, "board_driver_probe_all: %d device(s) failed", probe_fail);
    }

#ifdef CONFIG_ENABLE_WDT
    system_wdt_init(3000);
#endif

    system_scrubber_init();
    system_scrubber_start();

    safe_state_clear_bootloop();

    event_bus_post(EVENT_SYS_READY, 0);

    /* 封表: 此后 subscribe() 全部失败, ISR 中 post() 遍历只读静态表 */
    event_bus_seal();

#if CONFIG_CPU_CORES > 1
    /* AMP: 启动副核心 (Core 1 跑 hal_cpu_baremetal_entry) */
    hal_cpu_secondary_startup();
#endif

    SYS_LOGI(kTag, "=== MiniTree Phase 2 complete ===");
}

void mini_tree_system_loop(void)
{
    /* 裸机: 喂狗 (EventBus 需在 RTOS 模式下使用) */
#ifdef CONFIG_ENABLE_WDT
    system_wdt_feed();
    system_wdt_feed_rtc();
#endif
}

/* ── 初始化完成 — 释放全局中断 ──
 * 在 vTaskStartScheduler() 之前调用. 也可由裸机在首次进入 super-loop 前调用.
 * FreeRTOS 的 vTaskStartScheduler 在首次上下文切换时也会自动使能中断.
 */
void system_init_complete(void)
{
    IRQ_ENABLE();
}
