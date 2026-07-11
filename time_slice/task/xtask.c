/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @license Apache-2.0
 * @file xtask.c
 * @brief 时间片调度器
 * @note 时间片调度器是基于时间片轮转算法实现的调度器
*/
#ifdef CONFIG_OSAL_NULL

#include "xtask.h"
#include "compiler_compat.h"
#include "dt_config_gen.h"
#include "interrupt.h"
#include "device.h"
#include "board_devtable.h"
#include "vfs-tim.h"

xScheduler g_scheduler;
static struct scheduler_tim_ctx g_sched_tim_ctx;

pre_execution(160)
static void xscheduler_early_init(void)
{
    xSchedulerInit(&g_scheduler);
}

/**
 * @brief 调度器启动 — 通过 DTS chosen 引用打开调度器 TIM, 注册 VIRQ, 使能 NVIC
 * @note  必须在 board_driver_probe_all() 之后调用 (VFS 设备已就绪)
 * @note  TIM 的 PSC/ARR/CounterMode/DIER 全部由 DTS → HAL 自动完成
 * @note  NVIC 优先级从 DTS nvic-priority 属性读取, irqn 从 DTS irqn 读取
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

    g_sched_tim_ctx.tim       = tim;
    g_sched_tim_ctx.scheduler = &g_scheduler;

    /** 先注册 VIRQ 再使能 NVIC: 保证第一次 ISR 进来时 top_half 已就绪 */
    interrupt_virtual_register(VIRQ(tim, 0), scheduler_tim_isr_top, NULL, &g_sched_tim_ctx);

    int irqn = -1;
    int priority = 5;
    COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "irqn", &irqn));
    COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "nvic-priority", &priority));
    interrupt_hw_enable(irqn, (uint32_t)priority);
}

int scheduler_tim_isr_top(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    struct scheduler_tim_ctx* ctx = (struct scheduler_tim_ctx*)arg;
    if (ctx && hal_tim_clear_update_flag(ctx->tim) == VFS_OK)
        xScheduler_Tick(ctx->scheduler, 1);
    return VFS_IRQ_ENTRY_NOBOTTOM;
}

xTaskHandle_t xTaskCreate(xScheduler* sched, xTask* task, const char* name, void (*cb)(xTask*), unsigned int period_ms)
{
    if (!sched || !task || !cb) return 0;

    task->name          = name;
    task->xTask_cb      = cb;
    COMPAT_ATOMIC_STORE(&task->period, period_ms, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->next_running, COMPAT_ATOMIC_LOAD(&sched->tick_count, COMPAT_MO_RELAXED) + period_ms, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->is_running, true, COMPAT_MO_RELAXED);

    list_add_tail(&task->node, &sched->task_list_head);

    return (xTaskHandle_t)(uintptr_t)task;
}

int xScheduler_Tick(xScheduler* sched, unsigned int ms)
{
    if (!sched) return VFS_ERR_INVAL;
    COMPAT_ATOMIC_ADD_FETCH(&sched->tick_count, ms, COMPAT_MO_RELAXED);
    return VFS_OK;
}

int xTaskRun(xScheduler* sched)
{
    if (!sched) return -1;

    ListNode* head = &sched->task_list_head;
    ListNode* current = head->next;

    while (current != head)
    {
        ListNode* next = current->next;
        struct xTask* task = container_of(current,struct xTask, node);

        if (!COMPAT_ATOMIC_LOAD(&task->is_running, COMPAT_MO_RELAXED))/**<非运行状态才允许进入> */
        {
            COMPAT_ATOMIC_STORE(&task->is_running, true, COMPAT_MO_RELAXED);
            uint32_t now = COMPAT_ATOMIC_LOAD(&sched->tick_count, COMPAT_MO_RELAXED);
            uint32_t next_run = COMPAT_ATOMIC_LOAD(&task->next_running, COMPAT_MO_RELAXED);

            if ((int32_t)(now - next_run) >= 0)
            {
                task->xTask_cb(task);
                COMPAT_ATOMIC_STORE(&task->is_running, false, COMPAT_MO_RELAXED);
                COMPAT_ATOMIC_STORE(&task->next_running, now + COMPAT_ATOMIC_LOAD(&task->period, COMPAT_MO_RELAXED), COMPAT_MO_RELAXED);
            }
        }
        current = next;
    }
    return VFS_OK;
}

void xScheduler_Poll(void)
{
    xTaskRun(&g_scheduler);
}
#endif