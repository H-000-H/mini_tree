/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file xtask_preempt.c
 *@brief 抢占式分组优先级调度器 (CONFIG_XTASK_PREEMPT)
 *@author H-000-H
 *@details
 *   @note 与 xtask_coop.c 二选一互斥 (Kconfig choice + CMake 双重门控)
 *   设计:
 *   - 总级数 = GROUP × PER_GROUP (默认 4×8=32), 越大越优先
 *   - 组间用 group_bitmap + CLZ 定位最高优先级 (O(1)), 组内链表按优先级降序
 *   - 就绪链表每组一条; 休眠链表单条按到期升序, 只查表头
 *   - 无就绪任务时精确 WFI: 定时器单次触发到最早到期时刻
 *   - 全部状态收于 s_priv, 对外 API 走 g_scheduler (xtask.h 契约)
 */

#ifdef CONFIG_OSAL_NULL
#ifdef CONFIG_XTASK_PREEMPT

#include "board_devtable.h"
#include "compiler_compat.h"
#include "device.h"
#include "dt_config_gen.h"
#include "hal_systick.h"
#include "interrupt.h"
#include "osal_null.h"
#include "vfs-tim.h"
#include "xtask.h"

/* -------------------------------------------------------------------------- */
/* 分组优先级参数 (Kconfig 控制) */
/* -------------------------------------------------------------------------- */

#ifndef CONFIG_X_PREEMPT_PRIO_LEVELS
#define CONFIG_X_PREEMPT_PRIO_LEVELS 32
#endif
#ifndef CONFIG_X_PREEMPT_PRIO_PER_GROUP
#define CONFIG_X_PREEMPT_PRIO_PER_GROUP 8
#endif
#ifndef CONFIG_X_PREEMPT_MAX_TASKS
#define CONFIG_X_PREEMPT_MAX_TASKS 8
#endif
#define X_PREEMPT_PRIO_LEVELS CONFIG_X_PREEMPT_PRIO_LEVELS
#define X_PREEMPT_PRIO_PER_GROUP CONFIG_X_PREEMPT_PRIO_PER_GROUP
#define X_PREEMPT_PRIO_GROUP (X_PREEMPT_PRIO_LEVELS / X_PREEMPT_PRIO_PER_GROUP)

#if (X_PREEMPT_PRIO_LEVELS % X_PREEMPT_PRIO_PER_GROUP) != 0
#error "X_PREEMPT_PRIO_LEVELS must be a multiple of X_PREEMPT_PRIO_PER_GROUP"
#endif
#if X_PREEMPT_PRIO_GROUP > 32
#error "X_PREEMPT_PRIO_GROUP must be <= 32 (uint32_t group bitmap)"
#endif

/* -------------------------------------------------------------------------- */
/* 数据结构 */
/* -------------------------------------------------------------------------- */

/** 抢占式任务 (池槽, 内嵌 x_task 供对外句柄; 到期时刻复用 x_task.next_running) */
struct x_preempt_task
{
    x_task task; /**< 基础任务 (name/cb/period/next_running) */
    uint8_t priority; /**< 0..LEVELS-1, 越大越优先 */
    list_node ready_node; /**< 挂就绪链表 */
    list_node sleep_node; /**< 挂休眠链表 */
};

/** 调度器私有状态 (集中全部状态) */
struct x_preempt_priv
{
    uint32_t tick_count; /**< 系统滴答 */
    uint32_t group_bitmap; /**< 组就绪位图 */
    list_node ready_head[X_PREEMPT_PRIO_GROUP]; /**< 每组一条就绪链表 */
    list_node sleep_head; /**< 休眠链表 */
    hal_tim_device* tim; /**< 定时器 (xscheduler_start 绑定, SysTick 路径为 NULL) */
    uint32_t tick_period; /**< 周期 ARR (WFI 后恢复, 仅通用 TIM 路径) */
    int tick_delay; /**< 周期模式每次中断 tick 增量 (ms) */
    uint32_t oneshot_ticks; /**< 当前 ARR 对应的 tick 增量 (单次休眠记账, 周期模式 = tick_delay) */
    bool systick_active; /**< 当前 tick 源为 SysTick (架构异常直连) */
    struct x_preempt_task task[CONFIG_X_PREEMPT_MAX_TASKS]; /**< 任务池 */
};

/* -------------------------------------------------------------------------- */
/* 全局 */
/* -------------------------------------------------------------------------- */

x_scheduler g_scheduler = {0}; /**< 对外契约 (xtask.h), preempt 内部不用其字段 */
static struct x_preempt_priv s_priv; /**< 内部完整状态 */

#ifdef CONFIG_XTASK_COROUTINE
/** 当前正在执行的任务 (protothread 协程让出时供感知) */
static x_task* s_current_task;

/** @brief 当前系统滴答 (抢占式读内部 s_priv.tick_count) */
uint32_t x_scheduler_now(void) { return s_priv.tick_count; }

/** @brief 返回当前执行的任务 (主循环上下文为 NULL) */
x_task* x_scheduler_current(void) { return s_current_task; }
#endif /* CONFIG_XTASK_COROUTINE */
/* -------------------------------------------------------------------------- */
/* 内部工具 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 计算优先级所在组
 * @param[in] priority 优先级
 * @return 组号
 */
static uint32_t prio_group(uint32_t priority) { return priority / X_PREEMPT_PRIO_PER_GROUP; }

/**
 * @brief 就绪链表: 组内按优先级降序插入
 * @param[in] t 任务
 */
static void ready_insert(struct x_preempt_task* task)
{
    uint32_t group = prio_group(task->priority);
    list_node* head = &s_priv.ready_head[group];
    list_node* pos = head->next;

    while (pos != head)
    {
        struct x_preempt_task* cur = container_of(pos, struct x_preempt_task, ready_node);
        if (cur->priority <= task->priority)
            break;
        pos = pos->next;
    }
    list_add(&task->ready_node, pos, pos->prev);
    s_priv.group_bitmap |= (1u << group);
}

/**
 * @brief 就绪链表: 摘下任务, 组空则清位图
 * @param[in] t 任务
 */
static void ready_remove(struct x_preempt_task* task)
{
    uint32_t group = prio_group(task->priority);
    list_del(&task->ready_node);
    if (list_empty(&s_priv.ready_head[group]))
        s_priv.group_bitmap &= ~(1u << group);
}

/**
 * @brief 休眠链表: 按到期时刻升序插入
 * @param[in] t 任务
 */
static void sleep_insert(struct x_preempt_task* task)
{
    list_node* head = &s_priv.sleep_head;
    list_node* pos = head->next;

    while (pos != head)
    {
        struct x_preempt_task* cur = container_of(pos, struct x_preempt_task, sleep_node);
        if (COMPAT_ATOMIC_LOAD(&cur->task.next_running, COMPAT_MO_RELAXED) >
            COMPAT_ATOMIC_LOAD(&task->task.next_running, COMPAT_MO_RELAXED))
            break;
        pos = pos->next;
    }
    list_add(&task->sleep_node, pos, pos->prev);
}

/** 取最高优先级就绪任务 (O(1): CLZ + 组内表头) */
static struct x_preempt_task* ready_highest(void)
{
    if (s_priv.group_bitmap == 0)
        return NULL;
    uint32_t group_index = 31u - COMPAT_CLZ(s_priv.group_bitmap);
    list_node* head = &s_priv.ready_head[group_index];
    if (list_empty(head))
        return NULL;
    return container_of(head->next, struct x_preempt_task, ready_node);
}

/** 唤醒所有到期任务: 休眠表头 → 就绪链表 */
static void wakeup_due(void)
{
    while (!list_empty(&s_priv.sleep_head))
    {
        struct x_preempt_task* task =
            container_of(s_priv.sleep_head.next, struct x_preempt_task, sleep_node);
        if ((int32_t)(COMPAT_ATOMIC_LOAD(&task->task.next_running, COMPAT_MO_RELAXED) -
                      s_priv.tick_count) > 0)
            break; /* 表头未到期, 有序性保证后续全未到期 */
        list_del(&task->sleep_node);
        ready_insert(task);
    }
}

/** 无就绪任务时精确休眠: 睡到最早到期时刻 (通用 TIM) 或等下一个 SysTick 中断 */
static void idle_wfi(void)
{
    /* SysTick 路径: 固定周期架构中断, WFI 等下一个 tick 即可省电 (无法改单次到期时刻) */
    if (s_priv.systick_active)
    {
        COMPAT_WFI();
        return;
    }

    if (list_empty(&s_priv.sleep_head) || s_priv.tim == NULL || s_priv.tick_period == 0)
        return;

    struct x_preempt_task* next =
        container_of(s_priv.sleep_head.next, struct x_preempt_task, sleep_node);
    uint32_t remaining =
        COMPAT_ATOMIC_LOAD(&next->task.next_running, COMPAT_MO_RELAXED) - s_priv.tick_count;

    /* 单次休眠钳位: 通用 TIM 计数器可能仅 16 位 (如 TIM7), ARR = period × remaining
     * 超宽会被硬件截断 → 提前唤醒且记账失真。按 16 位上限截断休眠时长,
     * 到期前分多次休眠, 每次按真实休眠时长记账。 */
    uint32_t max_sleep = 0xFFFFu / s_priv.tick_period;
    if (remaining > max_sleep)
        remaining = max_sleep;

    uint32_t arr = s_priv.tick_period * remaining;
    if (arr == 0)
        return;

    struct vfs_tim_arg tim_arg = {0};
    tim_arg.obj = s_priv.tim;
    COMPAT_IGNORE_RESULT(vfs_tim_fast_set_counter(&tim_arg));
    tim_arg.arr = arr;
    s_priv.oneshot_ticks = remaining; /* ISR 按本次休眠时长记账 */
    COMPAT_IGNORE_RESULT(vfs_tim_fast_set_autoreload(&tim_arg));
    COMPAT_WFI();

    /* 非更新事件唤醒 (其他中断先行): 按计数器实际流逝补偿, 防 tick 欠账 */
    tim_arg.value = 0;
    if (vfs_tim_fast_get_counter(&tim_arg) == MINI_OK && tim_arg.value != 0)
    {
        uint32_t elapsed = tim_arg.value / s_priv.tick_period;
        if (elapsed > 0)
        {
            s_priv.oneshot_ticks -= elapsed;
            x_scheduler_tick(&g_scheduler, elapsed);
        }
    }

    tim_arg.arr = s_priv.tick_period;
    COMPAT_IGNORE_RESULT(vfs_tim_fast_set_autoreload(&tim_arg));
    s_priv.oneshot_ticks = (uint32_t)s_priv.tick_delay; /* 恢复周期模式记账 */
}

/* -------------------------------------------------------------------------- */
/* 对外 API */
/* -------------------------------------------------------------------------- */

pre_execution(PRE_EXEC_PRIO_SCHEDULER) static void xscheduler_early_init(void)
{
    uint32_t group;
    for (group = 0; group < X_PREEMPT_PRIO_GROUP; group++)
        list_init(&s_priv.ready_head[group]);
    list_init(&s_priv.sleep_head);
    s_priv.tick_count = 0;
    s_priv.group_bitmap = 0;
    s_priv.tim = NULL;
    s_priv.tick_period = 0;
    s_priv.tick_delay = 1;
    s_priv.oneshot_ticks = 1;
    s_priv.systick_active = false;
}

/**
 * @brief 启动 tick 源: chosen TIM 显式覆盖优先, 否则默认 SysTick
 * @note  - DTS 配了 chosen { scheduler-tim = &timN; } → 显式覆盖, 走通用 TIM + VIRQ
 *        - 未配 chosen → 默认 SysTick (Cortex-M 架构标准件; 非 ARM 返回 NOTSUPP)
 * @note  SysTick 路径下 s_priv.tim 置 NULL, idle_wfi 的精确休眠退化为等 SysTick 中断
 *        (固定周期, 无需软件改 ARR)。
 */
void xscheduler_start(void)
{
    /* ① DTS 显式配了 scheduler-tim → 显式覆盖, 直接走 chosen TIM */
#ifdef CHOSEN_SCHEDULER_TIM
    struct device* tick_dev = board_dev_get(CHOSEN_SCHEDULER_TIM);
    if (tick_dev != NULL && device_open(tick_dev, NULL) == MINI_OK)
    {
        s_priv.tim = vfs_tim_get_hal_dev(tick_dev);
        if (s_priv.tim != NULL)
        {
            /* 记录周期 tick 的硬件 ARR (精确 WFI 后恢复用, 走 VFS 快路径) */
            struct vfs_tim_arg tim_arg = {0};
            tim_arg.obj = s_priv.tim;
            if (vfs_tim_fast_get_autoreload(&tim_arg) == MINI_OK && tim_arg.value != 0)
                s_priv.tick_period = tim_arg.value;
            s_priv.oneshot_ticks = (uint32_t)s_priv.tick_delay;

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
    if (hal_systick_init(DTC_GEN_TICK_RATE_HZ) == MINI_OK)
    {
        s_priv.tim = NULL; /* SysTick 路径: 不占用通用 TIM */
        s_priv.systick_active = true;
        /* 每 SysTick 中断推进 ms = 1000 / tick-rate; 亚毫秒 tick-rate 按 1ms 兜底 */
        int delay = 1000 / DTC_GEN_TICK_RATE_HZ;
        s_priv.tick_delay = (delay >= 1) ? delay : 1;
        return;
    }
}

/**
 * @brief SysTick 中断业务钩子 (强符号覆盖 hal_systick 的 weak 空实现)
 * @note  仅 SysTick 作为默认 tick 源时由硬件中断调用; 累加系统滴答并唤醒到期任务。
 */
void hal_systick_irq_handler(void)
{
    x_scheduler_tick(&g_scheduler, (unsigned int)s_priv.tick_delay);
}

/**
 * @brief 创建抢占式任务 (任务池自分配)
 * @param[in] name 任务名
 * @param[in] period_ms 周期 (ms)
 * @param[in] priority 优先级, 越大越优先
 * @param[in] cb 回调
 * @param[in] param 透传参数 (忽略)
 * @return 任务句柄; 池满/非法返回 0
 */
x_task_handle_t x_scheduler_task_create(const char* name, uint32_t period_ms, uint32_t priority,
                                        void (*cb)(x_task*), void* param)
{
    COMPAT_IGNORE_RESULT(param);
    if (!cb || !name || priority >= X_PREEMPT_PRIO_LEVELS)
        return 0;

    /* 从任务池找空闲槽 */
    struct x_preempt_task* slot = NULL;
    for (uint32_t index = 0; index < CONFIG_X_PREEMPT_MAX_TASKS; index++)
    {
        if (s_priv.task[index].task.name == NULL)
        {
            slot = &s_priv.task[index];
            break;
        }
    }
    if (!slot)
        return 0;

    x_task* task = &slot->task;
    task->name = name;
    task->xTask_cb = cb;
    slot->priority = (uint8_t)priority;
    COMPAT_ATOMIC_STORE(&task->next_running, s_priv.tick_count + period_ms, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->period, period_ms, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->is_running, false, COMPAT_MO_RELAXED);
#ifdef CONFIG_XTASK_COROUTINE
    task->pt_line = 0; /**< 协程让出点复位 (首次进入 case 0) */
#endif
    list_init(&slot->ready_node);
    list_init(&slot->sleep_node);

    /* 与 tick 中断互斥: 休眠链表可能正被 wakeup_due 修改 */
    uint32_t irq = osal_null_irq_disable();
    sleep_insert(slot); /* 首个周期后唤醒 */
    osal_null_irq_restore(irq);
    return (x_task_handle_t)(uintptr_t)task;
}

/**
 * @brief TIM 上半部: 清 update flag + 累加 tick
 * @param[in] arg 指向 x_preempt_priv
 * @param[in] irq_num 中断号
 * @return MINI_OK
 */
int scheduler_tim_isr_top(void* context, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    struct x_preempt_priv* priv = (struct x_preempt_priv*)context;
    if (priv == NULL)
        return MINI_OK;

    struct vfs_tim_arg tim_arg = {0};
    tim_arg.obj = priv->tim;
    if (vfs_tim_fast_clear_update_flag(&tim_arg) == MINI_OK)
        x_scheduler_tick(&g_scheduler, (unsigned int)priv->oneshot_ticks);
    return MINI_OK;
}

/**
 * @brief 累加系统滴答并唤醒到期任务
 * @param[in] sched 忽略 (preempt 用全局 s_priv)
 * @param[in] ms 滴答增量
 * @return MINI_OK
 */
int x_scheduler_tick(x_scheduler* sched, unsigned int ms)
{
    s_priv.tick_count += ms;
    /* 同步对外契约时钟 (osal_time_ms 等读 g_scheduler.tick_count) */
    COMPAT_ATOMIC_STORE(&g_scheduler.tick_count, s_priv.tick_count, COMPAT_MO_RELAXED);
    if (sched != NULL)
        COMPAT_IGNORE_RESULT(sched); /* preempt 用全局 s_priv, sched 仅契约 */
    wakeup_due();
    return MINI_OK;
}

/**
 * @brief 抢占式调度核心: 运行最高优先级任务, 无任务时精确 WFI
 * @param[in] sched 忽略 (preempt 用全局 s_priv)
 * @return MINI_OK
 */
int x_task_run_preempt(x_scheduler* sched)
{
    COMPAT_IGNORE_RESULT(sched); /* preempt 用全局 s_priv */

    /* 临界区: 与 tick 中断 (wakeup_due) 互斥, 防就绪/休眠链表被撕裂 */
    uint32_t irq = osal_null_irq_disable();
    struct x_preempt_task* task = ready_highest();
    if (task == NULL)
    {
        osal_null_irq_restore(irq);
        idle_wfi(); /* 无就绪任务 → 精确休眠 (WFI 须在中断使能态执行) */
        return MINI_OK;
    }

    ready_remove(task);
    osal_null_irq_restore(irq);
    if (task->task.xTask_cb)
    {
#ifdef CONFIG_XTASK_COROUTINE
        s_current_task = &task->task; /* protothread 让出时感知当前任务 */
        task->task.xTask_cb(&task->task);
        s_current_task = NULL;

        if (task->task.pt_line == 0)
        {
            /* 协程跑完 (PT_END 复位) 或普通回调: 按周期推进下一轮 */
            COMPAT_ATOMIC_STORE(&task->task.next_running, s_priv.tick_count + task->task.period,
                                COMPAT_MO_RELAXED);
        }
        /* else: 协程挂起中, PT_DELAY 已设 next_running, sleep_insert 按到期排序 */
#else
        task->task.xTask_cb(&task->task);
        COMPAT_ATOMIC_STORE(&task->task.next_running, s_priv.tick_count + task->task.period,
                            COMPAT_MO_RELAXED);
#endif
    }
    /* 与 tick 中断互斥: wakeup_due 可能正从休眠链表摘节点 */
    irq = osal_null_irq_disable();
    sleep_insert(task);
    osal_null_irq_restore(irq);
    return MINI_OK;
}

/** 轮询全局抢占式调度器 (与协调式同名, 应用层无感知) */
void x_scheduler_poll(void) { x_task_run_preempt(&g_scheduler); }

#endif /* CONFIG_XTASK_PREEMPT */
#endif /* CONFIG_OSAL_NULL */
