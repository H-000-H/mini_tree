/* SPDX-License-Identifier: Apache-2.0 */
/*
 * BH (Bottom Half) — 中断下半部工作队列核心
 *
 * 无锁环形队列, ISR 仅入队不执行 fn; 消费者线程 drain 排空
 * pending/executing/rerun 三原子位实现合并与补跑, 执行期间再触发不丢失
 */
#ifndef BH_H
#define BH_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "bh_config.h"
#include "osal.h"

#ifdef __cplusplus
extern "C" 
{
#endif

typedef void (*bh_fn_t)(void* arg);

struct bh_work
{
    bh_fn_t     fn;
    void*       arg;
    atomic_bool pending;    /* 已在队列或正在执行 */
    atomic_bool executing;  /* drain 内 fn() 执行中 (仅消费者写) */
    atomic_bool rerun;      /* fn() 执行期间再次 trigger, drain 结束后补跑 */
};

#define BH_WORK_INIT(f, a) \
    { .fn = (f), .arg = (a), \
      .pending = ATOMIC_VAR_INIT(false), \
      .executing = ATOMIC_VAR_INIT(false), \
      .rerun = ATOMIC_VAR_INIT(false) }

#define BH_QUEUE_MASK (BH_QUEUE_DEPTH - 1U)

_Static_assert((BH_QUEUE_DEPTH >= 2U) && ((BH_QUEUE_DEPTH & (BH_QUEUE_DEPTH - 1U)) == 0U),
               "BH_QUEUE_DEPTH must be a power of two >= 2");

struct bh_queue
{
    struct bh_work*  ring[BH_QUEUE_DEPTH];
    atomic_uint head; /* ISR / 生产者写 */
    atomic_uint tail; /* 消费者写 (仅线程/主循环) */
};

/*
 * 上下半部上下文契约
 * ────────────────────
 *   ISR (上半部):  仅 bh_schedule_from_isr() / bh_*_schedule()
 *                  — 只入队, 绝不调用 w->fn()
 *   线程 (下半部): bh_drain() / bh_bare_poll() / bh_os_task_entry()
 *                  — 必须已退出中断 (osal_in_isr() == 0)
 *
 * ISR return 之后, release 语义保证队列对消费者可见;
 * OS 路径另经 sem post → task wait, 天然在任务上下文执行.
 */

static inline bool bh_in_isr(void)
{
    return osal_in_isr() != 0;
}

static inline void bh_queue_init(struct bh_queue* q)
{
    if (!q)
        return;
    atomic_store_explicit(&q->head, 0U, memory_order_relaxed);
    atomic_store_explicit(&q->tail, 0U, memory_order_relaxed);
}

static inline bool bh_queue_try_push(struct bh_queue* q, struct bh_work* w)
{
    unsigned h = atomic_load_explicit(&q->head, memory_order_relaxed);
    unsigned t = atomic_load_explicit(&q->tail, memory_order_acquire);

    if ((h - t) >= BH_QUEUE_DEPTH)
        return false;

    q->ring[h & BH_QUEUE_MASK] = w;
    atomic_store_explicit(&q->head, h + 1U, memory_order_release);
    return true;
}

static inline bool bh_schedule_rerun(struct bh_queue* q, struct bh_work* w)
{
    bool expected = false;

    if (!atomic_compare_exchange_strong_explicit(
            &w->pending, &expected, true,
            memory_order_acq_rel, memory_order_relaxed))
        return true;

    if (bh_queue_try_push(q, w))
        return true;

    atomic_store_explicit(&w->pending, false, memory_order_release);
    return false;
}

/*
 * ISR 侧入队 (裸机 / OS 共用).
 * 本函数不执行 w->fn(); 调用方 return-from-ISR 后由消费者 drain.
 *
 * 返回 true:  已入队, 或已在队列中 (合并), 或 fn() 执行中 (已记 rerun);
 *        false: 队列满, work 被丢弃 (pending 已回滚).
 */
static inline bool bh_schedule_from_isr(struct bh_queue* q, struct bh_work* w)
{
    bool expected = false;

    if (!q || !w || !w->fn)
        return false;

    if (!atomic_compare_exchange_strong_explicit(
            &w->pending, &expected, true,
            memory_order_acq_rel, memory_order_relaxed)) {
        if (atomic_load_explicit(&w->executing, memory_order_acquire))
            atomic_store_explicit(&w->rerun, true, memory_order_release);
        return true;
    }

    if (!bh_queue_try_push(q, w))
    {
        atomic_store_explicit(&w->pending, false, memory_order_release);
        return false;
    }
    return true;
}

/*
 * 消费者排空队列 — 禁止在 ISR 内调用.
 * fn() 完成后再推进 tail / 释放 pending, 槽位在回调期间保持占用.
 * fn() 执行期间再次 trigger 通过 rerun 补跑, 不静默丢失.
 */
static inline void bh_drain(struct bh_queue* q)
{
    if (!q || bh_in_isr())
        return;

    unsigned t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    for (;;)
    {
        unsigned h = atomic_load_explicit(&q->head, memory_order_acquire);
        if (t == h)
            break;

        struct bh_work* w = q->ring[t & BH_QUEUE_MASK];

        atomic_store_explicit(&w->executing, true, memory_order_release);
        w->fn(w->arg);
        atomic_store_explicit(&w->executing, false, memory_order_release);

        atomic_store_explicit(&q->tail, t + 1U, memory_order_release);
        atomic_store_explicit(&w->pending, false, memory_order_release);
        t++;

        while (atomic_exchange_explicit(&w->rerun, false, memory_order_acq_rel))
        {
            if (bh_schedule_rerun(q, w))
                break;
            atomic_store_explicit(&w->executing, true, memory_order_release);
            w->fn(w->arg);
            atomic_store_explicit(&w->executing, false, memory_order_release);
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* BH_H */

