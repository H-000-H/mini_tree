#ifndef BH_OS_H
#define BH_OS_H

#include <stdbool.h>

#include "bh.h"
#include "bh_config.h"
#include "osal.h"

#ifdef __cplusplus
extern "C" 
{
#endif

struct bh_os
{
    struct bh_queue  q;
    struct osal_sem* sem; /* 二值信号量, 初始计数 0 */
};

static inline int bh_os_init(struct bh_os* b, struct osal_sem* sem)
{
    if (!b || !sem)
        return -1;

    bh_queue_init(&b->q);
    b->sem = sem;
    return 0;
}

/*
 * ISR 调: 入队 + post 二值信号量.
 * px_yield_required 在 ISR 出口调用 osal_yield_from_isr().
 * fn() 仅在 bh 任务被唤醒后执行 —— 调度器切换发生在 ISR 退出之后.
 */
static inline bool bh_os_schedule_from_isr(struct bh_os* b, struct bh_work* w,
                                           bool* px_yield_required)
{
    if (!b || !b->sem)
        return false;

    bool ok = bh_schedule_from_isr(&b->q, w);
    if (!ok)
        return false;

    (void)osal_sem_post_from_isr(b->sem, px_yield_required);
    return true;
}

/*
 * 任务上下文: 入队 + post 二值信号量.
 */
static inline bool bh_os_schedule(struct bh_os* b, struct bh_work* w)
{
    if (!b || !b->sem || bh_in_isr())
        return false;

    bool ok = bh_schedule_from_isr(&b->q, w);
    if (!ok)
        return false;

    (void)osal_sem_post(b->sem);
    return true;
}

/*
 * bh 专用任务入口 (任务上下文, 非 ISR).
 * sem wait 返回时已脱离中断; 随后 drain 执行下半部.
 */
static inline void bh_os_task_entry(void* arg)
{
    struct bh_os* b = (struct bh_os*)arg;
    if (!b || !b->sem)
        return;

    for (;;)
    {
        if (osal_sem_wait(b->sem, OSAL_WAIT_FOREVER) != 0)
            continue;
        bh_drain(&b->q);
    }
}

static inline int bh_os_start_task(struct bh_os* b,
                                   const char* name,
                                   uint32_t stack_size,
                                   uint32_t priority)
{
    if (!b)
        return -1;

    return osal_task_create(name ? name : BH_TASK_NAME,
                            stack_size ? stack_size : BH_TASK_STACK_SIZE,
                            priority,
                            bh_os_task_entry,
                            b,
                            0);
}

#ifdef __cplusplus
}
#endif

#endif /* BH_OS_H */

