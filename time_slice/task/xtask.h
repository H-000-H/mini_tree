/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file xtask.h
 *@brief 裸机时间片调度器 (仅 CONFIG_OSAL_NULL)
 *@author H-000-H
 *@details
 *   @note 与 FreeRTOS/RT-Thread 等 OS 后端互斥; OS 后端勿包含本头
 */

#ifndef XTASK_H
#define XTASK_H

#ifndef CONFIG_OSAL_NULL
#error "xtask.h is bare-metal only; enable CONFIG_OSAL_NULL or do not include this header"
#endif

#include "compiler_compat.h"
#include "compiler_inline.h"
#include "hal_tim.h"
#include "status.h"
#include "stdint.h"
#ifdef __cplusplus
extern "C"
{
#endif

    typedef uint32_t x_task_handle_t;

    /* ── 侵入式双向链表 ─────────────────────────────────────────────── */

    typedef struct list_node
    {
        struct list_node* next;
        struct list_node* prev;
    } list_node;

    /**
     * @brief 初始化链表节点 (自环)
     * @param[in] node 节点
     */
    COMPAT_STATIC_INLINE int list_init(list_node* node)
    {
        node->next = node;
        node->prev = node;
        return MINI_OK;
    }

    /**
     * @brief 将 new_node 插入到 next 与 prev 之间
     * @param[in] new_node 新节点
     * @param[in] next 后继
     * @param[in] prev 前驱
     */
    COMPAT_STATIC_INLINE int list_add(list_node* new_node, list_node* next, list_node* prev)
    {
        next->prev = new_node;
        new_node->prev = prev;
        new_node->next = next;
        prev->next = new_node;
        return MINI_OK;
    }

    /**
     * @brief 尾插 (即头节点前)
     * @param[in] new_node 新节点
     * @param[in] head 链表头
     */
    COMPAT_STATIC_INLINE int list_add_tail(list_node* new_node, list_node* head) { return list_add(new_node, head, head->prev); }

    /**
     * @brief 从链表摘下节点 (自我删除)
     * @param[in] node 节点
     */
    COMPAT_STATIC_INLINE void list_del(list_node* node)
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
    }

    /**
     * @brief 链表是否为空
     * @param[in] head 链表头
     * @return true 空; false 非空
     */
    COMPAT_STATIC_INLINE bool list_empty(const list_node* head) { return head->next == head; }

    /* ── 任务与调度器 ────────────────────────────────────────────────── */

    /** 任务控制块 (TCB) */
    typedef struct x_task
    {
        const char* name; /**< 任务名 */
        void (*xTask_cb)(struct x_task* param); /**< 任务回调 */
        COMPAT_ATOMIC_UINT period; /**< 周期 (ms) */
        COMPAT_ATOMIC_UINT next_running; /**< 下次到期时刻 (tick) */
        COMPAT_ATOMIC_BOOL is_running; /**< 运行中标志 (重入保护) */
#ifdef CONFIG_XTASK_COROUTINE
        unsigned int pt_line; /**< protothread 让出点行号 (0 = 非协程/协程完成) */
#endif
        list_node node; /**< 链表节点 */
    } x_task;

    /** 调度器 (coop/preempt 共用契约) */
    typedef struct x_scheduler
    {
        COMPAT_ATOMIC_UINT tick_count; /**< 系统滴答计数 */
        list_node task_list_head; /**< 任务链表头 */
    } x_scheduler;

    extern x_scheduler g_scheduler;

    /**
     * @brief 初始化调度器
     * @param[in] sched 调度器
     */
    COMPAT_STATIC_INLINE void x_scheduler_init(x_scheduler* sched)
    {
        COMPAT_ATOMIC_STORE(&sched->tick_count, 0, COMPAT_MO_RELAXED);
        list_init(&sched->task_list_head);
    }

    /**
     * @brief 累加系统滴答
     * @param[in] sched 调度器
     * @param[in] ms 滴答增量
     * @return MINI_OK / MINI_ERR_INVAL
     */
    int x_scheduler_tick(x_scheduler* sched, unsigned int ms);

#ifdef CONFIG_XTASK_COROUTINE
    /** @brief 当前系统滴答 (ms), 调度器内部 tick 计数 (coop/preempt 各自实现) */
    uint32_t x_scheduler_now(void);

    /** @brief 当前正在执行的任务 TCB (主循环上下文返回 NULL) */
    x_task* x_scheduler_current(void);

    /* ── protothread 协程 (PT_*) ──────────────────────────────────────── */
    /**
     * @brief 任务回调内的轻量协程宏 (无堆、无独立栈, 用 switch-case 状态机恢复让出点)
     * @param[in] task 任务 TCB 指针 (x_task*)
     *
     * 用法: 回调内以 PT_BEGIN 开头、PT_END 结尾; 中间用 PT_YIELD / PT_WAIT_UNTIL /
     *       PT_DELAY 让出执行权, 调度器到期后重入回调并从让出点继续。
     * 注意: 跨让出点的局部变量不保留, 需存 TCB 或静态量。
     *       任务创建时 pt_line 须为 0 (首次进入 case 0)。
     */
#define PT_BEGIN(task)                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        switch ((task)->pt_line)                                                                                                                                                                       \
        {                                                                                                                                                                                              \
        case 0:
#define PT_YIELD(task)                                                                                                                                                                                 \
    (task)->pt_line = __LINE__;                                                                                                                                                                        \
    COMPAT_FALLTHROUGH;                                                                                                                                                                                \
    case __LINE__:
#define PT_WAIT_UNTIL(task, cond)                                                                                                                                                                      \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        (task)->pt_line = __LINE__;                                                                                                                                                                    \
        COMPAT_FALLTHROUGH;                                                                                                                                                                            \
    case __LINE__:                                                                                                                                                                                     \
        if (!(cond))                                                                                                                                                                                   \
        {                                                                                                                                                                                              \
            return;                                                                                                                                                                                    \
        }                                                                                                                                                                                              \
    } while (0)
#define PT_DELAY(task, ms)                                                                                                                                                                             \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        COMPAT_ATOMIC_STORE(&(task)->next_running, x_scheduler_now() + (ms), COMPAT_MO_RELAXED);                                                                                                       \
        (task)->pt_line = __LINE__;                                                                                                                                                                    \
        COMPAT_FALLTHROUGH;                                                                                                                                                                            \
    case __LINE__:                                                                                                                                                                                     \
        if ((int32_t)(x_scheduler_now() - COMPAT_ATOMIC_LOAD(&(task)->next_running, COMPAT_MO_RELAXED)) < 0)                                                                                           \
        {                                                                                                                                                                                              \
            return;                                                                                                                                                                                    \
        }                                                                                                                                                                                              \
    } while (0)
#define PT_END(task)                                                                                                                                                                                   \
    }                                                                                                                                                                                                  \
    (task)->pt_line = 0;                                                                                                                                                                               \
    }
#endif /* CONFIG_XTASK_COROUTINE */

#ifdef CONFIG_XTASK_PREEMPT
    /** @brief 创建抢占式任务 (任务池自分配)
     * @param[in] priority 优先级, 越大越优先
     * @return 任务句柄; 池满/非法返回 0
     */
    x_task_handle_t x_scheduler_task_create(const char* name, uint32_t period_ms, uint32_t priority, void (*cb)(x_task*), void* param);

    /** @brief 抢占式调度核心 (主循环调用, 无任务时精确 WFI) */
    int x_task_run_preempt(x_scheduler* sched);

    /** @brief 轮询全局调度器 (与协调式同名, 应用层无感知) */
    void x_scheduler_poll(void);
#else
/** @brief 创建协调式任务 (TCB 由调用方静态分配)
 * @return 任务句柄; 非法参数返回 0
 */
x_task_handle_t xscheduler_task_create(x_task* task, const char* name, void (*cb)(x_task*), unsigned int period_ms);

/** @brief 协调式调度核心 (轮询到期任务) */
int x_task_run(x_scheduler* sched);

/** @brief 轮询全局调度器 (主循环调用) */
void x_scheduler_poll(void);
#endif

    /** @brief 启动调度器: 打开 chosen TIM, 注册 VIRQ
     * @note 必须在 mini_tree_start_tasks() 之后调用
     */
    void xscheduler_start(void);

    /** @brief TIM 上半部: 清 update flag + 累加 tick
     * @return MINI_IRQ_ENTRY_NOBOTTOM
     */
    int scheduler_tim_isr_top(void* context, uint16_t irq_num);

#ifdef __cplusplus
}
#endif

#endif /* XTASK_H */