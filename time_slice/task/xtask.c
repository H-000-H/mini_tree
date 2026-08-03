/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @license Apache-2.0
 * @file xtask.c
 * @brief 时间片调度器
 * @note 时间片调度器是基于时间片轮转算法实现的调度器
 */
#ifdef CONFIG_OSAL_NULL

#include "xtask.h"

#include "board_devtable.h"
#include "compiler_compat.h"
#include "device.h"
#include "dt_config_gen.h"
#include "interrupt.h"
#include "vfs-tim.h"

/* 占位/无 chosen 板: 调度器 tick 设备缺省为无效 id, xscheduler_start() 直接返回 */
#ifndef CHOSEN_SCHEDULER_TIM
#define CHOSEN_SCHEDULER_TIM ((device_id_t)0)
#endif

x_scheduler g_scheduler;
static struct scheduler_tim_ctx g_sched_tim_ctx;

/**
 * @brief 时间片调度器早期初始化 (pre_execution 自动调用)
 */
pre_execution(160) static void xscheduler_early_init(void) { x_scheduler_init(&g_scheduler); }

/**
 * @brief 启动 tick 设备与 VIRQ
 */
void xscheduler_start(void)
{
    struct device* tick_dev = board_dev_get(CHOSEN_SCHEDULER_TIM);
    if (!tick_dev)
        return;

    if (device_open(tick_dev, NULL) != VFS_OK)
        return;

    /** 从 VFS 拿 hal_tim_device, 填充 ISR top_half 上下文 */
    hal_tim_device* tim = vfs_tim_get_hal_dev(tick_dev);
    if (!tim)
        return;

    g_sched_tim_ctx.tim = tim;
    g_sched_tim_ctx.scheduler = &g_scheduler;

#ifdef CONFIG_VIRQ
    interrupt_virtual_register(VIRQ(tim, 0), scheduler_tim_isr_top, NULL, &g_sched_tim_ctx);

    int irqn = -1;
    int priority = 5;
    COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "irqn", &irqn));
    COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "nvic-priority", &priority));
    interrupt_hw_enable(irqn, (uint32_t)priority);
#endif
}

/**
 * @brief 定时器 ISR 上半部
 * @param arg ctx
 * @param irq_num 号
 * @return VFS_IRQ_ENTRY_NOBOTTOM
 */
int scheduler_tim_isr_top(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    struct scheduler_tim_ctx* ctx = (struct scheduler_tim_ctx*)arg;
    if (ctx && hal_tim_clear_update_flag(ctx->tim) == VFS_OK)
        x_scheduler_tick(ctx->scheduler, 1);
    return VFS_IRQ_ENTRY_NOBOTTOM;
}

/**
 * @brief 注册周期任务
 * @param sched 调度器
 * @param task 任务
 * @param name 名
 * @param cb 回调
 * @param period_ms 周期
 * @return 句柄
 */
x_task_handle_t xscheduler_task_create(x_scheduler* sched, x_task* task, const char* name,
                                       void (*cb)(x_task*), unsigned int period_ms)
{
    if (!sched || !task || !cb)
        return 0;

    task->name = name;
    task->xTask_cb = cb;
    COMPAT_ATOMIC_STORE(&task->period, period_ms, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->next_running,
                        COMPAT_ATOMIC_LOAD(&sched->tick_count, COMPAT_MO_RELAXED) + period_ms,
                        COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->is_running, false, COMPAT_MO_RELAXED); /**< 创建即武装（非运行态），首轮 poll 即可进入 */

    list_add_tail(&task->node, &sched->task_list_head);

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
        return -1;

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
                task->xTask_cb(task);
                COMPAT_ATOMIC_STORE(&task->next_running,
                                    now + COMPAT_ATOMIC_LOAD(&task->period, COMPAT_MO_RELAXED),
                                    COMPAT_MO_RELAXED);
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

#endif /* CONFIG_OSAL_NULL */
