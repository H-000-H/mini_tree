/**
 * @file schedule.c
 * @author H-000-H
 * @brief Scheduler implementation
 * @details Round-robin selection inside one priority level, with threads A, B
 *          and C sharing a priority:
 *  - current is B (middle): sentinel -> A -> B(current) -> C -> sentinel,
 *    current->next is C (not the sentinel), so C is chosen
 *  - current is C (tail): sentinel -> A -> B -> C(current) -> sentinel,
 *    current->next is the sentinel, so the list head (A) is chosen (wraps)
 *  - single thread: sentinel -> A(current) -> sentinel, the successor is the
 *    sentinel, so the head is A again and the thread keeps running
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include "schedule.h"

#include "err.h"
#include "list.h"
#include "mini_config.h"
#include "port.h"
#include "redef.h"
#include "semaphore.h"
#include "thread.h"
#include "timer.h"

/** @brief Ready bitmap: bit i set = priority i has a ready or running thread*/
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

#if MINI_OS_THREAD_DEADLINE
/** @brief Global ready list of MINI_OS_SCHED_DEADLINE threads, earliest deadline first
 * @note the DL class sits above every priority level, so it has its own list and is
 *       deliberately NOT reflected in the g_priority bitmap; see mini_os_schedule_switch() */
static mini_os_list_t s_dl_ready_list;

/**
 * @brief Does a thread belong to the DL ready list?
 * @param[in] thread thread to test
 * @return MINI_OS_TRUE for a real DL task, and for any thread that deadline
 *         inheritance promoted into the DL class (dl_deadline_inherit != 0)
 * @details the mutex inheritance problem this solves: PI can only raise a
 *          blocker's *priority*, but the DL class outranks every priority level,
 *          so a boosted plain thread would still be preempted by any ready DL
 *          task and the DL waiter's deadline could be missed. A holder that
 *          inherits a deadline is therefore scheduled inside the DL class at
 *          that deadline, ahead of DL tasks with a later one. A promoted thread
 *          has no CBS reservation: it must never be throttled, and its zero
 *          period must stay out of the bandwidth arithmetic and the job refill
 * @note caller must hold interrupts disabled
 */
MINI_OS_STATIC_INLINE mini_os_bool_t mini_os_dl_scheduled(const mini_os_thread_t* thread)
{
    return (mini_os_bool_t)(thread->sched_policy == MINI_OS_SCHED_DEADLINE || thread->dl_deadline_inherit != 0);
}
#endif /* MINI_OS_THREAD_DEADLINE */

/** @brief Slot of the thread time wheel serviced on the next tick */
static mini_os_uint32_t s_current_slot = 0;

#if MINI_OS_THREAD_DEADLINE
/** @brief DL bandwidth scale: runtime/period is accounted in 1/2^20 CPU units */
#define MINI_OS_DL_BW_SCALE (1u << 20)
/** @brief Maximum total DL bandwidth (95%) */
#define MINI_OS_DL_BW_MAX ((mini_os_uint64_t)(95u) * MINI_OS_DL_BW_SCALE / 100u)

/** @brief this_bw: bandwidth reserved by every admitted DL task (the admission sum) */
static mini_os_uint64_t s_dl_this_bw = 0u;
/** @brief running_bw: bandwidth of the DL tasks currently contending (linked in
 *  s_dl_ready_list); extra_bw = this_bw - running_bw is the reclaimable pool */
static mini_os_uint64_t s_dl_running_bw = 0u;

/**
 * @brief Bandwidth of one DL task, in MINI_OS_DL_BW_SCALE units
 */
static mini_os_uint64_t mini_os_dl_bw_of(mini_os_tick_t runtime, mini_os_tick_t period)
{
    return ((mini_os_uint64_t)runtime * (mini_os_uint64_t)MINI_OS_DL_BW_SCALE) / (mini_os_uint64_t)period;
}

/**
 * @brief Admission control: reserve runtime/period for a new DL task
 * @return MINI_OS_OK when the total stays within MINI_OS_DL_BW_MAX;
 *         MINI_OS_ERR_BUSY when admitting it would oversubscribe the CPU
 * @note this is the DL "tax": this_bw only grows here and shrinks in
 *       mini_os_dl_bandwidth_release(); blocking never changes it
 */
mini_os_err_t mini_os_dl_bandwidth_reserve(mini_os_tick_t runtime, mini_os_tick_t period)
{
    mini_os_uint64_t add = mini_os_dl_bw_of(runtime, period);

    if (s_dl_this_bw + add > (mini_os_uint64_t)MINI_OS_DL_BW_MAX)
        return MINI_OS_ERR_BUSY;
    s_dl_this_bw += add;
    return MINI_OS_OK;
}

/**
 * @brief Release the reservation of a DL task that is going away
 */
void mini_os_dl_bandwidth_release(mini_os_tick_t runtime, mini_os_tick_t period)
{
    mini_os_uint64_t sub = mini_os_dl_bw_of(runtime, period);

    s_dl_this_bw = (s_dl_this_bw >= sub) ? (s_dl_this_bw - sub) : 0u;
}

/** @brief Registered DL deadline-overrun callback (MINI_OS_NULL = none) */
static mini_os_dl_miss_hook_t s_dl_miss_hook = MINI_OS_NULL;
/** @brief Opaque value forwarded to s_dl_miss_hook */
static void* s_dl_miss_param = MINI_OS_NULL;

/**
 * @brief Register or clear the DL deadline-overrun callback
 */
mini_os_err_t mini_os_dl_miss_hook_set(mini_os_dl_miss_hook_t hook, void* param)
{
    s_dl_miss_hook  = hook;
    s_dl_miss_param = param;
    return MINI_OS_OK;
}

/**
 * @brief Record a missed deadline and notify the registered callback
 * @param[in] thread DL thread whose absolute deadline was reached while it was
 *            still runnable or throttled (it never blocked to signal completion)
 * @note runs with interrupts masked inside the SysTick handler; the deadline is
 *       reported here before it is advanced to the next period
 */
static void mini_os_dl_report_miss(mini_os_thread_t* thread)
{
    thread->dl_miss_count++;
    if (s_dl_miss_hook != MINI_OS_NULL)
        s_dl_miss_hook(thread, thread->dl_deadline_time, s_dl_miss_param);
}

/** @brief Registered DL job-completion callback (MINI_OS_NULL = none) */
static mini_os_dl_finish_hook_t s_dl_finish_hook = MINI_OS_NULL;
/** @brief Opaque value forwarded to s_dl_finish_hook */
static void* s_dl_finish_param = MINI_OS_NULL;

/**
 * @brief Register or clear the DL job-completion callback
 */
mini_os_err_t mini_os_dl_finish_hook_set(mini_os_dl_finish_hook_t hook, void* param)
{
    s_dl_finish_hook  = hook;
    s_dl_finish_param = param;
    return MINI_OS_OK;
}

/**
 * @brief Record a finished job and notify the registered callback
 * @param[in] thread DL thread that finished its job
 * @note runs in thread context from mini_os_deadline_job_finish(), unlike the miss
 *       hook which runs in the SysTick handler
 */
static void mini_os_dl_report_finish(mini_os_thread_t* thread)
{
    thread->dl_finish_count++;
    if (s_dl_finish_hook != MINI_OS_NULL)
        s_dl_finish_hook(thread, thread->dl_deadline_time, s_dl_finish_param);
}

/** @brief Start a new job (refill budget, advance deadline); defined with the CBS helpers */
static void mini_os_dl_new_job(mini_os_thread_t* thread, mini_os_uint32_t now);
/** @brief Insert a DL thread into the global ready list in EDF order; defined with the CBS helpers */
static void mini_os_dl_list_insert(mini_os_thread_t* thread);
#endif /* MINI_OS_THREAD_DEADLINE */

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
#if MINI_OS_THREAD_DEADLINE
    mini_os_list_init(&s_dl_ready_list);
    s_dl_this_bw    = 0u;
    s_dl_running_bw = 0u;
#endif
    s_current_slot = 0;
    return MINI_OS_OK;
}

/**
 * @brief Start the scheduler (performs the very first context switch)
 * @return MINI_OS_OK once the first switch has been requested
 * @details PendSV is put at the lowest priority and SysTick at the second
 *          lowest, so user interrupts always preempt them; the PSP is primed
 *          with the "no thread to restore" marker, and a PendSV is forced
 *          before interrupts are unmasked, so the switch happens as soon as
 *          they are
 * @note CONTROL is deliberately left alone here: the marker leaves the PSP at
 *       0, so switching SPSEL first would point the very first exception's
 *       hardware stacking at 0xFFFFFFE0. The move to the PSP is declared by
 *       the PendSV exception return instead (see pendsv_handler), and the
 *       hardware restores SPSEL when that return is taken
 */
mini_os_err_t mini_os_schedule_start(void)
{
    MINI_OS_PENDSV_IRQ = 0xFF;  /* PendSV: lowest priority (never preempts user IRQs) */
    MINI_OS_SYSTICK_IRQ = 0xFE; /* SysTick: second-lowest */
    mini_os_psp_set(MINI_OS_NONE_THREAD_TO_RESTORE);
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
    mini_os_list_t*   next_node;
    mini_os_thread_t* next_thread;

    if (mini_os_current_thread != MINI_OS_NULL)
    {
        if (mini_os_current_thread->state == MINI_OS_THREAD_STATE_RUNNING)
            mini_os_current_thread->state = MINI_OS_THREAD_STATE_READY;
    }

#if MINI_OS_THREAD_DEADLINE
    /* DL class first: it outranks every priority level, so the earliest absolute
     * deadline among all ready DL threads wins regardless of priority */
    if (mini_os_list_is_empty(&s_dl_ready_list) == MINI_OS_FALSE)
    {
        next_node = s_dl_ready_list.next;
    }
    else
#endif
    {
        mini_os_uint8_t old_priority  = s_current_priority;
        mini_os_uint8_t next_priority = mini_os_get_highest_priority();

        if (next_priority >= (mini_os_uint8_t)MINI_OS_PRIORITY)
            return MINI_OS_ERR_NODEV;
        s_current_priority = next_priority;

        /* head of the level: preemption, or the current thread is no longer runnable */
        next_node = g_ready_running_list[next_priority].next;

#if MINI_OS_THREAD_EDF
        /* same-level EDF: an EDF head always keeps running, so the round-robin
         * rotation is skipped for it (ordinary threads still rotate among peers) */
        if (mini_os_container_of(next_node, mini_os_thread_t, list_node)->sched_policy == MINI_OS_SCHED_NORMAL)
#endif
        {
            if (mini_os_current_thread != MINI_OS_NULL && next_priority == old_priority && mini_os_current_thread->state == MINI_OS_THREAD_STATE_READY &&
                mini_os_current_thread->list_node.next != &mini_os_current_thread->list_node)
            {
                /* round-robin: successor, wrap past the sentinel at the tail */
                next_node = mini_os_current_thread->list_node.next;
                if (next_node == &g_ready_running_list[next_priority])
                    next_node = next_node->next;
            }
        }
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

    /* only a thread that outranks the interrupted one is worth a PendSV */
    if (mini_os_current_thread != MINI_OS_NULL)
    {
        mini_os_bool_t preempt = MINI_OS_FALSE;

#if MINI_OS_THREAD_DEADLINE
        if (mini_os_list_is_empty(&s_dl_ready_list) == MINI_OS_FALSE)
        {
            /* a DL thread is ready: it outranks everything, unless the current
             * thread already *is* the earliest-deadline DL thread */
            if (s_dl_ready_list.next != &mini_os_current_thread->list_node)
                preempt = MINI_OS_TRUE;
        }
        else
#endif
        {
            mini_os_uint8_t highest = mini_os_get_highest_priority();

            if (highest < mini_os_current_thread->priority)
                preempt = MINI_OS_TRUE;
#if MINI_OS_THREAD_EDF
            else if (highest == mini_os_current_thread->priority && highest < (mini_os_uint8_t)MINI_OS_PRIORITY)
            {
                /* same level, EDF: a woken EDF thread that now heads the list has
                 * an earlier deadline, so it outranks the running thread even
                 * though the priority number is equal */
                mini_os_list_t* head = g_ready_running_list[highest].next;

                if (head != &g_ready_running_list[highest] && head != &mini_os_current_thread->list_node &&
                    mini_os_container_of(head, mini_os_thread_t, list_node)->sched_policy == MINI_OS_SCHED_EDF)
                    preempt = MINI_OS_TRUE;
            }
#endif
        }
        if (preempt == MINI_OS_TRUE)
            mini_os_yield_trigger();
    }
    mini_os_irq_restore(irq_level);
    return MINI_OS_OK;
}

/**
 * @brief Add a thread to the ready/running structure it belongs to
 * @param[in] thread thread to add
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL,
 *         already READY/RUNNING, or has an out-of-range priority
 * @note a DL task, and any thread promoted into the DL class by deadline
 *       inheritance (mini_os_dl_scheduled()), joins the DL global list; every
 *       other thread joins the tail of its priority level (EDF threads are
 *       ordered inside the level by deadline). Sets the state to READY and, for
 *       the priority lists, the matching bit in the ready bitmap
 */
mini_os_err_t mini_os_add_thread_to_ready_running_list(mini_os_thread_t* thread)
{
    if (!thread || thread->state == MINI_OS_THREAD_STATE_READY || thread->state == MINI_OS_THREAD_STATE_RUNNING || thread->priority >= MINI_OS_PRIORITY)
        return MINI_OS_ERR_INVAL;
    mini_os_irq_t irq_level = mini_os_irq_save();
    thread->state = MINI_OS_THREAD_STATE_READY;
#if MINI_OS_THREAD_DEADLINE
    if (mini_os_dl_scheduled(thread) != MINI_OS_FALSE)
    {
        /* DL class: own global list, earliest effective deadline first. A thread
         * promoted by deadline inheritance is ordered by its inherited deadline;
         * the g_priority bitmap is left alone because this is not a priority
         * level. Only a real DL task runs CBS: a promoted holder must not be
         * throttled and has no bandwidth to account (its period is 0) */
        if (thread->sched_policy == MINI_OS_SCHED_DEADLINE)
        {
            /* non-deferrable CBS: the blocking time counts against the deadline,
             * so a thread that wakes after its absolute deadline starts a new job
             * (deadline advanced by whole periods, budget refilled) instead of
             * resuming with an expired deadline */
            if ((mini_os_int32_t)((mini_os_tick_t)g_global_tick - thread->dl_deadline_time) >= 0)
                mini_os_dl_new_job(thread, g_global_tick);
        }
        mini_os_dl_list_insert(thread);
        if (thread->sched_policy == MINI_OS_SCHED_DEADLINE)
            s_dl_running_bw += mini_os_dl_bw_of(thread->dl_run_time, (mini_os_tick_t)thread->dl_period); /* contending again: bandwidth leaves the reclaimable pool */
    }
    else
#endif /* MINI_OS_THREAD_DEADLINE */
#if MINI_OS_THREAD_EDF
    if (thread->sched_policy == MINI_OS_SCHED_EDF)
    {
        /* same priority, EDF: ahead of every ordinary thread, and ahead of the
         * first EDF thread with a later deadline (equal deadlines stay FIFO) */
        mini_os_list_t* head = &g_ready_running_list[thread->priority];
        mini_os_list_t* node;

        for (node = head->next; node != head; node = node->next)
        {
            mini_os_thread_t* ready = mini_os_container_of(node, mini_os_thread_t, list_node);

            if (ready->sched_policy == MINI_OS_SCHED_NORMAL)
                break; /* ordinary threads queue behind all EDF threads */
            if (mini_os_deadline_before(mini_os_thread_effective_deadline(thread), mini_os_thread_effective_deadline(ready)) == MINI_OS_TRUE)
                break; /* first later deadline: insert before it */
        }
        (void)mini_os_list_add(node, node->prev, &thread->list_node);
        g_priority |= (1u << thread->priority);
    }
    else
#endif /* MINI_OS_THREAD_EDF */
    {
        (void)mini_os_list_tail(&thread->list_node, &g_ready_running_list[thread->priority]);
        g_priority |= (1u << thread->priority);
    }
    mini_os_irq_restore(irq_level);
    return MINI_OS_OK;
}

/**
 * @brief Remove a thread from the ready/running structure it belongs to
 * @param[in] thread thread to remove
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL,
 *         not READY/RUNNING, or has an out-of-range priority
 * @note unlinks from the DL global list when the thread is DL-scheduled
 *       (mini_os_dl_scheduled()), otherwise from its priority level (clearing
 *       the ready bitmap bit when the level becomes empty); the state is left
 *       untouched, so the caller decides what the thread becomes next
 */
mini_os_err_t mini_os_remove_thread_from_ready_running_list(mini_os_thread_t* thread)
{
    if (!thread || (thread->state != MINI_OS_THREAD_STATE_READY && thread->state != MINI_OS_THREAD_STATE_RUNNING) || thread->priority >= MINI_OS_PRIORITY)
        return MINI_OS_ERR_INVAL;

    mini_os_irq_t irq_level = mini_os_irq_save();

    mini_os_list_remove(&thread->list_node);

#if MINI_OS_THREAD_DEADLINE
    if (mini_os_dl_scheduled(thread) != MINI_OS_FALSE)
    {
        if (thread->sched_policy == MINI_OS_SCHED_DEADLINE)
        {
            mini_os_uint64_t bw = mini_os_dl_bw_of(thread->dl_run_time, (mini_os_tick_t)thread->dl_period);

            /* leaving the contending set hands its bandwidth back to the reclaimable pool */
            s_dl_running_bw = (s_dl_running_bw >= bw) ? (s_dl_running_bw - bw) : 0u;
        }
        mini_os_irq_restore(irq_level); /* DL list is not tracked in the priority bitmap */
        return MINI_OS_OK;
    }
#endif
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

#if MINI_OS_THREAD_DEADLINE
/**
 * @brief Insert a thread into the DL global ready list in earliest-deadline order
 * @note the list holds real DL tasks and every thread promoted into the DL class
 *       by deadline inheritance (mini_os_dl_scheduled()); ordering uses the
 *       effective deadline, i.e. a deadline inherited through a held mutex
 *       outranks the thread's own one (see mini_os_thread_effective_deadline).
 *       mini_os_dl_replenish_due() must scan the whole list and filter on the
 *       policy because the order no longer follows the own deadline
 * @note caller must hold interrupts disabled; the node must not be linked
 */
static void mini_os_dl_list_insert(mini_os_thread_t* thread)
{
    mini_os_list_t* node;

    for (node = s_dl_ready_list.next; node != &s_dl_ready_list; node = node->next)
    {
        mini_os_thread_t* ready = mini_os_container_of(node, mini_os_thread_t, list_node);
        if (mini_os_deadline_before(mini_os_thread_effective_deadline(thread), mini_os_thread_effective_deadline(ready)) == MINI_OS_TRUE)
            break; /* first later deadline: insert before it (equal deadlines stay FIFO) */
    }
    (void)mini_os_list_add(node, node->prev, &thread->list_node);
}

/**
 * @brief Start a new job: refill the budget and move the deadline one period on
 * @param[in] thread DL thread whose period boundary was reached
 * @param[in] now current global tick
 * @note advances dl_deadline_time by whole periods until it is in the future (a
 *       late boundary never leaves the deadline in the past), then restores the
 *       full budget. Called from the time wheel (throttle expiry), from the
 *       period-boundary walk and from the wake path.
 */
static void mini_os_dl_new_job(mini_os_thread_t* thread, mini_os_uint32_t now)
{
    thread->dl_budget      = thread->dl_run_time; /* the new job starts with a full budget */
    thread->dl_budget_frac = 0u;
    do
    {
        thread->dl_deadline_time += (mini_os_tick_t)thread->dl_period;
    } while ((mini_os_int32_t)(thread->dl_deadline_time - (mini_os_tick_t)now) <= 0);
    thread->dl_throttled = MINI_OS_FALSE;
    thread->dl_job_done  = MINI_OS_FALSE;
}

/**
 * @brief Start a new job for every contending DL thread whose deadline was reached
 * @details the whole ready list is scanned: it is ordered by the effective
 *          deadline, so a thread boosted by an inherited deadline may sit ahead
 *          of a thread whose own deadline is due, and the due test cannot stop at
 *          the head. Each due thread is refilled (its own deadline advances by
 *          whole periods, the budget is restored) and re-inserted. Reclaiming
 *          means a task need not have spent its budget when the deadline arrives,
 *          so this walk is what still guarantees a fresh budget every period.
 * @note running_bw is untouched: the thread stays contending, only its own
 *       deadline and budget change
 */
static void mini_os_dl_replenish_due(void)
{
    mini_os_uint32_t now = g_global_tick;
    mini_os_list_t*  node;
    mini_os_list_t*  next;

    for (node = s_dl_ready_list.next; node != &s_dl_ready_list; node = next)
    {
        mini_os_thread_t* thread = mini_os_container_of(node, mini_os_thread_t, list_node);

        next = node->next; /* captured first: the re-insert below re-links nodes */
        if (thread->sched_policy != MINI_OS_SCHED_DEADLINE)
            continue; /* promoted holder: no CBS job, its own deadline is not a boundary */
        if ((mini_os_int32_t)((mini_os_tick_t)now - thread->dl_deadline_time) < 0)
            continue; /* own deadline still ahead: the job is not due yet */
        mini_os_list_remove(&thread->list_node);
        if (thread->dl_job_done == MINI_OS_FALSE)
            mini_os_dl_report_miss(thread); /* still runnable at its deadline: the job overran */
        mini_os_dl_new_job(thread, now);
        mini_os_dl_list_insert(thread);
    }
}

/**
 * @brief Throttle a DL thread that spent its budget until its next period
 * @param[in] thread DL thread currently READY/RUNNING with dl_budget == 0
 * @details unlinks the thread (handing its bandwidth back to the reclaimable pool)
 *          and parks it in the thread time wheel for the ticks left until its
 *          absolute deadline; mini_os_tick_decrement() refills the budget and
 *          re-readies it there. This is the CBS "run at most dl_run_time per
 *          dl_period" guarantee.
 * @note runs from the SysTick handler with interrupts masked; the PendSV is
 *       requested here because the throttled thread must leave the CPU now
 */
static void mini_os_dl_throttle(mini_os_thread_t* thread)
{
    mini_os_uint32_t remain;

    (void)mini_os_remove_thread_from_ready_running_list(thread);
    thread->dl_throttled = MINI_OS_TRUE;
    thread->dl_throttle_count++; /* statistics: one more budget exhaustion */
    remain = mini_os_tick_until((mini_os_tick_t)thread->dl_deadline_time);
    if (remain == 0u)
        remain = 1u; /* deadline already reached: replenish on the next tick */
    (void)mini_os_wheel_insert(thread, remain);
    mini_os_yield_trigger(); /* the thread is no longer runnable: switch away now */
}

/**
 * @brief Charge one tick to the running DL thread and throttle it when spent
 * @param[in] current running thread (may be MINI_OS_NULL)
 * @details GRUB-style reclaiming: the budget is spent at rate running_bw/this_bw,
 *          so while other DL tasks are blocked (non-contending) the running task
 *          spends its budget more slowly and uses their share, up to the admitted
 *          this_bw. The fraction is accumulated in dl_budget_frac so a rate below
 *          1 does not lose ticks to rounding.
 * @note called once per tick from mini_os_systick_handler() after the global tick
 *       advanced, so the throttle arithmetic sees the new "now"
 */
static void mini_os_dl_tick_decrement(mini_os_thread_t* current)
{
    mini_os_uint64_t delta;

    if (s_dl_this_bw == 0u)
        return;
    if (current == MINI_OS_NULL || current->sched_policy != MINI_OS_SCHED_DEADLINE || current->dl_throttled == MINI_OS_TRUE)
        return;

    /* budget spent by this tick, in 1/MINI_OS_DL_BW_SCALE of a tick */
    delta = (s_dl_running_bw * (mini_os_uint64_t)MINI_OS_DL_BW_SCALE) / s_dl_this_bw;
    if (delta == 0u)
        delta = 1u; /* never let a tiny bandwidth stall the accounting */

    current->dl_budget_frac = (mini_os_uint32_t)(current->dl_budget_frac + (mini_os_uint32_t)delta);
    while (current->dl_budget_frac >= (mini_os_uint32_t)MINI_OS_DL_BW_SCALE)
    {
        current->dl_budget_frac = (mini_os_uint32_t)(current->dl_budget_frac - (mini_os_uint32_t)MINI_OS_DL_BW_SCALE);
        if (current->dl_budget > 0)
            current->dl_budget--;
    }
    if (current->dl_budget == 0)
        mini_os_dl_throttle(current);
}

/**
 * @brief Finish the current DL job and wait for the next period
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when the caller is not a DL thread
 * @details Called by a DL thread when its job is done. The completion is recorded
 *          (dl_finish_count plus the finish hook) and the job is marked done, so the
 *          period boundary will not report it as an overrun; a call that is already
 *          past the absolute deadline reports a miss instead. The thread then parks
 *          until its absolute deadline, where the wake path starts a new job (budget
 *          refilled, dl_deadline_time += dl_period), so this call returns inside the
 *          next period and the caller never computes delays or tracks the period.
 * @note thread context only; the finish and (late) miss hooks run here, whereas the
 *       boundary-driven miss hook runs in the SysTick handler
 */
mini_os_err_t mini_os_deadline_job_finish(void)
{
    mini_os_thread_t* current = mini_os_current_thread;
    mini_os_uint32_t  remain;
    mini_os_bool_t    late;

    if (current == MINI_OS_NULL || current->sched_policy != MINI_OS_SCHED_DEADLINE)
        return MINI_OS_ERR_INVAL;

    late = (mini_os_tick_until((mini_os_tick_t)current->dl_deadline_time) == 0u) ? MINI_OS_TRUE : MINI_OS_FALSE;

    mini_os_dl_report_finish(current); /* the job is done: hook + dl_finish_count */
    if (late == MINI_OS_TRUE)
        mini_os_dl_report_miss(current); /* ... but only after its deadline */
    current->dl_job_done = MINI_OS_TRUE; /* the boundary must not report it a second time */

    /* block on the per-task activation semaphore: the thread time wheel releases
     * this wait at the absolute deadline (the periodic activation), and an early
     * mini_os_semaphore_give() starts the next job immediately. Either way the
     * wake path has started the new job before take() returns. */
    remain = mini_os_tick_until((mini_os_tick_t)current->dl_deadline_time);
    if (remain == 0u)
        remain = 1u; /* deadline already reached: let the next tick start the new job */
    if (current->dl_activation != MINI_OS_NULL)
        (void)mini_os_semaphore_take(current->dl_activation, (mini_os_tick_t)remain);
    else
        mini_os_schedule_delay(remain); /* defensive: no semaphore created */
    return MINI_OS_OK;
}
#endif /* MINI_OS_THREAD_DEADLINE */

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
#if MINI_OS_THREAD_DEADLINE
        if (thread->sched_policy == MINI_OS_SCHED_DEADLINE && thread->dl_throttled == MINI_OS_TRUE)
        {
            if (thread->dl_job_done == MINI_OS_FALSE)
                mini_os_dl_report_miss(thread);        /* throttled when the deadline arrived: overrun */
            mini_os_dl_new_job(thread, g_global_tick); /* CBS: the period boundary refills the budget */
        }
#endif
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
#if MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE
    if (current_thread->sched_policy != MINI_OS_SCHED_NORMAL)
        return; /* EDF / DL threads are deadline-ordered: no round-robin expiry */
#endif
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
#if MINI_OS_THREAD_DEADLINE
    mini_os_dl_replenish_due();                        /* period boundaries: start the new jobs */
    mini_os_dl_tick_decrement(mini_os_current_thread); /* GRUB: spend running_bw/this_bw of the budget */
#endif
#if MINI_OS_TIMER
    mini_os_timer_tick(); /* advance the timer wheel, run/queue expired timers */
#endif
    mini_os_irq_restore(irq_level);

    (void)mini_os_schedule_yield_isr();
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
MINI_OS_WEAK void mini_os_systick_init(mini_os_uint32_t ticks_per_ms)
{
    mini_os_uint32_t reload;

    if (ticks_per_ms == 0u)
        ticks_per_ms = 1000u / MINI_OS_DEFAULT_SYSTICK; /* 0 -> default tick rate */

    reload = (MINI_OS_CPU_CLOCK_HZ / 1000u) * ticks_per_ms; /* cycles per tick */

    MINI_OS_SYSTICK_RELOAD = reload - 1u;
    MINI_OS_SYSTICK_VAL = 0u;
    MINI_OS_SYSTICK_CTRL = MINI_OS_SYSTICK_CTRL_CLKSOURCE | MINI_OS_SYSTICK_CTRL_TICKINT | MINI_OS_SYSTICK_CTRL_ENABLE;
}
