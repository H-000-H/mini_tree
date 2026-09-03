/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file schedule.c
 * @brief Scheduler implementation
 * @author H-000-H
 * @details Round-robin selection inside one priority level, with threads A, B
 *          and C sharing a priority:
 *  - current is B (middle): sentinel -> A -> B(current) -> C -> sentinel,
 *    current->next is C (not the sentinel), so C is chosen
 *  - current is C (tail): sentinel -> A -> B -> C(current) -> sentinel,
 *    current->next is the sentinel, so the list head (A) is chosen (wraps)
 *  - single thread: sentinel -> A(current) -> sentinel, the successor is the
 *    sentinel, so the head is A again and the thread keeps running
 */
#include "schedule.h"

#include "err.h"
#include "list.h"
#include "mini_config.h"
#include "port.h"
#include "redef.h"
#include "thread.h"
#include "timer.h"

/** @brief Ready bitmap: bit i set = priority i has a ready or running thread (declared extern in
 * schedule.h / thread.h) */
mini_os_uint32_t g_priority = 0u;

/** @brief Ready/running list head per priority (declared extern in schedule.h) */
mini_os_list_t g_ready_running_list[MINI_OS_PRIORITY];

extern mini_os_thread_t* mini_os_current_thread;

/** @brief Priority level the scheduler selected on the last switch */
static mini_os_uint8_t s_current_priority = 0;

/** @brief Global OS tick counter, advanced by the SysTick handler */
mini_os_uint32_t g_global_tick = 0;
#if MINI_OS_LONG_TIME
/** @brief Number of times g_global_tick wrapped around (high half of a 64-bit tick) */
mini_os_uint32_t g_global_tick_overflow = 0;
#endif

/** @brief Thread time wheel slots (MINI_OS_TICK_WHEEL must be a power of 2) */
MINI_OS_ASSERT((MINI_OS_TICK_WHEEL & MINI_OS_TICK_WHEEL_MASK) == 0, "MINI_OS_TICK_WHEEL must be a power of 2");

static mini_os_list_t s_wheel[MINI_OS_TICK_WHEEL];

/** @brief Slot of the thread time wheel serviced on the next tick */
static mini_os_uint32_t s_current_slot = 0;

/**
 * @brief Initialize the scheduler (ready lists, time wheel, slot cursor)
 * @return MINI_OS_OK always
 * @details every ready list head and every wheel slot becomes a self-referencing
 *          sentinel, so the list helpers can be used before the first insert
 * @note must run before any thread is created or started
 */
mini_os_err_t mini_os_schedule_init(void)
{
    mini_os_uint32_t i;

    g_priority = 0;
    for (i = 0; i < (mini_os_uint32_t)MINI_OS_PRIORITY; i++)
        mini_os_list_init(&g_ready_running_list[i]);
    for (i = 0; i < (mini_os_uint32_t)MINI_OS_TICK_WHEEL; i++)
        mini_os_list_init(&s_wheel[i]);
    s_current_slot = 0;
    return MINI_OS_OK;
}

/**
 * @brief Start the scheduler (performs the very first context switch)
 * @return MINI_OS_OK once the first switch has been requested
 * @details PendSV is put at the lowest priority and SysTick at the second
 *          lowest, so user interrupts always preempt them; the PSP is primed
 *          with the "no thread to restore" marker, CONTROL switches to PSP with
 *          privileged mode, and a PendSV is forced before interrupts are
 *          unmasked, so the switch happens as soon as they are
 */
mini_os_err_t mini_os_schedule_start(void)
{
    MINI_OS_PENDSV_IRQ = 0xFF;  /* PendSV: lowest priority (never preempts user IRQs) */
    MINI_OS_SYSTICK_IRQ = 0xFE; /* SysTick: second-lowest */
    mini_os_psp_set(MINI_OS_NONE_THREAD_TO_RESTORE);
    mini_os_set_control(MINI_OS_CONTROL_REGISTER_PSP_PRIVILEGE);
    mini_os_yield_trigger();
    mini_os_irq_enable();
    return MINI_OS_OK;
}

/**
 * @brief Select the next thread to run and publish it as the current one
 * @return MINI_OS_OK on success; MINI_OS_ERR_NODEV when no thread is ready
 * @details
 *  - the outgoing thread is demoted RUNNING -> READY before the selection
 *  - same priority, still READY and still linked: round-robin to the list
 *    successor, skipping the sentinel at the tail
 *  - otherwise (preemption, or the current thread left the list): take the head
 *    of the selected priority list
 * @note mini_os_current_thread is the hand-off to the port, which restores SP
 *       from it; the selected thread is marked RUNNING here
 */
mini_os_err_t mini_os_schedule_switch(void)
{
    mini_os_uint8_t   old_priority;
    mini_os_uint8_t   next_priority;
    mini_os_list_t*   next_node;
    mini_os_thread_t* next_thread;

    old_priority = s_current_priority;
    if (mini_os_current_thread != MINI_OS_NULL)
    {
        if (mini_os_current_thread->state == MINI_OS_THREAD_STATE_RUNNING)
            mini_os_current_thread->state = MINI_OS_THREAD_STATE_READY;
    }
    next_priority = mini_os_get_highest_priority();
    if (next_priority >= (mini_os_uint8_t)MINI_OS_PRIORITY)
        return MINI_OS_ERR_NODEV;
    s_current_priority = next_priority;

    if (mini_os_current_thread != MINI_OS_NULL && next_priority == old_priority && mini_os_current_thread->state == MINI_OS_THREAD_STATE_READY && mini_os_current_thread->list_node.next != &mini_os_current_thread->list_node)
    {
        /* round-robin: successor, wrap past the sentinel at the tail */
        next_node = mini_os_current_thread->list_node.next;
        if (next_node == &g_ready_running_list[s_current_priority])
            next_node = next_node->next;
    }
    else
    {
        /* preemption, or the current thread is no longer runnable: take the head */
        next_node = g_ready_running_list[s_current_priority].next;
    }

    next_thread = mini_os_container_of(next_node, mini_os_thread_t, list_node);
    next_thread->state = MINI_OS_THREAD_STATE_RUNNING;
    mini_os_current_thread = next_thread; /* the port restores SP from this */
    return MINI_OS_OK;
}

/**
 * @brief Trigger a context switch from thread context
 * @return MINI_OS_OK always
 * @note the IRQ lock keeps the PendSV request inside the caller's critical
 *       section; the trailing barrier orders the ICSR write before the switch
 */
mini_os_err_t mini_os_schedule_yield(void)
{
    mini_os_irq_t irq_level = mini_os_irq_save();
    mini_os_yield_trigger();
    mini_os_irq_restore(irq_level);
    mini_os_barrier();
    return MINI_OS_OK;
}

/**
 * @brief Trigger a context switch from ISR context when a thread was outranked
 * @return MINI_OS_OK always
 * @details only a numerically lower (more urgent) priority is worth a PendSV:
 *          an equal or lower priority wake simply joins the ready list and is
 *          picked up by the next regular switch, so the interrupted thread
 *          resumes directly and the wake costs nothing
 * @note mini_os_yield_trigger() ends with dsb, which orders the ICSR write
 *       before the hardware exception return that tail-chains PendSV
 */
mini_os_err_t mini_os_schedule_yield_isr(void)
{
    mini_os_irq_t irq_level = mini_os_irq_save();

    /* only a more urgent thread is worth a PendSV */
    if (mini_os_current_thread != MINI_OS_NULL)
    {
        if (mini_os_get_highest_priority() < mini_os_current_thread->priority)
            mini_os_yield_trigger();
    }
    mini_os_irq_restore(irq_level);
    return MINI_OS_OK;
}

/**
 * @brief Add a thread to the tail of its priority ready/running list
 * @param[in] thread thread to add
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL,
 *         already READY/RUNNING, or has an out-of-range priority
 * @note sets the state to READY and the matching bit in the ready bitmap
 */
mini_os_err_t mini_os_add_thread_to_ready_running_list(mini_os_thread_t* thread)
{
    if (!thread || thread->state == MINI_OS_THREAD_STATE_READY || thread->state == MINI_OS_THREAD_STATE_RUNNING || thread->priority >= MINI_OS_PRIORITY)
        return MINI_OS_ERR_INVAL;
    mini_os_irq_t irq_level = mini_os_irq_save();
    thread->state = MINI_OS_THREAD_STATE_READY;
    mini_os_list_tail(&thread->list_node, &g_ready_running_list[thread->priority]);
    g_priority |= (1u << thread->priority);
    mini_os_irq_restore(irq_level);
    return MINI_OS_OK;
}

/**
 * @brief Remove a thread from its priority ready/running list
 * @param[in] thread thread to remove
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL,
 *         not READY/RUNNING, or has an out-of-range priority
 * @note clears the ready bitmap bit when the list becomes empty; the state is
 *       left untouched, so the caller decides what the thread becomes next
 */
mini_os_err_t mini_os_remove_thread_from_ready_running_list(mini_os_thread_t* thread)
{
    if (!thread || (thread->state != MINI_OS_THREAD_STATE_READY && thread->state != MINI_OS_THREAD_STATE_RUNNING) || thread->priority >= MINI_OS_PRIORITY)
        return MINI_OS_ERR_INVAL;

    mini_os_irq_t irq_level = mini_os_irq_save();

    mini_os_list_remove(&thread->list_node);

    if (mini_os_list_is_empty(&g_ready_running_list[thread->priority]))
        g_priority &= ~(1u << thread->priority);

    mini_os_irq_restore(irq_level);
    return MINI_OS_OK;
}

/**
 * @brief Unlink a wheel-parked thread from the thread time wheel
 * @param[in] thread thread to unlink (must be BLOCKED)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 *         or not BLOCKED
 * @note clears round and marks the slot as "not in the wheel"; the state stays
 *       BLOCKED, so the caller decides where the thread goes next
 */
mini_os_err_t mini_os_remove_thread_from_blocked_list(mini_os_thread_t* thread)
{
    if (!thread || thread->state != MINI_OS_THREAD_STATE_BLOCKED)
        return MINI_OS_ERR_INVAL;

    mini_os_irq_t irq_level = mini_os_irq_save();

    mini_os_list_remove(&thread->list_node);
    thread->round = 0;
    thread->wheel_slot = (mini_os_uint8_t)MINI_OS_TICK_WHEEL;

    mini_os_irq_restore(irq_level);
    return MINI_OS_OK;
}

/**
 * @brief Advance the thread time wheel by one slot and release expired threads
 * @details walks the slot that just came up: a thread with rounds left is
 *          decremented and stays parked, an expired one is unlinked and made
 *          ready. A thread that is also parked on a sync-object wait list is
 *          unlinked from there and flagged wait_done = MINI_OS_FALSE, so its
 *          mini_os_sync_wait_park() call reports MINI_OS_ERR_TIMEOUT
 * @note called with interrupts masked from mini_os_systick_handler(); the walk
 *       captures the next pointer first because list_remove re-links the node
 */
static void mini_os_tick_decrement(void)
{
    mini_os_list_t *  node, *next;
    mini_os_thread_t* thread;
    s_current_slot = (s_current_slot + 1) & MINI_OS_TICK_WHEEL_MASK; /* increment current slot */

    for (node = s_wheel[s_current_slot].next; node != &s_wheel[s_current_slot]; node = next)
    {
        next = node->next;
        thread = mini_os_container_of(node, mini_os_thread_t, list_node);
        if (thread->round > 0)
        {
            thread->round--;
            continue;
        }

        mini_os_list_remove(&thread->list_node);
        thread->wheel_slot = (mini_os_uint8_t)MINI_OS_TICK_WHEEL;
        if (thread->wait_list != MINI_OS_NULL)
        {
            /* timed-out sync wait: cancel it, the waiter reports the timeout */
            mini_os_list_remove(&thread->wait_node);
            thread->wait_list = MINI_OS_NULL;
            thread->wait_done = MINI_OS_FALSE;
        }
        mini_os_add_thread_to_ready_running_list(thread);
    }
}

/**
 * @brief Park a thread in the thread time wheel for a number of ticks
 * @param[in] thread thread to park (must not be linked anywhere)
 * @param[in] ticks delay length in ticks (> 0)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 *         or ticks is 0
 * @details slot = (current + ticks) mod wheel and round = whole revolutions
 *          still to wait; the state becomes BLOCKED and the thread is queued at
 *          the tail of that slot
 * @note caller must hold interrupts disabled
 */
mini_os_err_t mini_os_wheel_insert(mini_os_thread_t* thread, mini_os_uint32_t ticks)
{
    mini_os_uint32_t slot, round;

    if (thread == MINI_OS_NULL || ticks == 0u)
        return MINI_OS_ERR_INVAL;

    slot = (s_current_slot + ticks) & MINI_OS_TICK_WHEEL_MASK;
    round = (ticks - 1u) >> (MINI_OS_CTZ(MINI_OS_TICK_WHEEL));

    thread->round = round;
    thread->wheel_slot = (mini_os_uint8_t)slot;
    thread->state = MINI_OS_THREAD_STATE_BLOCKED;
    mini_os_list_tail(&thread->list_node, &s_wheel[slot]);
    return MINI_OS_OK;
}

/**
 * @brief Remaining ticks of a wheel-parked thread
 * @param[in] thread thread to query
 * @return remaining ticks (slot distance plus whole revolutions); 0 when the
 *         thread is not parked in the wheel
 * @note a zero slot distance means a whole wheel is left, reported as
 *       MINI_OS_TICK_WHEEL to stay consistent with the round formula
 */
mini_os_uint32_t mini_os_wheel_remain(mini_os_thread_t* thread)
{
    mini_os_uint32_t remain;

    if (thread == MINI_OS_NULL || thread->wheel_slot >= MINI_OS_TICK_WHEEL)
        return 0u; /* not parked in the wheel (e.g. waiting on a sync object) */
    remain = ((mini_os_uint32_t)thread->wheel_slot - s_current_slot) & MINI_OS_TICK_WHEEL_MASK;
    if (remain == 0u)
        remain = MINI_OS_TICK_WHEEL; /* whole-wheel boundary (matches the -1 round formula) */
    return remain + thread->round * MINI_OS_TICK_WHEEL;
}

/**
 * @brief Delay the current thread for a number of ticks
 * @param[in] ticks delay length in ticks; 0 returns immediately
 * @details leaves the ready/running list, parks the thread in the wheel and
 *          yields, so it resumes when the delay expires
 * @note kernel API behind mini_os_thread_delay_tick(); no-op outside thread
 *       context, where there is nothing to park
 */
void mini_os_schedule_delay(mini_os_uint32_t ticks)
{
    mini_os_irq_t irq_level;

    if (mini_os_current_thread == MINI_OS_NULL || ticks == 0u)
        return;
    irq_level = mini_os_irq_save();

    mini_os_remove_thread_from_ready_running_list(mini_os_current_thread);
    mini_os_wheel_insert(mini_os_current_thread, ticks);

    mini_os_irq_restore(irq_level);
    mini_os_schedule_yield();
}

/**
 * @brief Park the current thread on a sync-object wait list with a timeout
 * @param[in] wait_list wait list of the sync object (queue/semaphore/event...)
 * @param[in] wait_mask expected event mask stored on the parked thread (event
 *            groups evaluate it on wake; pass 0 for objects without a mask)
 * @param[in] timeout_tick (mini_os_tick_t)-1 = wait forever, otherwise park in
 *            the time wheel for this many ticks
 * @param[in] irq_level IRQ level saved by the caller with mini_os_irq_save()
 * @return MINI_OS_OK when woken by an event; MINI_OS_ERR_TIMEOUT when the wheel
 *         timeout expired first; MINI_OS_ERR_INVAL on invalid arguments
 * @details the park happens inside the caller's critical section, so the
 *          condition check done by the caller and the park are atomic and no
 *          event can slip through. The thread is queued through wait_node and,
 *          for a finite timeout, also in the wheel through list_node (free,
 *          because the thread just left the ready list); the wake side unlinks
 *          both, so there is no retry loop and no deadline recomputation
 * @note consumes the caller's critical section (restores irq_level itself) and
 *       yields; back only after an event wake or a wheel timeout unlinked it
 */
mini_os_err_t mini_os_sync_wait_park(mini_os_list_t* wait_list, mini_os_uint32_t wait_mask, mini_os_tick_t timeout_tick, mini_os_irq_t irq_level)
{
    mini_os_thread_t* current;
    mini_os_bool_t    done;
    mini_os_irq_t     irq;

    if (wait_list == MINI_OS_NULL || timeout_tick == 0 || mini_os_current_thread == MINI_OS_NULL)
    {
        mini_os_irq_restore(irq_level);
        return MINI_OS_ERR_INVAL;
    }

    /* park atomically with the caller's condition check */
    current = mini_os_current_thread;
    (void)mini_os_remove_thread_from_ready_running_list(current);
    current->state = MINI_OS_THREAD_STATE_BLOCKED;
    current->wait_list = wait_list;
#if MINI_OS_EVENT
    current->wait_mask = wait_mask;
#endif
    current->wait_done = MINI_OS_FALSE;
    mini_os_list_tail(&current->wait_node, wait_list);
    if (timeout_tick != (mini_os_tick_t)-1)
    {
        /* separate node: the thread just left the ready list */
        (void)mini_os_wheel_insert(current, (mini_os_uint32_t)timeout_tick);
    }
    mini_os_irq_restore(irq_level);
    (void)mini_os_schedule_yield();

    /* back only after an event wake or a wheel timeout unlinked us */
    irq = mini_os_irq_save();
    done = current->wait_done;
    current->wait_done = MINI_OS_TRUE; /* consume the result */
    mini_os_irq_restore(irq);
    return (done != MINI_OS_FALSE) ? MINI_OS_OK : MINI_OS_ERR_TIMEOUT;
}

#if MINI_OS_TIME_SLICE
/**
 * @brief Decrement the running thread's time slice; rotate when it expires
 * @note
 *  - remain_tick is the thread's own remaining quantum: it is decremented
 *    only while the thread is running and preserved across preemption/block
 *  - a thread without a configured slice (init_tick_num == 0) runs until it
 *    blocks or is preempted
 *  - on natural expiry the quantum is refilled for the thread's next run
 *    and the PendSV is triggered to rotate to the next same-priority thread
 */
static void mini_os_tick_slice_decrement(void)
{
    mini_os_thread_t* current_thread = mini_os_current_thread;

    if (current_thread == MINI_OS_NULL || current_thread->init_tick_num == 0)
        return;
    if (current_thread->remain_tick > 0)
        current_thread->remain_tick--;
    if (current_thread->remain_tick == 0)
    {
        current_thread->remain_tick = current_thread->init_tick_num; /* refill for the next run */
        mini_os_schedule_yield();
    }
}
#endif

/**
 * @brief SysTick handler: advance every wheel and the timer module by one tick
 * @details with interrupts masked it runs the thread time wheel, the
 *          round-robin slice (when MINI_OS_TIME_SLICE is on), the global tick
 *          counter and then the timer wheel through mini_os_timer_tick()
 * @note installed in the vector table; the overflow counter exists only with
 *       MINI_OS_LONG_TIME
 */
void mini_os_systick_handler(void)
{
    mini_os_irq_t irq_level = mini_os_irq_save();
    mini_os_tick_decrement();
#if MINI_OS_TIME_SLICE
    mini_os_tick_slice_decrement();
#endif
    g_global_tick++;
#if MINI_OS_LONG_TIME
    if (g_global_tick == 0u) /* wrapped around: count the overflow */
        g_global_tick_overflow++;
#endif
    mini_os_timer_tick(); /* advance the timer wheel, run/queue expired timers */
    mini_os_irq_restore(irq_level);
}

#if MINI_OS_LONG_TIME
/**
 * @brief Read the 64-bit tick counter (tick value plus wrap count)
 * @param[out] tick receives the low 32 bits (g_global_tick)
 * @param[out] overflow receives the number of 32-bit wrap-arounds
 * @return MINI_OS_OK always
 * @note only compiled with MINI_OS_LONG_TIME
 */
mini_os_err_t mini_os_get_tick_long_time(mini_os_uint32_t* tick, mini_os_uint32_t* overflow)
{
    *tick = g_global_tick;
    *overflow = g_global_tick_overflow;
    return MINI_OS_OK;
}
#endif

/**
 * @brief Read the current OS tick counter
 * @param[out] tick receives the tick count
 * @return MINI_OS_OK always
 */
mini_os_err_t mini_os_get_tick(mini_os_tick_t* tick)
{
    *tick = g_global_tick;
    return MINI_OS_OK;
}

/**
 * @brief Remaining ticks until an absolute deadline (tick-wrap safe)
 * @param[in] deadline absolute tick value, captured at entry as now + timeout
 * @return ticks left; 0 when the deadline has been reached or passed
 * @note retry loops re-park with this value to keep the total timeout strict
 */
mini_os_uint32_t mini_os_tick_until(mini_os_uint32_t deadline)
{
    mini_os_uint32_t now = g_global_tick; /* aligned 32-bit read is atomic */

    if ((mini_os_int32_t)(deadline - now) <= 0)
        return 0u; /* reached or passed */
    return deadline - now;
}

/**
 * @brief Configure and start the SysTick timer
 * @param[in] ticks_per_ms ticks per millisecond; 0 selects the default rate
 *            (MINI_OS_DEFAULT_SYSTICK)
 * @note weak default: override it when the board needs another clock source
 */
MINI_OS_WEAK void mini_os_systick_init(uint32_t ticks_per_ms)
{
    uint32_t reload;

    if (ticks_per_ms == 0u)
        ticks_per_ms = 1000u / MINI_OS_DEFAULT_SYSTICK; /* 0 -> default tick rate */

    reload = (MINI_OS_CPU_CLOCK_HZ / 1000u) * ticks_per_ms; /* cycles per tick */

    MINI_OS_SYSTICK_RELOAD = reload - 1u;
    MINI_OS_SYSTICK_VAL = 0u;
    MINI_OS_SYSTICK_CTRL = MINI_OS_SYSTICK_CTRL_CLKSOURCE | MINI_OS_SYSTICK_CTRL_TICKINT | MINI_OS_SYSTICK_CTRL_ENABLE;
}
