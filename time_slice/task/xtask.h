/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file xtask.h
 * @brief 裸机时间片调度器 (仅 CONFIG_OSAL_NULL)
 * @note 与 FreeRTOS/RT-Thread 互斥; OS 后端勿包含本头文件
 * @note 链表类型:侵入式链表
 */
#ifndef XTASK_H
#define XTASK_H

#ifndef CONFIG_OSAL_NULL
#error "xtask.h is bare-metal only; enable CONFIG_OSAL_NULL or do not include this header"
#endif

#include "stdint.h"
#include "compiler_compat.h"
#include "status.h"
#include "hal_tim.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t xTaskHandle_t;

/**
 * @brief 链表节点结构体
 */
typedef struct ListNode 
{
    struct ListNode* next;
    struct ListNode* prev;
} ListNode;

/**
 * @brief 初始化链表节点
 */
COMPAT_STATIC_INLINE int list_init(ListNode* node) 
{
    node->next = node;
    node->prev = node;
    return VFS_OK;
}

/**
 * @brief 在两个已知节点之间插入一个新节点
 */
COMPAT_STATIC_INLINE int list_add(ListNode* new_node, ListNode* next, ListNode* pre) 
{
    next->prev          = new_node;
    new_node->prev      = pre;
    new_node->next      = next;
    pre->next           = new_node;
    return VFS_OK;
}

/**
 * @brief 尾插结点也就是头结点前驱
 */
COMPAT_STATIC_INLINE int list_add_tail(ListNode* new_node, ListNode* head)
{
    return list_add(new_node, head, head->prev);
}

struct xTask;

typedef struct xTask
{
    const char* name;                                   /**< 任务名称 */
    void            (*xTask_cb)(struct xTask* param);   /**< 任务回调函数 */
    COMPAT_ATOMIC_UINT period;                          /**< 任务周期 */
    COMPAT_ATOMIC_UINT next_running;                    /**< 下次运行时间 */
    COMPAT_ATOMIC_BOOL is_running;                      /**< 任务是否正在运行 */
    ListNode        node;                               /**< 任务链表节点 */
} xTask;

typedef struct xScheduler
{
    COMPAT_ATOMIC_UINT tick_count;   /**< 虚拟系统滴答计数器 */
    ListNode        task_list_head; /**< 任务链表头 */
} xScheduler;

extern xScheduler g_scheduler;

/**
 * @brief 初始化调度器
 */
COMPAT_STATIC_INLINE void xSchedulerInit(xScheduler* sched)
{
    COMPAT_ATOMIC_STORE(&sched->tick_count, 0, COMPAT_MO_RELAXED);
    list_init(&sched->task_list_head);
}

/**
 * @brief 创建/注册任务
 * @return 返回任务的句柄（指针地址）
 */
xTaskHandle_t xscheduler_task_create(xScheduler* sched, xTask* task, const char* name, void (*cb)(xTask*), unsigned int period_ms);

/**
 * @brief 系统滴答计数器增加
 * @return VFS_OK 成功; VFS_ERR_INVAL 非法参数
 */
int xScheduler_Tick(xScheduler* sched, unsigned int ms);

/**
 * @brief 调度器核心轮询逻辑
 * @return VFS_OK 成功; -1 非法参数
 */
int xTaskRun(xScheduler* sched);

/**
 * @brief 轮询全局调度器（主循环调用）
 */
void xScheduler_Poll(void);

/**
 * @brief 启动调度器 — 通过 VFS 打开 chosen TIM, 注册 VIRQ, 使能 NVIC
 * @note  必须在 mini_tree_start_tasks() 之后调用 (VFS 设备已 probe)
 * @note  NVIC 优先级从 DTS nvic-priority 属性读取
 */
void xscheduler_start(void);

/**
 * @brief 调度器 TIM ISR 上下文 — top_half 使用, 包含 TIM 设备和调度器指针
 */
struct scheduler_tim_ctx
{
    hal_tim_device* tim;        /**< TIM 设备 (用于清 update flag) */
    xScheduler*    scheduler;  /**< 调度器实例 (用于 tick) */
};

/**
 * @brief 调度器 TIM 上半部回调 — 清 update flag + 累加 tick
 * @return VFS_IRQ_ENTRY_NOBOTTOM 不需要下半部
 */
int scheduler_tim_isr_top(void* arg, uint16_t irq_num);

#ifdef __cplusplus
}
#endif

#endif /* XTASK_H */