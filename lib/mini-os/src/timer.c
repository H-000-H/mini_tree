/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file timer.c
 * @brief Software/hardware timers built on a dedicated tick wheel
 * @details
 *  - every timer sits on its own time wheel (s_timer_wheel), advanced one slot
 *    per OS tick by mini_os_timer_tick() from the SysTick handler; the wheel is
 *    independent of the scheduler's thread wheel so timers never touch a TCB
 *  - a HARD timer runs its callback directly in the tick (ISR) context
 *  - a SOFT timer is queued on s_soft_pending and its callback runs later on the
 *    timer service thread, which is woken through a binary semaphore
 *  - the module self-initializes: a constructor sets up the wheel/pending list/
 *    semaphore before main; the service thread is spawned lazily on the first
 *    SOFT timer start because it needs the scheduler up (mini_os_schedule_init
 *    runs in main, after every constructor), so it cannot be created from the
 *    constructor itself
 */
#include "timer.h"

#include "err.h"
#include "list.h"
#include "memory.h"
#include "mini_config.h"
#include "redef.h"
#include "schedule.h"
#include "semaphore.h"
#include "thread.h"

/** @brief Dedicated timer tick wheel (MINI_OS_TICK_WHEEL must be a power of 2) */
static mini_os_list_t s_timer_wheel[MINI_OS_TICK_WHEEL];

/** @brief Slot of the timer wheel serviced on the next tick */
static mini_os_uint32_t s_timer_slot = 0;

/** @brief SOFT timers whose deadline hit, waiting for the service thread to run them */
static mini_os_list_t s_soft_pending;

/** @brief Binary semaphore (max 1, starts empty) used to wake the service thread */
static mini_os_semaphore_t s_timer_sem;

/** @brief Timer service thread: static storage (no heap), spawned on first SOFT start */
static mini_os_thread_t  s_timer_tcb;
static mini_os_uint32_t  s_timer_stack[MINI_OS_TIMER_THREAD_STACK_SIZE / 4u] MINI_OS_ALIGN(8);
static mini_os_thread_t* s_timer_thread = MINI_OS_NULL;

#if MINI_OS_FIND_BY_NAME
/** @brief Global registry of every timer, used for the by-name lookup */
static mini_os_list_t s_mini_os_timer_list;
#endif

/**
 * @brief Timer module self-init (constructor, runs before main)
 * @return void
 * @note only touches module-private state (wheel, pending list, semaphore);
 *       the service thread is created lazily because a thread cannot be linked
 *       into the ready list before mini_os_schedule_init() runs in main
 */
MINI_OS_CONSTRUCTOR(MINI_OS_TIMER_DESTRUCTOR) void mini_os_g_timer_init(void)
{
    mini_os_uint32_t i;

    for (i = 0; i < (mini_os_uint32_t)MINI_OS_TICK_WHEEL; i++)
        mini_os_list_init(&s_timer_wheel[i]);
    s_timer_slot = 0;
    mini_os_list_init(&s_soft_pending);
    /* binary semaphore, created empty so the first take parks until a deadline */
    (void)mini_os_semaphore_create_static("timer_sem", 1u, 0u, &s_timer_sem);
#if MINI_OS_FIND_BY_NAME
    mini_os_list_init(&s_mini_os_timer_list);
#endif
}

/**
 * @brief Arm a timer into the wheel from its trigger_tick
 * @param[in] timer timer to (re)insert; trigger_tick must be > 0
 * @details slot = (current + tick) mod wheel and round = whole revolutions until
 *          it expires, matching the scheduler wheel formula
 * @note when the target slot is the slot being serviced right now (tick == WHEEL
 *       or a multiple of it) the visit in progress must not count: the round is
 *       ticks/wheel - 1 and the timer is linked at the HEAD of that slot,
 *       because the tick loop walks on with a captured next - a tail insert
 *       there would be revisited within the same tick and fire twice (or worse)
 * @note caller must hold interrupts disabled
 */
static void timer_wheel_insert(mini_os_timer_t* timer)
{
    mini_os_uint32_t ticks = (mini_os_uint32_t)timer->trigger_tick;
    mini_os_uint32_t slot = (s_timer_slot + ticks) & MINI_OS_TICK_WHEEL_MASK;
    mini_os_uint32_t shift = (mini_os_uint32_t)MINI_OS_CTZ(MINI_OS_TICK_WHEEL);

    timer->flag |= (mini_os_uint8_t)MINI_OS_TIMER_FLAG_ACTIVE;
    /* current slot: head insert with round - 1, the scan in progress must not count */
    if (slot == s_timer_slot)
    {
        timer->round = (ticks >> shift) - 1u;
        (void)mini_os_list_head(&timer->list_node, &s_timer_wheel[slot]);
    }
    else
    {
        timer->round = (ticks - 1u) >> shift;
        (void)mini_os_list_tail(&timer->list_node, &s_timer_wheel[slot]);
    }
}

/**
 * @brief Unlink a timer from wherever it is parked and clear its ACTIVE flag
 * @param[in] timer timer to detach
 * @note list_remove on an already-unlinked (self-referencing) node is a harmless
 *       no-op, so this is safe whether the timer was on the wheel, on the SOFT
 *       pending list, or not scheduled at all
 * @note caller must hold interrupts disabled
 */
static void timer_unlink(mini_os_timer_t* timer)
{
    mini_os_list_remove(&timer->list_node);
    timer->round = 0;
    timer->flag &= (mini_os_uint8_t)~MINI_OS_TIMER_FLAG_ACTIVE;
}

/**
 * @brief Validate the trigger_num / trigger_mode pair of a create call
 * @param[in] trigger_num one-shot (MINI_OS_TIMER_FLAG_ONE_SHOT) or periodic
 *            (MINI_OS_TIMER_FLAG_PERIODIC)
 * @param[in] trigger_mode hard (MINI_OS_TIMER_FLAG_HARD) or soft
 *            (MINI_OS_TIMER_FLAG_SOFT)
 * @return MINI_OS_TRUE when both selectors are in range; MINI_OS_FALSE otherwise
 */
static mini_os_bool_t timer_selectors_valid(mini_os_uint8_t trigger_num, mini_os_uint8_t trigger_mode)
{
    if ((trigger_num & (mini_os_uint8_t)~MINI_OS_TIMER_FLAG_PERIODIC) != 0u)
        return MINI_OS_FALSE;
    if ((trigger_mode & (mini_os_uint8_t)~MINI_OS_TIMER_FLAG_SOFT) != 0u)
        return MINI_OS_FALSE;
    return MINI_OS_TRUE;
}

/**
 * @brief Fill a timer descriptor (shared by the dynamic and static creators)
 * @param[in] timer descriptor to initialize
 * @param[in] name timer name (MINI_OS_NULL = empty name)
 * @param[in] cb callback run when the deadline hits
 * @param[in] arg argument passed to the callback
 * @param[in] tick period in ticks (> 0)
 * @param[in] flag trigger_num | trigger_mode (bits 0-1; ACTIVE is set on start)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL timer/callback, a
 *         non-positive tick or an out-of-range flag
 */
static mini_os_err_t timer_init(mini_os_timer_t* timer, const char* name, mini_os_timer_callback cb, void* arg, mini_os_tick_t tick, mini_os_uint8_t flag)
{
    if (timer == MINI_OS_NULL || cb == MINI_OS_NULL || tick <= 0 || (flag & (mini_os_uint8_t)~0x03u) != 0u)
        return MINI_OS_ERR_INVAL;
    mini_os_set_name(timer->timer_name, name, sizeof(timer->timer_name));
    timer->callback = cb;
    timer->arg = arg;
    timer->trigger_tick = tick;
    timer->flag = flag; /* bits 0-1 only; ACTIVE is set when the timer starts */
    timer->round = 0;
    mini_os_list_init(&timer->list_node);
#if MINI_OS_FIND_BY_NAME
    mini_os_list_init(&timer->list_name_node);
    mini_os_list_tail(&timer->list_name_node, &s_mini_os_timer_list);
#endif
    return MINI_OS_OK;
}

/**
 * @brief Create a timer on the heap (not started)
 * @param[in] name timer name (MINI_OS_NULL = empty name)
 * @param[in] cb callback run when the deadline hits
 * @param[in] arg argument passed to the callback
 * @param[in] trigger_tick period in ticks (> 0)
 * @param[in] trigger_num MINI_OS_TIMER_FLAG_ONE_SHOT or MINI_OS_TIMER_FLAG_PERIODIC
 * @param[in] trigger_mode MINI_OS_TIMER_FLAG_HARD (callback in tick/ISR context)
 *            or MINI_OS_TIMER_FLAG_SOFT (callback on the timer service thread)
 * @return timer handle on success; MINI_OS_NULL on invalid arguments, an invalid
 *         selector pair or out of memory
 * @note the timer is armed by mini_os_timer_start(), never by the creator
 */
mini_os_timer_t* mini_os_timer_create(const char* name, mini_os_timer_callback cb, void* arg, mini_os_tick_t trigger_tick, mini_os_uint8_t trigger_num, mini_os_uint8_t trigger_mode)
{
    mini_os_timer_t* timer;
    mini_os_uint8_t  flag;

    if (cb == MINI_OS_NULL || trigger_tick <= 0 || timer_selectors_valid(trigger_num, trigger_mode) == MINI_OS_FALSE)
        return MINI_OS_NULL;
    timer = (mini_os_timer_t*)mini_os_malloc(sizeof(mini_os_timer_t));
    if (timer == MINI_OS_NULL)
        return MINI_OS_NULL;
    flag = (mini_os_uint8_t)(trigger_num | trigger_mode);
    if (timer_init(timer, name, cb, arg, trigger_tick, flag) != MINI_OS_OK)
    {
        (void)mini_os_free(timer);
        return MINI_OS_NULL;
    }
    return timer;
}

/**
 * @brief Create a timer over caller-provided storage (not started)
 * @param[in] name timer name (MINI_OS_NULL = empty name)
 * @param[in] cb callback run when the deadline hits
 * @param[in] arg argument passed to the callback
 * @param[in] trigger_tick period in ticks (> 0)
 * @param[in] trigger_num MINI_OS_TIMER_FLAG_ONE_SHOT or MINI_OS_TIMER_FLAG_PERIODIC
 * @param[in] trigger_mode MINI_OS_TIMER_FLAG_HARD or MINI_OS_TIMER_FLAG_SOFT
 * @param[in] timer storage for the timer descriptor
 * @return timer handle on success; MINI_OS_NULL on invalid arguments or an
 *         invalid selector pair
 */
mini_os_timer_t* mini_os_timer_create_static(const char* name, mini_os_timer_callback cb, void* arg, mini_os_tick_t trigger_tick, mini_os_uint8_t trigger_num, mini_os_uint8_t trigger_mode, mini_os_timer_t* timer)
{
    mini_os_uint8_t flag;

    if (timer == MINI_OS_NULL || cb == MINI_OS_NULL || trigger_tick <= 0 || timer_selectors_valid(trigger_num, trigger_mode) == MINI_OS_FALSE)
        return MINI_OS_NULL;
    flag = (mini_os_uint8_t)(trigger_num | trigger_mode);
    if (timer_init(timer, name, cb, arg, trigger_tick, flag) != MINI_OS_OK)
        return MINI_OS_NULL;
    return timer;
}

/**
 * @brief Drain the SOFT pending list and run each callback in thread context
 * @details the pending list is spliced one node at a time under the IRQ lock;
 *          each periodic timer is re-armed BEFORE its callback runs (fixed-rate),
 *          a one-shot is deactivated, and the callback itself runs outside the
 *          critical section so it may block or call timer APIs
 * @note like the HARD path in mini_os_timer_tick, `timer` is never touched again
 *       after the callback returns: the callback may have stopped or deleted it
 */
static void timer_run_soft_callbacks(void)
{
    mini_os_timer_t* timer;
    mini_os_list_t*  node;
    mini_os_irq_t    irq;

    for (;;)
    {
        irq = mini_os_irq_save();
        if (mini_os_list_is_empty(&s_soft_pending))
        {
            mini_os_irq_restore(irq);
            break;
        }
        node = s_soft_pending.next;
        timer = mini_os_container_of(node, mini_os_timer_t, list_node);
        mini_os_list_remove(node);
        if ((timer->flag & MINI_OS_TIMER_FLAG_PERIODIC) != 0u)
            timer_wheel_insert(timer); /* fixed-rate re-arm for the next period */
        else
            timer->flag &= (mini_os_uint8_t)~MINI_OS_TIMER_FLAG_ACTIVE;
        mini_os_irq_restore(irq);

        if (timer->callback != MINI_OS_NULL)
            timer->callback(timer->arg);
    }
}

/**
 * @brief Timer service thread body: run the queued SOFT callbacks, then park
 * @param[in] param unused
 * @note drain comes BEFORE the take so that both wake paths converge: a thread
 *       woken from its park resumes inside the take and drains on the next
 *       loop pass, and an entry that starts afresh (host test harness, or any
 *       re-entry) drains before it blocks. No wake can be lost either way: the
 *       pending queue and the semaphore token are updated together in the
 *       SysTick critical section, and take's count check + park are atomic
 */
static void mini_os_timer_thread_entry(void* param)
{
    (void)param;
    for (;;)
    {
        timer_run_soft_callbacks();
        (void)mini_os_semaphore_take(&s_timer_sem, MINI_OS_WAIT_FOREVER);
    }
}

/**
 * @brief Spawn the SOFT-timer service thread once
 * @note created on the first SOFT timer start rather than from the constructor:
 *       the scheduler ready list only exists after mini_os_schedule_init(),
 *       which runs in main once every constructor has finished
 * @note thread/main context only; the thread uses static storage, so it never
 *       touches the heap
 */
static void timer_thread_ensure(void)
{
    if (s_timer_thread != MINI_OS_NULL)
        return;
    s_timer_thread = mini_os_thread_create_static(MINI_OS_TIMER_THREAD_NAME, MINI_OS_TIMER_THREAD_STACK_SIZE, MINI_OS_TIMER_THREAD_PRIORITY, mini_os_timer_thread_entry, MINI_OS_NULL, s_timer_stack, &s_timer_tcb);
}

/**
 * @brief (Re)start a timer
 * @param[in] timer timer to start
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL or
 *         its trigger_tick is not positive
 * @details an already running timer is unlinked first, so a start always means
 *          "count the full period from now"
 * @note a SOFT timer spawns the service thread on its first start, so this is a
 *       thread/main context call for SOFT timers
 */
mini_os_err_t mini_os_timer_start(mini_os_timer_t* timer)
{
    mini_os_irq_t irq;

    if (timer == MINI_OS_NULL || timer->trigger_tick <= 0)
        return MINI_OS_ERR_INVAL;
    if ((timer->flag & MINI_OS_TIMER_FLAG_SOFT) != 0u)
        timer_thread_ensure(); /* SOFT callbacks run on the service thread */
    irq = mini_os_irq_save();
    timer_unlink(timer); /* restart cleanly if it was already running */
    timer_wheel_insert(timer);
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Stop a timer (keeps the descriptor and its configuration)
 * @param[in] timer timer to stop
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL
 * @note a callback that is already queued on the SOFT pending list is not
 *       removed: only the wheel link is dropped
 */
mini_os_err_t mini_os_timer_stop(mini_os_timer_t* timer)
{
    mini_os_irq_t irq;

    if (timer == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    timer_unlink(timer);
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Stop a heap timer and free its descriptor
 * @param[in] timer timer to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_delete(mini_os_timer_t* timer)
{
    mini_os_irq_t irq;

    if (timer == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    timer_unlink(timer);
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&timer->list_name_node);
#endif
    mini_os_irq_restore(irq);
    (void)mini_os_free(timer);
    return MINI_OS_OK;
}

/**
 * @brief Stop a static timer (never frees the caller storage)
 * @param[in] timer timer to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_delete_static(mini_os_timer_t* timer)
{
    mini_os_irq_t irq;

    if (timer == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    timer_unlink(timer);
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&timer->list_name_node);
#endif
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Change the period of a timer
 * @param[in] timer timer to configure
 * @param[in] trigger_tick new period in ticks (> 0)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL or
 *         trigger_tick is not positive
 * @note a running timer is re-armed with the new period immediately, so the
 *       change takes effect from now on; a stopped one only stores the value
 */
mini_os_err_t mini_os_timer_set_trigger_tick(mini_os_timer_t* timer, mini_os_tick_t trigger_tick)
{
    mini_os_irq_t  irq;
    mini_os_bool_t was_active;

    if (timer == MINI_OS_NULL || trigger_tick <= 0)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    was_active = (mini_os_bool_t)((timer->flag & MINI_OS_TIMER_FLAG_ACTIVE) != 0u);
    timer_unlink(timer);
    timer->trigger_tick = trigger_tick;
    if (was_active != MINI_OS_FALSE)
        timer_wheel_insert(timer); /* running timer: re-arm with the new period */
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Change the callback of a timer
 * @param[in] timer timer to configure
 * @param[in] cb new callback
 * @param[in] arg argument passed to the new callback
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer or cb is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_set_callback(mini_os_timer_t* timer, mini_os_timer_callback cb, void* arg)
{
    mini_os_irq_t irq;

    if (timer == MINI_OS_NULL || cb == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    timer->callback = cb;
    timer->arg = arg;
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Advance the timer wheel by one slot and service the expired timers
 * @details runs with interrupts masked from mini_os_systick_handler(): a timer
 *          with rounds left is decremented and stays parked, an expired one is
 *          taken off the wheel. A SOFT timer is queued on the pending list and
 *          stays ACTIVE (the service thread re-arms or deactivates it), a HARD
 *          timer is re-armed or deactivated right away and its callback runs
 *          here, in tick (ISR) context
 * @note a HARD timer is never touched again after its callback returns: the
 *       callback may stop or delete it, so a trailing re-arm would both be a
 *       use-after-free and defeat a stop done by the callback
 * @note the pending queue and the semaphore token are updated in one critical
 *       section, so the service thread cannot miss a wake
 */
void mini_os_timer_tick(void)
{
    mini_os_list_t*  node;
    mini_os_list_t*  next;
    mini_os_timer_t* timer;
    mini_os_bool_t   wake = MINI_OS_FALSE;

    s_timer_slot = (s_timer_slot + 1) & MINI_OS_TICK_WHEEL_MASK;

    for (node = s_timer_wheel[s_timer_slot].next; node != &s_timer_wheel[s_timer_slot]; node = next)
    {
        next = node->next;
        timer = mini_os_container_of(node, mini_os_timer_t, list_node);
        if (timer->round > 0)
        {
            timer->round--; /* not this revolution yet */
            continue;
        }
        /* deadline reached: take the timer off the wheel */
        mini_os_list_remove(&timer->list_node);

        if ((timer->flag & MINI_OS_TIMER_FLAG_SOFT) != 0u)
        {
            /* SOFT: defer the callback to the service thread (stay ACTIVE) */
            mini_os_list_tail(&timer->list_node, &s_soft_pending);
            wake = MINI_OS_TRUE;
        }
        else
        {
            /* HARD: re-arm/disarm BEFORE running the callback. The callback may
             * stop or even delete this very timer, so nothing of `timer` may be
             * touched once it returns (a trailing re-arm would be a
             * use-after-free and would defeat a stop done by the callback) */
            if ((timer->flag & MINI_OS_TIMER_FLAG_PERIODIC) != 0u)
                timer_wheel_insert(timer); /* fixed-rate re-arm */
            else
                timer->flag &= (mini_os_uint8_t)~MINI_OS_TIMER_FLAG_ACTIVE;
            if (timer->callback != MINI_OS_NULL)
                timer->callback(timer->arg);
        }
    }

    if (wake != MINI_OS_FALSE)
    {
        /* hand the unit to the parked service thread (or publish it), then let
         * the ISR-exit path switch to it when it outranks the interrupted one */
        (void)mini_os_semaphore_give_isr(&s_timer_sem);
        (void)mini_os_schedule_yield_isr();
    }
}
