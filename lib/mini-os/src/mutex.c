/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @brief mutex implementation (recursive variant, priority inheritance over the
 *        embedded binary semaphore)
 * @file mutex.c
 * @author H-000-H
 * @note
 *  - inheritance is tracked per thread, not per mutex: the owner keeps its
 *    caller-requested priority in TCB base_priority and every mutex it holds is
 *    linked into owner->hold_list through mutex->hold_node, so its effective
 *    priority is min(base_priority, highest waiter of every mutex it holds)
 *  - the requirement travels along the wait chain: when a holder is itself
 *    blocked on another mutex, that mutex's owner inherits the same requirement
 *    (capped at MINI_OS_MUTEX_PI_CHAIN_MAX links, which also breaks wait cycles)
 *  - every inheritance step runs with interrupts disabled
 */
#include "mutex.h"

#include "err.h"
#include "list.h"
#include "memory.h"
#include "mini_config.h"
#include "redef.h"
#include "schedule.h"
#include "semaphore.h"
#include "thread.h"

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

/**
 * @brief Apply the required priority of a thread and push the requirement up the wait chain
 * @param[in] thread thread to recompute
 * @param[in] extra additional requirement to fold in (MINI_OS_PRIORITY = none);
 *            used by a taker that is about to park and is therefore not on the
 *            wait list yet
 * @param[in] depth chain links already walked (loop guard)
 * @details the walk follows thread->wait_mutex as long as the thread is really
 *          parked on that mutex's wait list, so a stale back pointer ends it;
 *          MINI_OS_MUTEX_PI_CHAIN_MAX caps the length and breaks wait cycles
 * @note caller must hold interrupts disabled
 */
static void mini_os_mutex_propagate(mini_os_thread_t* thread, mini_os_uint8_t extra, mini_os_uint32_t depth)
{
    while (thread != MINI_OS_NULL && depth < MINI_OS_MUTEX_PI_CHAIN_MAX)
    {
        mini_os_uint8_t  required = mini_os_mutex_required_priority(thread);
        mini_os_mutex_t* waited;

        if (extra < required)
            required = extra;
        if (required != thread->priority)
            (void)mini_os_thread_priority_apply(thread, required);

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
 * @details each waiter leaves the wait list and the time wheel (when it was
 *          parked there for a timeout) and is made ready again
 * @note wait_done stays MINI_OS_FALSE, so each waiter's mini_os_semaphore_take
 *       returns MINI_OS_ERR_TIMEOUT and its mini_os_mutex_lock fails
 * @note wait_mutex is cleared as well: the descriptor is about to disappear and
 *       the waiter's timeout path must not dereference it again
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_mutex_kill_waiters(mini_os_mutex_t* mutex)
{
    mini_os_list_t* node;
    mini_os_list_t* next;
    mini_os_bool_t  woken = MINI_OS_FALSE;

    for (node = mutex->semaphore.wait_list.next; node != &mutex->semaphore.wait_list; node = next)
    {
        mini_os_thread_t* thread = mini_os_container_of(node, mini_os_thread_t, wait_node);

        next = node->next;
        mini_os_list_remove(&thread->wait_node);
        thread->wait_list = MINI_OS_NULL;
        thread->wait_mutex = MINI_OS_NULL;
        thread->wait_done = MINI_OS_FALSE; /* the wait is killed, not satisfied */
        if (thread->wheel_slot < MINI_OS_TICK_WHEEL)
            (void)mini_os_remove_thread_from_blocked_list(thread);
        (void)mini_os_add_thread_to_ready_running_list(thread);
        woken = MINI_OS_TRUE;
    }
    return woken;
}

/**
 * @brief Initialize a mutex descriptor over already-provided storage
 * @param[in] mutex mutex descriptor to initialize
 * @param[in] name mutex name (MINI_OS_NULL = empty name)
 * @param[in] is_recuring MINI_OS_TRUE allows the owner to re-lock (depth++)
 * @param[in] is_static MINI_OS_TRUE when the storage is caller-provided
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when mutex is MINI_OS_NULL
 * @details the embedded semaphore is configured as a binary one (count and
 *          max_count 1, i.e. created unlocked), the owner is cleared, the
 *          recursion depth is 0 and hold_node is self-referencing, which is the
 *          "not held" state of the mini-os list convention
 */
static mini_os_err_t mini_os_mutex_init(mini_os_mutex_t* mutex, const char* name, mini_os_bool_t is_recuring, mini_os_bool_t is_static)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    mini_os_set_name(mutex->semaphore.name, name, MINI_OS_SEMAPHORE_NAME_LEN);
    mutex->semaphore.count = 1u;     /* created unlocked */
    mutex->semaphore.max_count = 1u; /* binary: one unit */
    mutex->semaphore.is_static = is_static;
    mini_os_list_init(&mutex->semaphore.wait_list);
    mutex->owner = MINI_OS_NULL;
    mutex->depth = 0u;
    mutex->is_recuring = is_recuring;
    mini_os_list_init(&mutex->hold_node); /* unheld: self-linked until acquired */
    mutex->kill_enable = MINI_OS_FALSE;

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

/**
 * @brief Create a non-recursive mutex on the heap
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @return mutex handle on success; MINI_OS_NULL on failure
 */
mini_os_mutex_t* mini_os_mutex_create(const char* name)
{
    mini_os_mutex_t* mutex = (mini_os_mutex_t*)mini_os_malloc(sizeof(mini_os_mutex_t));

    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_FALSE, MINI_OS_FALSE) != MINI_OS_OK)
    {
        (void)mini_os_free(mutex);
        return MINI_OS_NULL;
    }
    return mutex;
}

/**
 * @brief Create a recursive mutex on the heap
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @return mutex handle on success; MINI_OS_NULL on failure
 */
mini_os_mutex_t* mini_os_mutex_recuring_create(const char* name)
{
    mini_os_mutex_t* mutex = (mini_os_mutex_t*)mini_os_malloc(sizeof(mini_os_mutex_t));

    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_TRUE, MINI_OS_FALSE) != MINI_OS_OK)
    {
        (void)mini_os_free(mutex);
        return MINI_OS_NULL;
    }
    return mutex;
}

/**
 * @brief Create a non-recursive mutex over caller-provided storage
 * @param[in] mutex storage for the mutex descriptor
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @return mutex handle on success; MINI_OS_NULL on invalid arguments
 */
mini_os_mutex_t* mini_os_mutex_create_static(mini_os_mutex_t* mutex, const char* name)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_FALSE, MINI_OS_TRUE) != MINI_OS_OK)
        return MINI_OS_NULL;
    return mutex;
}

/**
 * @brief Create a recursive mutex over caller-provided storage
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @param[in] mutex storage for the mutex descriptor
 * @return mutex handle on success; MINI_OS_NULL on invalid arguments
 */
mini_os_mutex_t* mini_os_mutex_recuring_create_static(const char* name, mini_os_mutex_t* mutex)
{
    if (mutex == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_mutex_init(mutex, name, MINI_OS_TRUE, MINI_OS_TRUE) != MINI_OS_OK)
        return MINI_OS_NULL;
    return mutex;
}

/**
 * @brief Lock a mutex (priority inheritance on contention)
 * @param[in] mutex mutex to lock
 * @param[in] timeout_tick 0 = non-blocking, MINI_OS_WAIT_FOREVER = block until
 *            available, otherwise block for at most this many ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments or no
 *         thread context; MINI_OS_ERR_AGAIN when contested and non-blocking;
 *         MINI_OS_ERR_BUSY when the owner re-locks a non-recursive mutex or the
 *         recursion depth overflows; MINI_OS_ERR_TIMEOUT when the wait expired
 * @details
 *  - uncontended: the unit is taken and ownership recorded inside the critical
 *    section, no scheduling happens at all
 *  - owned by the caller: a recursive mutex deepens depth, a non-recursive one
 *    fails fast with MINI_OS_ERR_BUSY
 *  - contested with a timeout: the caller's own priority is pushed as extra
 *    (it is not on the wait list yet) and then it parks on the semaphore,
 *    which parks atomically and hands the unit straight over on wake
 * @note on a timeout the boost only this waiter justified is dropped again; the
 *       parked_on pointer is compared instead of dereferenced, so a mutex that
 *       was kill-deleted under us cannot be used after free
 */
mini_os_err_t mini_os_mutex_lock(mini_os_mutex_t* mutex, mini_os_tick_t timeout_tick)
{
    mini_os_thread_t* current;
    mini_os_mutex_t*  parked_on;
    mini_os_err_t     ret;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_current();
    if (current == MINI_OS_NULL)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_INVAL; /* ownership needs a thread context */
    }
    if (mutex->semaphore.count > 0u)
    {
        /* uncontended: acquire inside this critical section */
        mutex->semaphore.count--;
        mutex->owner = current;
        mutex->depth = 1u;
        mini_os_list_tail(&mutex->hold_node, &current->hold_list);
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    if (mutex->owner == current)
    {
        if (mutex->is_recuring == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_BUSY; /* non-recursive owner re-lock: fail fast */
        }
        if (mutex->depth == MINI_OS_UINT8_MAX)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_BUSY; /* recursion overflow */
        }
        mutex->depth++;
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    if (timeout_tick == 0)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_AGAIN; /* contested and non-blocking */
    }
    if (mutex->owner != MINI_OS_NULL)
    {
        /* about to park: the owner has to run at least as fast as we do, and
         * the requirement travels on when the owner is itself blocked. Our own
         * priority is passed as extra because we are not on the wait list yet */
        current->wait_mutex = mutex;
        mini_os_mutex_propagate(mutex->owner, current->priority, 0u);
    }
    mini_os_irq_restore(irq);

    /* the take parks atomically and, on success, the unit was handed over directly */
    ret = mini_os_semaphore_take(&mutex->semaphore, timeout_tick);

    irq = mini_os_irq_save();
    parked_on = current->wait_mutex;
    current->wait_mutex = MINI_OS_NULL;
    if (ret == MINI_OS_OK)
    {
        mutex->owner = current;
        mutex->depth = 1u;
        mini_os_list_tail(&mutex->hold_node, &current->hold_list);
        /* waiters that parked behind us boosted the previous owner, not us:
         * fold their requirement (and that of our other held mutexes) in now */
        mini_os_mutex_propagate(current, MINI_OS_PRIORITY, 0u);
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    if (ret == MINI_OS_ERR_TIMEOUT && parked_on == mutex && mutex->owner != MINI_OS_NULL)
    {
        /* we left the wait set, so drop the boost only we justified. parked_on
         * is MINI_OS_NULL when the mutex was kill-deleted under us: comparing
         * pointers keeps us from dereferencing a freed descriptor */
        mini_os_mutex_propagate(mutex->owner, MINI_OS_PRIORITY, 0u);
    }
    mini_os_irq_restore(irq);
    return ret;
}

/**
 * @brief Unlock a mutex (owner only)
 * @param[in] mutex mutex to unlock
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when not called by the owner;
 *         MINI_OS_ERR_BUSY when the embedded semaphore give fails
 * @details a recursive lock is released one level per call; the final level
 *          leaves the owner's hold list, settles the owner on the priority the
 *          mutexes it keeps holding still require (base when this was the last
 *          one) and then hands the unit to the oldest waiter or republishes it
 */
mini_os_err_t mini_os_mutex_unlock(mini_os_mutex_t* mutex)
{
    mini_os_thread_t* current;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_current();
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
    mini_os_mutex_propagate(current, MINI_OS_PRIORITY, 0u);
    mini_os_irq_restore(irq);

    /* hands the unit to the oldest waiter (and yields) or republishes it */
    return mini_os_semaphore_give(&mutex->semaphore);
}

/**
 * @brief Try-lock a mutex from ISR context (never blocks, never boosts)
 * @param[in] mutex mutex to lock
 * @return MINI_OS_OK on success (ownership attributed to the interrupted
 *         thread); MINI_OS_ERR_INVAL on invalid arguments or no interrupted
 *         thread; MINI_OS_ERR_AGAIN when contested; MINI_OS_ERR_BUSY on a
 *         non-recursive owner re-lock or a recursion overflow
 */
mini_os_err_t mini_os_mutex_lock_isr(mini_os_mutex_t* mutex)
{
    mini_os_thread_t* current;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_current();
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
        if (mutex->is_recuring == MINI_OS_FALSE || mutex->depth == MINI_OS_UINT8_MAX)
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

/**
 * @brief Unlock a mutex from ISR context (the interrupted thread must own it)
 * @param[in] mutex mutex to unlock
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when the interrupted thread
 *         is not the owner; MINI_OS_ERR_BUSY when the semaphore give fails
 * @note wakes the oldest waiter but never triggers the context switch itself:
 *       the ISR caller decides through the is_heigher_priority pattern
 */
mini_os_err_t mini_os_mutex_unlock_isr(mini_os_mutex_t* mutex)
{
    mini_os_thread_t* current;
    mini_os_irq_t     irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    current = mini_os_thread_current();
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
    mini_os_mutex_propagate(current, MINI_OS_PRIORITY, 0u);
    mini_os_irq_restore(irq);

    /* no yield here: the ISR caller decides via the is_heigher_priority pattern */
    return mini_os_semaphore_give_isr(&mutex->semaphore);
}

/**
 * @brief Allow the delete path to force-release this mutex
 * @param[in] mutex mutex to arm
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments
 */
mini_os_err_t mini_os_mutex_enable_kill(mini_os_mutex_t* mutex)
{
    mini_os_irq_t irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    mutex->kill_enable = MINI_OS_TRUE;
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Recompute a thread's effective priority from its base, its held mutexes
 *        and the mutex it waits on
 * @param[in] thread thread to recompute
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 * @note kernel API, called after base_priority changed: a holder keeps the boost
 *       its waiters still require and a thread blocked on a mutex pushes the
 *       change on to that mutex's owner
 */
mini_os_err_t mini_os_mutex_priority_recompute(mini_os_thread_t* thread)
{
    mini_os_irq_t irq;

    if (thread == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    irq = mini_os_irq_save();
    mini_os_mutex_propagate(thread, MINI_OS_PRIORITY, 0u);
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Force-release every mutex a disappearing thread still holds
 * @param[in] thread thread that is about to exit or be deleted
 * @return MINI_OS_TRUE when at least one parked waiter was released, so the
 *         caller should yield; MINI_OS_FALSE otherwise (also on MINI_OS_NULL)
 * @details every held mutex is left free (owner cleared, depth 0, unit
 *          republished) and its parked waiters are released with
 *          MINI_OS_ERR_TIMEOUT
 * @note the unit is deliberately not handed over to a waiter: the protected
 *       resource may be inconsistent after its owner disappeared. The walk
 *       captures the next pointer first because both helpers re-link nodes
 * @note never yields by itself, so it is safe inside a critical section
 */
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
 *         MINI_OS_ERR_NOTSUPP for the wrong variant; MINI_OS_ERR_BUSY while the
 *         mutex is held or waited on and kill is not enabled
 * @details the teardown order matters: the wrong-storage check, then the
 *          in-flight-unit check (count 0 with no owner and no waiter means a
 *          woken taker has not taken ownership yet, so freeing now would let it
 *          write a dead descriptor), then the kill path
 * @note with kill enabled the waiters are released with MINI_OS_ERR_TIMEOUT, the
 *          owner loses this mutex's requirement and the mutex is left free
 */
static mini_os_err_t mini_os_mutex_delete_common(mini_os_mutex_t* mutex, mini_os_bool_t is_static)
{
    mini_os_bool_t woken = MINI_OS_FALSE;
    mini_os_irq_t  irq;

    if (mutex == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if ((mutex->semaphore.is_static != MINI_OS_FALSE) != (is_static != MINI_OS_FALSE))
        return MINI_OS_ERR_NOTSUPP; /* wrong delete variant for this storage */

    irq = mini_os_irq_save();
    if (mutex->semaphore.count == 0u && mutex->owner == MINI_OS_NULL && mini_os_list_is_empty(&mutex->semaphore.wait_list))
    {
        /* unit in flight: already handed to a woken taker that has not taken
         * ownership yet, freeing now would leave it writing a dead descriptor */
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY;
    }
    if (!mini_os_list_is_empty(&mutex->semaphore.wait_list) || mutex->owner != MINI_OS_NULL)
    {
        mini_os_thread_t* owner;

        if (mutex->kill_enable == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_BUSY; /* held or waited: unlock/park out first */
        }
        woken = mini_os_mutex_kill_waiters(mutex);
        owner = mutex->owner;
        mini_os_list_remove(&mutex->hold_node);
        mutex->owner = MINI_OS_NULL;
        mutex->depth = 0u;
        if (owner != MINI_OS_NULL)
        {
            /* the force-released owner loses this mutex's requirement */
            mini_os_mutex_propagate(owner, MINI_OS_PRIORITY, 0u);
        }
    }
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&mutex->g_list_node);
#endif
    mini_os_irq_restore(irq);

    if (is_static != MINI_OS_FALSE)
        MINI_OS_MEMSET(mutex, 0, sizeof(mini_os_mutex_t)); /* never free caller storage */
    else
        (void)mini_os_free(mutex);
    if (woken != MINI_OS_FALSE)
        (void)mini_os_schedule_yield(); /* killed waiters may outrank the caller */
    return MINI_OS_OK;
}

/**
 * @brief Delete a heap-created mutex and free its memory
 * @param[in] mutex mutex to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_NOTSUPP for a static mutex; MINI_OS_ERR_BUSY while owned
 *         or waited on, unless kill is enabled
 */
mini_os_err_t mini_os_mutex_delete(mini_os_mutex_t* mutex) { return mini_os_mutex_delete_common(mutex, MINI_OS_FALSE); }

/**
 * @brief Delete a static mutex (clears the caller storage, never frees)
 * @param[in] mutex mutex to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_NOTSUPP for a heap mutex; MINI_OS_ERR_BUSY like
 *         mini_os_mutex_delete(), kill_enable honored
 */
mini_os_err_t mini_os_mutex_delete_static(mini_os_mutex_t* mutex) { return mini_os_mutex_delete_common(mutex, MINI_OS_TRUE); }

#if MINI_OS_FIND_BY_NAME
/**
 * @brief Find a mutex by name
 * @param[in] name name to look for
 * @return mutex handle on success; MINI_OS_NULL when name is MINI_OS_NULL or no
 *         mutex carries that name
 */
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
