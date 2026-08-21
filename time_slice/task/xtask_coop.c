/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file xtask_coop.c
 *@brief 协调式时间片调度器 (cooperative / round-robin)
 *@author H-000-H
 *@details
 *   @note 与 xtask_preempt.c 二选一互斥 (Kconfig choice + CMake 双重门控)
 */

#ifdef CONFIG_OSAL_NULL
#ifndef CONFIG_XTASK_PREEMPT

#include "board_devtable.h"
#include "compiler_compat.h"
#include "device.h"
#include "dt_config_gen.h"
#include "hal_systick.h"
#include "interrupt.h"
#include "vfs-tim.h"
#include "xtask.h"

/* 协调式私有状态 (集中定时器状态) */
struct x_coop_priv
{
    hal_tim_device* tim; /**< 定时器 (xscheduler_start 绑定) */
    int tick_delay; /**< 每次中断 tick 增量 */
};

static struct x_coop_priv s_priv;

x_scheduler g_scheduler;

#ifdef CONFIG_XTASK_COROUTINE
/** 当前正在执行的任务 (protothread 协程让出时供感知) */
static x_task* s_current_task;

/** @brief 当前系统滴答 (协调式读 g_scheduler.tick_count) */
uint32_t x_scheduler_now(void)
{
    return COMPAT_ATOMIC_LOAD(&g_scheduler.tick_count, COMPAT_MO_RELAXED);
}

/** @brief 返回当前执行的任务 (主循环上下文为 NULL) */
x_task* x_scheduler_current(void) { return s_current_task; }
#endif /* CONFIG_XTASK_COROUTINE */

/**
 * @brief 时间片调度器早期初始化 (pre_execution 自动调用)
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void xscheduler_early_init(void)
{
    x_scheduler_init(&g_scheduler);
    s_priv.tim = NULL;
    s_priv.tick_delay = 1;
}

/**
 * @brief 启动 tick 源: chosen TIM 显式覆盖优先, 否则默认 SysTick
 * @note  - DTS 配了 chosen { scheduler-tim = &timN; } → 显式覆盖, 走通用 TIM + VIRQ
 *        - 未配 chosen → 默认 SysTick (Cortex-M 架构标准件, 开箱即用; 非 ARM 返回 NOTSUPP)
 * @note  前提: 兜底宏已移除, CHOSEN_SCHEDULER_TIM 仅在 DTS 显式配置时由 dtc-lite 生成,
 *        #ifdef 编译期即可判定用户是否显式选择 TIM。
 */
void xscheduler_start(void)
{
    /* ① DTS 显式配了 scheduler-tim → 显式覆盖, 直接走 chosen TIM */
#ifdef CHOSEN_SCHEDULER_TIM
    struct device* tick_dev = board_dev_get(CHOSEN_SCHEDULER_TIM);
    if (tick_dev != NULL && device_open(tick_dev, NULL) == VFS_OK)
    {
        s_priv.tim = vfs_tim_get_hal_dev(tick_dev);
        if (s_priv.tim != NULL)
        {
#ifdef CONFIG_VIRQ
            COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "tick_delay", &s_priv.tick_delay));
            interrupt_virtual_register(VIRQ(tim, 0), scheduler_tim_isr_top, NULL, &s_priv);

            int irqn = -1;
            int priority = 5;
            COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "irqn", &irqn));
            COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "nvic-priority", &priority));
            interrupt_hw_enable(irqn, (uint32_t)priority);
#endif
            return;
        }
    }
#endif

    /* ② 没配 chosen (或打开失败) → 默认 SysTick (仅 Cortex-M 存在; 非 ARM 返回 NOTSUPP) */
    if (hal_systick_init(DTC_GEN_TICK_RATE_HZ) == VFS_OK)
    {
        s_priv.tim = NULL; /* SysTick 路径: 不占用通用 TIM */
        /* 每 SysTick 中断推进 ms = 1000 / tick-rate; 亚毫秒 tick-rate 按 1ms 兜底 */
        int delay = 1000 / DTC_GEN_TICK_RATE_HZ;
        s_priv.tick_delay = (delay >= 1) ? delay : 1;
        return;
    }
}

/**
 * @brief SysTick 中断业务钩子 (强符号覆盖 hal_systick 的 weak 空实现)
 * @note  仅 SysTick 作为默认 tick 源时由硬件中断调用; 累加系统滴答。
 */
void hal_systick_irq_handler(void)
{
    x_scheduler_tick(&g_scheduler, (unsigned int)s_priv.tick_delay);
}

/**
 * @brief 定时器 ISR 上半部
 * @param arg 指向 x_coop_priv
 * @param irq_num 号
 * @return VFS_IRQ_ENTRY_NOBOTTOM
 */
int scheduler_tim_isr_top(void* context, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    struct x_coop_priv* priv = (struct x_coop_priv*)context;
    if (priv == NULL)
        return VFS_IRQ_ENTRY_NOBOTTOM;

    struct vfs_tim_arg tim_arg = {0};
    tim_arg.obj = priv->tim;
    if (vfs_tim_fast_clear_update_flag(&tim_arg) == VFS_OK)
        x_scheduler_tick(&g_scheduler, (unsigned int)priv->tick_delay);
    return VFS_IRQ_ENTRY_NOBOTTOM;
}

/**
 * @brief 注册周期任务 (使用全局 g_scheduler)
 * @param task 调用方静态分配的 x_task TCB
 * @param name 任务名
 * @param cb 回调
 * @param period_ms 周期
 * @return 句柄
 */
x_task_handle_t xscheduler_task_create(x_task* task, const char* name, void (*cb)(x_task*),
                                       unsigned int period_ms)
{
    if (!task || !cb || !name)
        return VFS_ERR_INVAL;

    task->name = name;
    task->xTask_cb = cb;
    COMPAT_ATOMIC_STORE(&task->period, period_ms, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->next_running,
                        COMPAT_ATOMIC_LOAD(&g_scheduler.tick_count, COMPAT_MO_RELAXED) + period_ms,
                        COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->is_running, false,
                        COMPAT_MO_RELAXED); /**<（非运行态），首轮 poll 即可进入 */
#ifdef CONFIG_XTASK_COROUTINE
    task->pt_line = 0; /**< 协程让出点复位 (首次进入 case 0) */
#endif

    list_add_tail(&task->node, &g_scheduler.task_list_head);

    return (x_task_handle_t)(uintptr_t)task;
}

/**
 * @brief 递增 tick
 * @param sched 调度器
 * @param ms 毫秒
 * @return VFS_OK
 */
int x_scheduler_tick(x_scheduler* sched, unsigned int ms)
{
    if (!sched)
        return VFS_ERR_INVAL;
    COMPAT_ATOMIC_ADD_FETCH(&sched->tick_count, ms, COMPAT_MO_RELAXED);
    return VFS_OK;
}

/**
 * @brief 运行到期任务
 * @param sched 调度器
 * @return VFS_OK
 */
int x_task_run(x_scheduler* sched)
{
    if (!sched)
        return VFS_ERR_INVAL;

    list_node* head = &sched->task_list_head;
    list_node* current = head->next;

    while (current != head)
    {
        list_node* next = current->next;
        struct x_task* task = container_of(current, struct x_task, node);

        if (!COMPAT_ATOMIC_LOAD(&task->is_running, COMPAT_MO_RELAXED)) /**< 非运行状态才允许进入 */
        {
            COMPAT_ATOMIC_STORE(&task->is_running, true, COMPAT_MO_RELAXED);
            uint32_t now = COMPAT_ATOMIC_LOAD(&sched->tick_count, COMPAT_MO_RELAXED);
            uint32_t next_run = COMPAT_ATOMIC_LOAD(&task->next_running, COMPAT_MO_RELAXED);

            if ((int32_t)(now - next_run) >= 0)
            {
#ifdef CONFIG_XTASK_COROUTINE
                s_current_task = task; /* protothread 让出时感知当前任务 */
                task->xTask_cb(task);
                s_current_task = NULL;
                if (task->pt_line == 0)
                {
                    /* 协程跑完 (PT_END 复位) 或普通回调: 按周期推进下一轮 */
                    COMPAT_ATOMIC_STORE(&task->next_running,
                                        now + COMPAT_ATOMIC_LOAD(&task->period, COMPAT_MO_RELAXED),
                                        COMPAT_MO_RELAXED);
                }
                /* else: 协程挂起中, PT_DELAY 已设 next_running, 保持到期时刻 */
#else
                task->xTask_cb(task);
                COMPAT_ATOMIC_STORE(&task->next_running,
                                    now + COMPAT_ATOMIC_LOAD(&task->period, COMPAT_MO_RELAXED),
                                    COMPAT_MO_RELAXED);
#endif
            }
            /* 无论到期与否都复位：否则未到期分支会把 is_running 卡在 true，任务永不调度 */
            COMPAT_ATOMIC_STORE(&task->is_running, false, COMPAT_MO_RELAXED);
        }
        current = next;
    }
    return VFS_OK;
}

/**
 * @brief poll g_scheduler
 */
void x_scheduler_poll(void) { x_task_run(&g_scheduler); }

#endif /* !CONFIG_XTASK_PREEMPT */
#endif /* CONFIG_OSAL_NULL */
