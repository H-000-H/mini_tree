/**
 * @file mini_time.c
 * @author H-000-H
 * @brief 毫秒时钟与延时实现 (按后端分发)
 * @note 时间三函数按后端分发, 语义与各内核原生延时对齐。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "mini_time.h"

#include "compiler_compat.h"
#include "hal_amp.h"

#if defined(CONFIG_OS_MINI_OS)
#include "redef.h"
#include "schedule.h"
#include "thread.h"
#elif defined(CONFIG_OS_FREERTOS)
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#endif
#elif defined(CONFIG_OS_RTTHREAD)
#include "rtthread.h"
#elif defined(CONFIG_OS_BARE)
#include "compiler_inline.h"
#include "xtask.h"
#endif

/* -------------------------------------------------------------------------- */
/* 裸机专用: tick 停滞保护                                                     */
/* -------------------------------------------------------------------------- */
#if defined(CONFIG_OS_BARE)

/**
 * @brief tick 连续不前进的次数阈值
 * @details 连续冲过阈值判定时基系统未运行或中断被意外屏蔽, 退出死等以防硬死锁
 */
#define MINI_TIME_TICK_HANG_THRESHOLD 10000U

#endif /* CONFIG_OS_BARE */

/* -------------------------------------------------------------------------- */
/* 时间 API                                                                    */
/* -------------------------------------------------------------------------- */

uint32_t mini_time_ms(void)
{
#if defined(CONFIG_OS_MINI_OS)
    mini_os_tick_t tick = 0;
    MINI_IGNORE_RESULT(mini_os_get_tick(&tick));
    return (uint32_t)MINI_OS_TICK_TO_MS((mini_os_uint32_t)tick);
#elif defined(CONFIG_OS_FREERTOS)
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
#elif defined(CONFIG_OS_RTTHREAD)
    return rt_tick_get() * 1000 / RT_TICK_PER_SECOND;
#else
    /* 裸机: 复用 xtask 调度器 tick (tick 与 ms 1:1) */
    return MINI_ATOMIC_LOAD(&g_scheduler.tick_count, MINI_RELAXED);
#endif
}

void mini_delay_ms(uint32_t ms)
{
#if defined(CONFIG_OS_MINI_OS)
    if (hal_is_in_isr())
        return; /* 中断中不能阻塞 */
    MINI_IGNORE_RESULT(mini_os_thread_delay_ms(ms));
#elif defined(CONFIG_OS_FREERTOS)
    if (hal_is_in_isr())
        return; /* 中断中不能阻塞 */
    vTaskDelay(pdMS_TO_TICKS(ms));
#elif defined(CONFIG_OS_RTTHREAD)
    rt_thread_mdelay(ms);
#else
    /* 裸机: WFI 忙等, 期间所有裸机任务停摆; 仅用于主循环 / 非协程上下文。
     * 任务回调内的"让出式延时"请用 protothread 宏 PT_DELAY(task, ms)。 */
    if (ms == 0)
        return;

    uint32_t start = mini_time_ms();
    uint32_t last = start;
    uint32_t no_tick_count = 0U;

    while ((mini_time_ms() - start) < ms)
    {
        mini_wfi();

        uint32_t now = mini_time_ms();
        if (now == last)
            no_tick_count++;
        else
        {
            last = now;
            no_tick_count = 0U; /* 时钟一旦前进立刻清零重置 */
        }

        if (no_tick_count > MINI_TIME_TICK_HANG_THRESHOLD)
            break; /* 时基未运行 / 中断被屏蔽: 退出死等 */
    }
#endif
}

void mini_delay_us(uint32_t us)
{
    if (us == 0U)
        return;

#if defined(CONFIG_OS_FREERTOS)
#ifdef ESP_PLATFORM
    /* 厂商 ROM 忙等 (不进 product driver) */
    extern void esp_rom_delay_us(uint32_t us);
    esp_rom_delay_us(us);
#else
    {
        uint32_t          cycles = us * (configCPU_CLOCK_HZ / 1000000U);
        volatile uint32_t iter_index;
        for (iter_index = 0; iter_index < cycles; iter_index++)
            MINI_UNUSED_PARAM(iter_index);
    }
#endif
#elif defined(CONFIG_OS_MINI_OS)
    {
        uint32_t          cycles = us * (MINI_OS_CPU_CLOCK_HZ / 1000000U);
        volatile uint32_t iter_index;
        for (iter_index = 0; iter_index < cycles; iter_index++)
            MINI_UNUSED_PARAM(iter_index);
    }
#elif defined(CONFIG_OS_RTTHREAD)
#ifdef RT_USING_HW_USDELAY
    rt_hw_us_delay(us);
#else
    {
        volatile uint32_t loops = us * 8U;
        while (loops-- > 0U)
            ;
    }
#endif
#else
    /* 裸机: 无可靠 us 时钟, 短忙等; 精度依赖编译器与主频 */
    {
        volatile uint32_t loops = us * 8U;
        while (loops-- > 0U)
            ;
    }
#endif
}
