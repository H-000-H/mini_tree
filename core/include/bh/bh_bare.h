/* SPDX-License-Identifier: Apache-2.0 */
/*
 * bh_bare — 裸机 (无 OS) 下半部适配
 *
 * 在 bh 队列上叠加 pending_drain 标志, ISR 置位、主循环轮询
 * bh_bare_poll() 先清标志再 drain, drain 期间新 ISR 重新置位, 防丢唤醒
 */
#ifndef BH_BARE_H
#define BH_BARE_H

#include <stdbool.h>

#include "bh.h"

#ifdef __cplusplus
extern "C" 
{
#endif

struct bh_bare
{
    struct bh_queue    q;
    volatile bool pending_drain; /* ISR 写, 主循环读 */
};

static inline void bh_bare_init(struct bh_bare* b)
{
    if (!b)
        return;
    bh_queue_init(&b->q);
    b->pending_drain = false;
}

/*
 * ISR 调: 仅入队 + 置 pending_drain, 不执行 fn.
 * return-from-ISR 后, 主循环 bh_bare_poll() 才 drain.
 */
static inline bool bh_bare_schedule(struct bh_bare* b, struct bh_work* w)
{
    if (!b)
        return false;

    bool ok = bh_schedule_from_isr(&b->q, w);
    if (ok)
        b->pending_drain = true;
    return ok;
}

/*
 * 主循环轮询 (线程上下文, 禁止在 ISR 内调用).
 * 先清 pending_drain 再 drain: drain 期间新 ISR 会重新置位, 防丢唤醒.
 */
static inline void bh_bare_poll(struct bh_bare* b)
{
    if (!b || !b->pending_drain || bh_in_isr())
        return;

    b->pending_drain = false;
    bh_drain(&b->q);
}

#ifdef __cplusplus
}
#endif

#endif /* BH_BARE_H */

