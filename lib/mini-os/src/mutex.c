/**
 * @file mutex.c
 * @author H-000-H
 * @brief mutex implementation (recursive variant, priority inheritance over the
 *        embedded binary semaphore)
 * @note
 *  - inheritance is tracked per thread, not per mutex: the owner keeps its
 *    caller-requested priority in TCB base_priority and every mutex it holds is
 *    linked into owner->hold_list through mutex->hold_node, so its effective
 *    priority is min(base_priority, highest waiter of every mutex it holds)
 *  - deadline inheritance runs next to priority inheritance when EDF or
 *    SCHED_DEADLINE is compiled in: the owner also inherits the earliest absolute
 *    deadline among its waiters. It is kept in TCB dl_deadline_inherit (the own
 *    deadline in dl_deadline_time is left alone), so the scheduler orders the
 *    owner earlier without disturbing CBS budget/period accounting
 *  - the requirement travels along the wait chain: when a holder is itself
 *    blocked on another mutex, that mutex's owner inherits the same priority and
 *    deadline requirement (capped at MINI_OS_MUTEX_PI_CHAIN_MAX links, which also
 *    breaks wait cycles)
 *  - every inheritance step runs with interrupts disabled
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include <mutex.h>

#include "minierr.h"
#include <list.h>
#include <memory.h>
#include <mini_config.h>
#include <obj.h>
#include <redef.h>
#include <schedule.h>
#include <semaphore.h>
#include <thread.h>

/**
 * @brief Deadline a taker folds into the inheritance walk before it is parked
 * @details its own effective (possibly already inherited) deadline; 0 when no
 *          deadline scheduling is compiled in or the thread has no deadline
 */
#if MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE
#define MINI_OS_MUTEX_DEADLINE_OF(thread) mini_os_thread_effective_deadline(thread)
#else
#define MINI_OS_MUTEX_DEADLINE_OF(thread) ((mini_os_tick_t)0)
#endif

#if MINI_OS_FIND_BY_NAME
/** @brief Global registry of every mutex, used by mini_os_mutex_find_by_name() */
static mini_os_list_t g_mutex_list;

/**
 * @brief Initialize the global mutex registry (constructor, runs before main)
 * @note the list head becomes a self-referencing sentinel, so mutexes can be
 *       linked before the scheduler is up
 */
MINI_OS_CONSTRUCTOR(MINI_OS_MUTEX_REGISTRY_CONSTRUCTOR) void mini_os_mutex_registry_init(void) { mini_os_list_init(&g_mutex_list); }
#endif

/**
 * @brief Highest priority among the parked waiters of a mutex
 * @param[in] mutex mutex whose wait list is scanned
 * @return smallest priority number on the wait list; MINI_OS_PRIORITY when empty
 * @note caller must hold interrupts disabled
 */
static mini_os_uint8_t mini_os_mutex_highest_waiter(mini_os_mutex_t* mutex)
{
    mini_os_list_t* node;
    mini_os_uint8_t highest = MINI_OS_PRIORITY;

    for (node = mutex->semaphore.wait_list.next; node != &mutex->semaphore.wait_list; node = node->next)
    {
        mini_os_thread_t* waiter = mini_os_container_of(node, mini_os_thread_t, wait_node);

        if (waiter->priority < highest)
            highest = waiter->priority;
    }
    return highest;
}

/**
 * @brief Priority a thread has to run at: its base plus every mutex it holds
 * @param[in] thread thread to evaluate
 * @return smallest priority number required, always a valid priority
 * @note caller must hold interrupts disabled
 */
static mini_os_uint8_t mini_os_mutex_required_priority(mini_os_thread_t* thread)
{
    mini_os_list_t* node;
    mini_os_uint8_t required = thread->base_priority;

    for (node = thread->hold_list.next; node != &thread->hold_list; node = node->next)
    {
        mini_os_mutex_t* held = mini_os_container_of(node, mini_os_mutex_t, hold_node);
        mini_os_uint8_t  waiter = mini_os_mutex_highest_waiter(held);

        if (waiter < required)
            required = waiter;
    }
    return required;
}

#if MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE
/**
 * @brief Earliest effective deadline among the parked waiters of a mutex
 * @param[in] mutex mutex whose wait list is scanned
 * @return smallest absolute deadline required by a waiter that is scheduled by
 *         deadline (wrap-safe comparison); 0 when no waiter has one
 * @note a waiter's own deadline is used unless it already carries an inherited
 *       one, so a chain of blocked owners keeps pushing the earliest deadline on
 * @note caller must hold interrupts disabled
 */
static mini_os_tick_t mini_os_mutex_earliest_waiter_deadline(mini_os_mutex_t* mutex)
{
    mini_os_list_t* node;
    mini_os_tick_t  earliest = 0;

    for (node = mutex->semaphore.wait_list.next; node != &mutex->semaphore.wait_list; node = node->next)
    {
        mini_os_thread_t* waiter = mini_os_container_of(node, mini_os_thread_t, wait_node);
        mini_os_tick_t    deadline = mini_os_thread_effective_deadline(waiter);

        if (deadline != 0 && (earliest == 0 || mini_os_deadline_before(deadline, earliest) != MINI_OS_FALSE))
            earliest = deadline;
    }
    return earliest;
}

/**
 * @brief Deadline a thread has to run at, derived from every mutex it holds
 * @param[in] thread thread to evaluate
 * @return earliest waiter deadline over all mutexes in thread->hold_list; 0 when
 *         no waiter of any held mutex is scheduled by deadline
 * @note the deadline counterpart of mini_os_mutex_required_priority(): the
 *       thread's own deadline is NOT folded in here, the scheduler takes the
 *       earlier of the two (see mini_os_thread_effective_deadline)
 * @note caller must hold interrupts disabled
 */
static mini_os_tick_t mini_os_mutex_required_deadline(mini_os_thread_t* thread)
{
    mini_os_list_t* node;
    mini_os_tick_t  required = 0;

    for (node = thread->hold_list.next; node != &thread->hold_list; node = node->next)
    {
        mini_os_mutex_t* held = mini_os_container_of(node, mini_os_mutex_t, hold_node);
        mini_os_tick_t   waiter = mini_os_mutex_earliest_waiter_deadline(held);

        if (waiter != 0 && (required == 0 || mini_os_deadline_before(waiter, required) != MINI_OS_FALSE))
            required = waiter;
    }
    return required;
}
#endif /* MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE */

/**
 * @brief Apply the required priority/deadline of a thread and push the requirement up the wait chain
 * @param[in] thread thread to recompute
 * @param[in] extra additional priority requirement to fold in (MINI_OS_PRIORITY =
 *            none); used by a taker that is about to park and is therefore not on
 *            the wait list yet
 * @param[in] extra_deadline additional deadline requirement to fold in
 *            (0 = none); the taker's own effective deadline, passed for the same
 *            reason as extra
 * @param[in] depth chain links already walked (loop guard)
 * @details both requirements travel together: the thread's effective priority is
 *          min(base_priority, extra, highest waiter of every held mutex) and its
 *          inherited deadline is the earliest of extra_deadline and the earliest
 *          waiter deadline of every held mutex. The walk follows
 *          thread->wait_mutex as long as the thread is really parked on that
 *          mutex's wait list, so a stale back pointer ends it;
 *          MINI_OS_MUTEX_PI_CHAIN_MAX caps the length and breaks wait cycles
 * @note caller must hold interrupts disabled
 */
static void mini_os_mutex_propagate(mini_os_thread_t* thread, mini_os_uint8_t extra, mini_os_tick_t extra_deadline, mini_os_uint32_t depth)
{
#if !(MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE)
    (void)extra_deadline; /* deadline inheritance not compiled in */
#endif
    while (thread != MINI_OS_NULL && depth < MINI_OS_MUTEX_PI_CHAIN_MAX)
    {
        mini_os_uint8_t  required = mini_os_mutex_required_priority(thread);
        mini_os_mutex_t* waited;

        if (extra < required)
            required = extra;
        if (required != thread->priority)
            (void)mini_os_thread_priority_apply(thread, required);

#if MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE
        {
            mini_os_tick_t required_deadline = mini_os_mutex_required_deadline(thread);

            if (extra_deadline != 0 && (required_deadline == 0 || mini_os_deadline_before(extra_deadline, required_deadline) != MINI_OS_FALSE))
                required_deadline = extra_deadline;
            if (required_deadline != thread->dl_deadline_inherit)
                (void)mini_os_thread_deadline_apply(thread, required_deadline);
            extra_deadline = required_deadline; /* the requirement travels on */
        }
#endif

        /* the requirement does not stop at a blocked holder: whoever owns the
         * mutex it is parked on has to run at least as fast */
        waited = thread->wait_mutex;
        if (waited == MINI_OS_NULL || thread->wait_list != &waited->semaphore.wait_list)
            return; /* not parked on a mutex wait list, or the wait already ended */
        thread = waited->owner;
        extra = required;
        depth++;
    }
}

/**
 * @brief Release every parked waiter with a failed result
 * @param[in] mutex mutex whose waiters are released
 * @return MINI_OS_TRUE when at least one thread was moved back to the ready list
 * @details each waiter is unparked through mini_os_sync_wake_thread() with
 *          wait_status set from the object's kill policy
 * @note wait_mutex is cleared first: the descriptor is about to disappear and the
 *       waiter's timeout path must not dereference it again
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_mutex_kill_waiters(mini_os_mutex_t* mutex)
{
    mini_os_list_t* node;
    mini_os_list_t* next;
    mini_os_bool_t  woken = MINI_OS_FALSE;
    /* DEAD is re-checked by the caller, so its park must return OK for that to
     * happen; a TIMEOUT is reported by the park itself, so its park must fail */
    mini_os_bool_t  report_dead = (mini_os_obj_kill_error(&mutex->obj) == MINI_OS_ERR_DEAD) ? MINI_OS_TRUE : MINI_OS_FALSE;

    for (node = mutex->semaphore.wait_list.next; node != &mutex->semaphore.wait_list; node = next)
    {
        mini_os_thread_t* thread = mini_os_container_of(node, mini_os_thread_t, wait_node);

        next = node->next;
        thread->wait_mutex = MINI_OS_NULL;
        mini_os_sync_wake_thread(thread, report_dead);
        woken = MINI_OS_TRUE;
    }
    return woken;
}

/**
 * @brief Initialize a mutex descriptor over already-provided storage
 * @param[in] mutex mutex descriptor to initialize
 * @param[in] name mutex name (MINI_OS_NULL = empty name)
 * @param[in] is_recursive MINI_OS_TRUE allows the owner to re-lock (depth++)
 * @param[in] heap_owned MINI_OS_TRUE when the descriptor came from the heap
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when mutex is MINI_OS_NULL
 * @details the embedded semaphore is configured as a binary one (count and
 *          max_count 1, i.e. created unlocked), the owner is cleared, the
 *          recursion depth is 0 and hold_node is self-referencing, which is the
 *          "not held" state of the mini-os list convention
 * @note the embedded semaphore carries an object header only because
 *       mini_os_semaphore_take()/give() take a reference on it; it is never
 *       reclaimed on its own, so it is initialized as caller-owned storage
 */
static mini_os_err_t mini_os_mutex_init(mini_os_mutex_t* mutex, const char* name, mini_os_bool_t is_recursive, mini_os_bool_t heap_owned)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    mini_os_set_name(mutex->semaphore.name, name, MINI_OS_SEMAPHORE_NAME_LEN);
    mutex->semaphore.count = 1u;     /* created unlocked */
    mutex->semaphore.max_count = 1u; /* binary: one unit */
    mini_os_obj_init(&mutex->semaphore.obj, MINI_OS_FALSE);
    mini_os_list_init(&mutex->semaphore.wait_list);
    mutex->owner = MINI_OS_NULL;
    mutex->depth = 0u;
    mutex->is_recursive = is_recursive;
    mini_os_list_init(&mutex->hold_node); /* unheld: self-linked until acquired */
    mini_os_obj_init(&mutex->obj, heap_owned);

#if MINI_OS_FIND_BY_NAME
    {
        mini_os_irq_t irq = mini_os_irq_save();

        mini_os_list_init(&mutex->g_list_node);
        mini_os_list_tail(&mutex->g_list_node, &g_mutex_list);
        mini_os_irq_restore(irq);
    }
#endif
    return MINI_OS_OK;
}

mini_os_mutex_t* mini_os_mutex_create(const char* name)
{
    mini_os_mutex_t* mutex = (mini_os_mutex_t*)mini_os_malloc(sizeof(mini_os_mutex_t));

    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_FALSE, MINI_OS_TRUE) != MINI_OS_OK)
    {
        (void)mini_os_free(mutex);
        return MINI_OS_NULL;
    }
    return mutex;
}

mini_os_mutex_t* mini_os_mutex_recursive_create(const char* name)
{
    mini_os_mutex_t* mutex = (mini_os_mutex_t*)mini_os_malloc(sizeof(mini_os_mutex_t));

    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_TRUE, MINI_OS_TRUE) != MINI_OS_OK)
    {
        (void)mini_os_free(mutex);
        return MINI_OS_NULL;
    }
    return mutex;
}

mini_os_mutex_t* mini_os_mutex_create_static(mini_os_mutex_t* mutex, const char* name)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_FALSE, MINI_OS_FALSE) != MINI_OS_OK)
        return MINI_OS_NULL;
    return mutex;
}

mini_os_mutex_t* mini_os_mutex_recursive_create_static(const char* name, mini_os_mutex_t* mutex)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_TRUE, MINI_OS_FALSE) != MINI_OS_OK)
        return MINI_OS_NULL;
    return mutex;
}

mini_os_err_t mini_os_mutex_lock(mini_os_mutex_t* mutex, mini_os_tick_t timeout_tick)
{
    mini_os_thread_t* current;
    mini_os_mutex_t*  parked_on;
    mini_os_err_t     ret;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    /* the reference is held across the whole call, so a destroy request cannot
     * reclaim the descriptor under a parked taker */
    ret = mini_os_obj_acquire(&mutex->obj);
    if (ret != MINI_OS_OK)
        return ret;

    irq = mini_os_irq_save();
    current = mini_os_thread_get_current();
    if (current == MINI_OS_NULL)
    {
        mini_os_irq_restore(irq);
        ret = MINI_OS_ERR_INVAL; /* ownership needs a thread context */
    }
    else if (mutex->semaphore.count > 0u)
    {
        /* uncontended: acquire inside this critical section */
        mutex->semaphore.count--;
        mutex->owner = current;
        mutex->depth = 1u;
        mini_os_list_tail(&mutex->hold_node, &current->hold_list);
        mini_os_irq_restore(irq);
        ret = MINI_OS_OK;
    }
    else if (mutex->owner == current)
    {
        if (mutex->is_recursive == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            ret = MINI_OS_ERR_BUSY; /* non-recursive owner re-lock: fail fast */
        }
        else if (mutex->depth == MINI_OS_UINT8_MAX)
        {
            mini_os_irq_restore(irq);
            ret = MINI_OS_ERR_BUSY; /* recursion overflow */
        }
        else
        {
            mutex->depth++;
            mini_os_irq_restore(irq);
            ret = MINI_OS_OK;
        }
    }
    else if (timeout_tick == 0)
    {
        mini_os_irq_restore(irq);
        ret = MINI_OS_ERR_AGAIN; /* contested and non-blocking */
    }
    else
    {
        if (mutex->owner != MINI_OS_NULL)
        {
            /* about to park: the owner has to run at least as fast as we do (and as
             * early as our deadline), and the requirement travels on when the owner
             * is itself blocked. Our own priority and deadline are passed as extra
             * because we are not on the wait list yet */
            current->wait_mutex = mutex;
            mini_os_mutex_propagate(mutex->owner, current->priority, MINI_OS_MUTEX_DEADLINE_OF(current), 0u);
        }
        /* the mutex reference acquired above is held across the park, but the
         * park itself happens inside the embedded semaphore: record it here, so
         * deleting this thread while parked drops it instead of leaking it (the
         * semaphore records its own reference on top of it; its descriptor is
         * caller-owned storage that is never reclaimed on its own) */
        mini_os_sync_wait_ref_push(current, &mutex->obj, mutex, sizeof(mini_os_mutex_t));
        mini_os_irq_restore(irq);

        /* the take parks atomically and, on success, the unit was handed over directly */
        ret = mini_os_semaphore_take(&mutex->semaphore, timeout_tick);

        irq = mini_os_irq_save();
        mini_os_sync_wait_ref_pop(current);
        parked_on = current->wait_mutex;
        current->wait_mutex = MINI_OS_NULL;
        if (ret == MINI_OS_OK)
        {
            mutex->owner = current;
            mutex->depth = 1u;
            mini_os_list_tail(&mutex->hold_node, &current->hold_list);
            /* waiters that parked behind us boosted the previous owner, not us:
             * fold their requirement (and that of our other held mutexes) in now */
            mini_os_mutex_propagate(current, MINI_OS_PRIORITY, 0, 0u);
        }
        else if (ret == MINI_OS_ERR_TIMEOUT && parked_on == mutex && mutex->owner != MINI_OS_NULL)
        {
            /* we left the wait set, so drop the boost only we justified. parked_on
             * is MINI_OS_NULL when the mutex was kill-deleted under us: comparing
             * pointers keeps us from dereferencing a freed descriptor */
            mini_os_mutex_propagate(mutex->owner, MINI_OS_PRIORITY, 0, 0u);
        }
        mini_os_irq_restore(irq);
    }
    mini_os_obj_release(&mutex->obj, mutex, sizeof(mini_os_mutex_t));
    return ret;
}

mini_os_err_t mini_os_mutex_unlock(mini_os_mutex_t* mutex)
{
    mini_os_thread_t* current;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_get_current();
    if (current == MINI_OS_NULL || mutex->owner != current)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_INVAL; /* only the owner may unlock */
    }
    mutex->depth--;
    if (mutex->depth > 0u)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_OK; /* still recursively held */
    }
    /* final unlock: leave the owner's hold list and settle on the priority the
     * mutexes it keeps holding still require (base when this was the last one) */
    mini_os_list_remove(&mutex->hold_node);
    mutex->owner = MINI_OS_NULL;
    mini_os_mutex_propagate(current, MINI_OS_PRIORITY, 0, 0u);
    mini_os_irq_restore(irq);

    /* hands the unit to the oldest waiter (and yields) or republishes it */
    return mini_os_semaphore_give(&mutex->semaphore);
}

mini_os_err_t mini_os_mutex_lock_isr(mini_os_mutex_t* mutex)
{
    mini_os_thread_t* current;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_get_current();
    if (current == MINI_OS_NULL)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_INVAL;
    }
    if (mutex->semaphore.count > 0u)
    {
        mutex->semaphore.count--;
        mutex->owner = current; /* attributed to the interrupted thread */
        mutex->depth = 1u;
        mini_os_list_tail(&mutex->hold_node, &current->hold_list);
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    if (mutex->owner == current)
    {
        if (mutex->is_recursive == MINI_OS_FALSE || mutex->depth == MINI_OS_UINT8_MAX)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_BUSY;
        }
        mutex->depth++;
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    mini_os_irq_restore(irq);
    return MINI_OS_ERR_AGAIN; /* contested: an ISR never blocks and never boosts */
}

mini_os_err_t mini_os_mutex_unlock_isr(mini_os_mutex_t* mutex)
{
    mini_os_thread_t* current;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_get_current();
    if (current == MINI_OS_NULL || mutex->owner != current)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_INVAL;
    }
    mutex->depth--;
    if (mutex->depth > 0u)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    mini_os_list_remove(&mutex->hold_node);
    mutex->owner = MINI_OS_NULL;
    mini_os_mutex_propagate(current, MINI_OS_PRIORITY, 0, 0u);
    mini_os_irq_restore(irq);

    /* no yield here: the ISR caller decides via the is_heigher_priority pattern */
    return mini_os_semaphore_give_isr(&mutex->semaphore);
}

mini_os_err_t mini_os_mutex_enable_kill(mini_os_mutex_t* mutex)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    /* a killed waiter must fail its mini_os_semaphore_take, so arm the timeout
     * flavour of the kill policy */
    mini_os_obj_enable_kill_reports_timeout(&mutex->obj);
    return MINI_OS_OK;
}

mini_os_err_t mini_os_mutex_priority_recompute(mini_os_thread_t* thread)
{
    mini_os_irq_t irq;

    if (thread == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    mini_os_mutex_propagate(thread, MINI_OS_PRIORITY, 0, 0u);
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

mini_os_bool_t mini_os_mutex_kill_held(mini_os_thread_t* thread)
{
    mini_os_list_t* node;
    mini_os_list_t* next;
    mini_os_bool_t  woken = MINI_OS_FALSE;
    mini_os_irq_t   irq;

    if (thread == MINI_OS_NULL)
        return MINI_OS_FALSE;

    irq = mini_os_irq_save();
    for (node = thread->hold_list.next; node != &thread->hold_list; node = next)
    {
        mini_os_mutex_t* mutex = mini_os_container_of(node, mini_os_mutex_t, hold_node);

        next = node->next; /* captured first: both helpers below re-link nodes */
        if (mini_os_mutex_kill_waiters(mutex) != MINI_OS_FALSE)
            woken = MINI_OS_TRUE;
        mini_os_list_remove(&mutex->hold_node);
        mutex->owner = MINI_OS_NULL;
        mutex->depth = 0u;
        mutex->semaphore.count = mutex->semaphore.max_count; /* unit back: the mutex is free */
    }
    mini_os_irq_restore(irq);
    return woken;
}

/**
 * @brief Shared delete path of the heap and static variants
 * @param[in] mutex mutex to delete
 * @param[in] is_static MINI_OS_TRUE for caller storage (clear only), MINI_OS_FALSE
 *            for heap storage (free)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when mutex is MINI_OS_NULL;
 *         MINI_OS_ERR_NOTSUPP for the wrong variant; MINI_OS_ERR_REJECT while an
 *         API call is in flight or the mutex is held/waited on and kill is not
 *         enabled
 * @details the teardown order matters: the wrong-storage check, then the
 *          in-flight-API check (a user in flight may still be writing the
 *          descriptor, so refuse unless kill is armed), the in-flight-unit check
 *          (count 0 with no owner and no waiter means a woken taker has not taken
 *          ownership yet, so freeing now would let it write a dead descriptor),
 *          then the kill path. The descriptor is reclaimed by the last in-flight
 *          user that leaves, so it outlives every in-flight call (heap storage
 *          freed, caller storage cleared)
 * @note with kill enabled the waiters are released with MINI_OS_ERR_TIMEOUT, the
 *          owner loses this mutex's requirement and the mutex is left free
 */
static mini_os_err_t mini_os_mutex_delete_common(mini_os_mutex_t* mutex, mini_os_bool_t is_static)
{
    mini_os_bool_t woken = MINI_OS_FALSE;
    mini_os_irq_t  irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (mini_os_obj_is_static(&mutex->obj) != is_static)
        return MINI_OS_ERR_NOTSUPP; /* the caller picked the other delete variant */

    irq = mini_os_irq_save();
    if (mini_os_obj_user_count(&mutex->obj) > 1u && mini_os_obj_is_kill_enabled(&mutex->obj) == MINI_OS_FALSE)
    {
        /* an API call is in flight: destroying now would leave it operating on a
         * rejected descriptor, so refuse until it leaves */
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_REJECT;
    }
    if (mutex->semaphore.count == 0u && mutex->owner == MINI_OS_NULL && mini_os_list_is_empty(&mutex->semaphore.wait_list))
    {
        /* unit in flight: already handed to a woken taker that has not taken
         * ownership yet, freeing now would leave it writing a dead descriptor */
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_REJECT;
    }
    if (!mini_os_list_is_empty(&mutex->semaphore.wait_list) || mutex->owner != MINI_OS_NULL)
    {
        mini_os_thread_t* owner;

        if (mini_os_obj_is_kill_enabled(&mutex->obj) == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_REJECT; /* held or waited: unlock/park out first */
        }
        woken = mini_os_mutex_kill_waiters(mutex);
        owner = mutex->owner;
        mini_os_list_remove(&mutex->hold_node);
        mutex->owner = MINI_OS_NULL;
        mutex->depth = 0u;
        if (owner != MINI_OS_NULL)
        {
            /* the force-released owner loses this mutex's requirement */
            mini_os_mutex_propagate(owner, MINI_OS_PRIORITY, 0, 0u);
        }
    }
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&mutex->g_list_node);
#endif
    (void)mini_os_obj_request_destroy(&mutex->obj, mutex, sizeof(mini_os_mutex_t));
    mini_os_irq_restore(irq);

    if (woken != MINI_OS_FALSE)
        (void)mini_os_schedule_yield(); /* killed waiters may outrank the caller */
    return MINI_OS_OK;
}

mini_os_err_t mini_os_mutex_delete(mini_os_mutex_t* mutex) { return mini_os_mutex_delete_common(mutex, MINI_OS_FALSE); }

mini_os_err_t mini_os_mutex_delete_static(mini_os_mutex_t* mutex) { return mini_os_mutex_delete_common(mutex, MINI_OS_TRUE); }

#if MINI_OS_FIND_BY_NAME
mini_os_mutex_t* mini_os_mutex_find_by_name(const char* name)
{
    mini_os_list_t* node;

    if (name == MINI_OS_NULL)
        return MINI_OS_NULL;
    for (node = g_mutex_list.next; node != &g_mutex_list; node = node->next)
    {
        mini_os_mutex_t* mutex = mini_os_container_of(node, mini_os_mutex_t, g_list_node);

        if (MINI_OS_STRCMP(mutex->semaphore.name, name) == 0)
            return mutex;
    }
    return MINI_OS_NULL;
}
#endif
