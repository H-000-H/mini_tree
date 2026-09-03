/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @brief semaphore implementation
 * @file semaphore.c
 * @author H-000-H
 * @note a give hands the unit straight to the oldest parked taker instead of
 *       incrementing the count, so a woken take needs no retry loop and a
 *       binary semaphore can satisfy several parked takers in a row
 */
#include "semaphore.h"

#include "err.h"
#include "list.h"
#include "memory.h"
#include "mini_config.h"
#include "redef.h"
#include "schedule.h"
#include "thread.h"

#if MINI_OS_FIND_BY_NAME
/** @brief Global registry of every semaphore, used by mini_os_get_semaphore_by_name() */
static mini_os_list_t g_semaphore_list;

/**
 * @brief Initialize the global semaphore registry (constructor, runs before main)
 * @note the list head becomes a self-referencing sentinel, so semaphores can be
 *       linked before the scheduler is up
 */
MINI_OS_CONSTRUCTOR(MINI_OS_SEMAPHORE_REGISTRY_CONSTRUCTOR)
void mini_os_semaphore_registry_init(void) { mini_os_list_init(&g_semaphore_list); }
#endif

/**
 * @brief Wake the oldest waiter of a semaphore
 * @param[in] semaphore semaphore whose wait list is served
 * @return MINI_OS_TRUE when a thread was moved back to the ready list
 * @details the waiter leaves the wait list and, when it was parked in the time
 *          wheel for a timeout, the wheel as well; wait_done is set so its
 *          mini_os_semaphore_take() returns MINI_OS_OK
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_semaphore_wake_one(mini_os_semaphore_t* semaphore)
{
    mini_os_thread_t* thread;

    if (mini_os_list_is_empty(&semaphore->wait_list))
        return MINI_OS_FALSE;
    thread = mini_os_container_of(semaphore->wait_list.next, mini_os_thread_t, wait_node);
    mini_os_list_remove(&thread->wait_node);
    thread->wait_list = MINI_OS_NULL;
    thread->wait_done = MINI_OS_TRUE;
    if (thread->wheel_slot < MINI_OS_TICK_WHEEL)
    {
        /* timed sync wait: the thread is also parked in the time wheel */
        (void)mini_os_remove_thread_from_blocked_list(thread);
    }
    (void)mini_os_add_thread_to_ready_running_list(thread);
    return MINI_OS_TRUE;
}

/**
 * @brief Initialize a semaphore descriptor over already-provided storage
 * @param[in,out] semaphore semaphore to initialize
 * @param[in] max_count capacity of the semaphore (>= 1; 1 makes it a binary semaphore)
 * @param[in] count initial count (0 = unavailable at start, must be <= max_count)
 * @param[in] is_static MINI_OS_TRUE when the storage is caller-provided (not heap owned)
 * @param[in] name optional name, copied and NUL-terminated (MINI_OS_NULL = empty name)
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL on a NULL semaphore,
 *         max_count == 0 or count > max_count
 */
static mini_os_err_t mini_os_semaphore_init(mini_os_semaphore_t* semaphore, mini_os_uint16_t max_count, mini_os_uint16_t count, mini_os_bool_t is_static, const char* name)
{
    if (semaphore == MINI_OS_NULL || max_count == 0u || count > max_count)
        return MINI_OS_ERR_INVAL;

    mini_os_set_name(semaphore->name, name, MINI_OS_SEMAPHORE_NAME_LEN);
    semaphore->max_count = max_count;
    semaphore->count = count;
    semaphore->is_static = is_static;
    mini_os_list_init(&semaphore->wait_list);

#if MINI_OS_FIND_BY_NAME
    {
        mini_os_irq_t irq = mini_os_irq_save();

        mini_os_list_init(&semaphore->g_list_node);
        mini_os_list_tail(&semaphore->g_list_node, &g_semaphore_list);
        mini_os_irq_restore(irq);
    }
#endif
    return MINI_OS_OK;
}

/**
 * @brief Create a counting semaphore on the heap
 * @param[in] name semaphore name (MINI_OS_NULL = unnamed)
 * @param[in] max_count capacity (>= 1; 1 makes it a binary semaphore)
 * @param[in] count initial count (<= max_count, 0 = unavailable at start)
 * @return semaphore handle on success; MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_semaphore_create(const char* name, mini_os_uint16_t max_count, mini_os_uint16_t count)
{
    mini_os_semaphore_t* semaphore = (mini_os_semaphore_t*)mini_os_malloc(sizeof(mini_os_semaphore_t));

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, max_count, count, MINI_OS_FALSE, name) != MINI_OS_OK)
    {
        (void)mini_os_free(semaphore); /* init failed: give the block back, no leak */
        return MINI_OS_NULL;
    }
    return semaphore;
}

/**
 * @brief Create a binary semaphore on the heap (max_count 1, created given)
 * @param[in] name semaphore name (MINI_OS_NULL = unnamed)
 * @return semaphore handle on success; MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_binary_semaphore_create(const char* name)
{
    mini_os_semaphore_t* semaphore = (mini_os_semaphore_t*)mini_os_malloc(sizeof(mini_os_semaphore_t));

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, 1u, 1u, MINI_OS_FALSE, name) != MINI_OS_OK)
    {
        (void)mini_os_free(semaphore);
        return MINI_OS_NULL;
    }
    return semaphore;
}

/**
 * @brief Create a counting semaphore over caller-provided storage
 * @param[in] name semaphore name (MINI_OS_NULL = unnamed)
 * @param[in] max_count capacity (>= 1; 1 makes it a binary semaphore)
 * @param[in] count initial count (<= max_count, 0 = unavailable at start)
 * @param[in] semaphore storage for the semaphore descriptor
 * @return semaphore handle on success; MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_semaphore_create_static(const char* name, mini_os_uint16_t max_count, mini_os_uint16_t count, mini_os_semaphore_t* semaphore)
{
    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, max_count, count, MINI_OS_TRUE, name) != MINI_OS_OK)
        return MINI_OS_NULL;
    return semaphore;
}

/**
 * @brief Create a binary semaphore over caller-provided storage (max_count 1, created given)
 * @param[in] name semaphore name (MINI_OS_NULL = unnamed)
 * @param[in] semaphore storage for the semaphore descriptor
 * @return semaphore handle on success; MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_binary_semaphore_create_static(const char* name, mini_os_semaphore_t* semaphore)
{
    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, 1u, 1u, MINI_OS_TRUE, name) != MINI_OS_OK)
        return MINI_OS_NULL;
    return semaphore;
}

/**
 * @brief Collapse a counting semaphore into a binary one (max_count = 1)
 * @param[in,out] semaphore semaphore to convert
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_BUSY unless the semaphore is saturated
 * @details only a saturated semaphore can be collapsed: count == max_count means
 *          every unit is home and nobody still owes a give, so shrinking the
 *          capacity to 1 cannot swallow an outstanding unit. A parked waiter is
 *          rejected too, because it implies count == 0, i.e. all units are held
 */
mini_os_err_t mini_os_semaphore_to_binary(mini_os_semaphore_t* semaphore)
{
    mini_os_err_t ret = MINI_OS_OK;
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    /* Only a saturated semaphore can be collapsed: count == max_count means
     * every unit is home and nobody still owes a give, so shrinking the
     * capacity to 1 cannot swallow an outstanding unit. A parked waiter is
     * rejected too (it implies count == 0,  all units are held). */
    if (semaphore->count != semaphore->max_count || !mini_os_list_is_empty(&semaphore->wait_list))
    {
        ret = MINI_OS_ERR_BUSY;
    }
    else
    {
        semaphore->max_count = 1u;
        semaphore->count = 1u;
    }
    mini_os_irq_restore(irq);
    return ret;
}

/**
 * @brief Widen a binary semaphore into a counting one
 * @param[in,out] semaphore semaphore to convert
 * @param[in] max_count new capacity (>= 2)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL
 *         or max_count < 2; MINI_OS_ERR_NOTSUPP when the semaphore is not binary
 * @note widening can never swallow a unit, so the count is left as it is: an
 *       outstanding one (count 0) stays owed by its holder, a published one
 *       (count 1) stays available to the next taker. Parked waiters are not
 *       woken by the widening itself, only by the next give
 */
mini_os_err_t mini_os_semaphore_to_counting(mini_os_semaphore_t* semaphore, mini_os_uint16_t max_count)
{
    mini_os_err_t ret = MINI_OS_OK;
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL || max_count < 2u)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    if (semaphore->max_count != 1u)
    {
        ret = MINI_OS_ERR_NOTSUPP;
    }
    else
    {
        /* Widening can never swallow a unit, so the count is left as it is: an
         * outstanding one (count 0) stays owed by its holder, a published one
         * (count 1) stays available to the next taker. */
        semaphore->max_count = max_count;
    }
    mini_os_irq_restore(irq);
    return ret;
}

/**
 * @brief Delete a heap-created semaphore and free its memory
 * @param[in] semaphore semaphore to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_NOTSUPP for caller-provided storage; MINI_OS_ERR_BUSY while
 *         threads are still parked on it
 * @note waking parked threads is the caller's job, so the descriptor is never
 *       pulled from under a waiter
 */
mini_os_err_t mini_os_semaphore_delete(mini_os_semaphore_t* semaphore)
{
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (semaphore->is_static != MINI_OS_FALSE)
        return MINI_OS_ERR_NOTSUPP; /* caller-provided storage: use delete_static */

    irq = mini_os_irq_save();
    if (!mini_os_list_is_empty(&semaphore->wait_list))
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY; /* threads still parked: waking them is the caller's job */
    }
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&semaphore->g_list_node);
#endif
    mini_os_irq_restore(irq);

    (void)mini_os_free(semaphore);
    return MINI_OS_OK;
}

/**
 * @brief Delete a static semaphore (clears the caller storage, never frees)
 * @param[in] semaphore semaphore to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_NOTSUPP for heap storage; MINI_OS_ERR_BUSY while threads
 *         are still parked on it
 */
mini_os_err_t mini_os_semaphore_delete_static(mini_os_semaphore_t* semaphore)
{
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (semaphore->is_static == MINI_OS_FALSE)
        return MINI_OS_ERR_NOTSUPP; /* heap owned: use mini_os_semaphore_delete */

    irq = mini_os_irq_save();
    if (!mini_os_list_is_empty(&semaphore->wait_list))
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY;
    }
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&semaphore->g_list_node);
#endif
    /* never mini_os_free() here: the storage belongs to the caller, only clear it */
    MINI_OS_MEMSET(semaphore, 0, sizeof(mini_os_semaphore_t));
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Read the current count of a semaphore
 * @param[in] semaphore semaphore to query
 * @param[out] count receives the current count
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_semaphore_get_count(mini_os_semaphore_t* semaphore, mini_os_uint16_t* count)
{
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL || count == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    *count = semaphore->count;
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Take a semaphore (consume a unit, park the caller when empty)
 * @param[in] semaphore semaphore to take
 * @param[in] timeout_tick 0 = do not wait, MINI_OS_WAIT_FOREVER = wait forever,
 *            otherwise the maximum number of ticks to wait
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL
 *         or there is no thread context for a blocking wait; MINI_OS_ERR_AGAIN
 *         when the count is 0 and timeout_tick is 0; MINI_OS_ERR_TIMEOUT when the
 *         wait expired
 * @details the park happens inside the caller's critical section, so a give
 *          cannot slip between the count check and the park
 * @note no retry loop and no deadline recomputation: MINI_OS_OK means the giver
 *       handed the unit to this thread directly (RT-Thread style), so the count
 *       is not touched again here
 */
mini_os_err_t mini_os_semaphore_take(mini_os_semaphore_t* semaphore, mini_os_tick_t timeout_tick)
{
    mini_os_err_t parked;
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    if (semaphore->count > 0u)
    {
        semaphore->count--;
        mini_os_irq_restore(irq);
        return MINI_OS_OK;
    }
    if (timeout_tick == 0)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_AGAIN;
    }
    if (mini_os_thread_current() == MINI_OS_NULL)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_INVAL; /* no thread context: only timeout 0 is supported */
    }
    /* Park inside the critical section so a give cannot slip between the count
     * check and the park; park consumes the critical section and yields.
     * No retry loop and no deadline recomputation: MINI_OS_OK means the giver
     * handed the unit to this thread directly, so the count is not touched
     * again here  */
    parked = mini_os_sync_wait_park(&semaphore->wait_list, 0u, timeout_tick, irq);
    if (parked == MINI_OS_OK)
        return MINI_OS_OK;
    return (parked == MINI_OS_ERR_TIMEOUT) ? MINI_OS_ERR_TIMEOUT : parked;
}

/**
 * @brief Try to take a semaphore without blocking (timeout_tick 0 equivalent)
 * @param[in] semaphore semaphore to take
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_AGAIN when the count is 0
 */
mini_os_err_t mini_os_semaphore_try_take(mini_os_semaphore_t* semaphore) { return mini_os_semaphore_take(semaphore, 0); }

/**
 * @brief Give a semaphore (hand the unit to the oldest waiter, else count++)
 * @param[in] semaphore semaphore to give
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_BUSY when saturated with no waiter
 * @details with a waiter parked the count stays untouched and, because the
 *          woken thread may outrank the caller, a yield is triggered
 */
mini_os_err_t mini_os_semaphore_give(mini_os_semaphore_t* semaphore)
{
    mini_os_bool_t woken;
    mini_os_irq_t  irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    woken = mini_os_semaphore_wake_one(semaphore);
    if (woken != MINI_OS_FALSE)
    {
        mini_os_irq_restore(irq);
        (void)mini_os_schedule_yield(); /* the woken thread may outrank the caller */
        return MINI_OS_OK;
    }
    /* No waiter: publish the unit for the next taker, up to the capacity. */
    if (semaphore->count >= semaphore->max_count)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY; /* saturated: the semaphore already holds max_count units */
    }
    semaphore->count++;
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Give a semaphore from ISR context (non-blocking, no context switch)
 * @param[in] semaphore semaphore to give
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_BUSY when saturated with no waiter
 * @note never triggers the context switch itself: call
 *       mini_os_schedule_yield_isr() once at the end of the ISR
 */
mini_os_err_t mini_os_semaphore_give_isr(mini_os_semaphore_t* semaphore)
{
    mini_os_bool_t woken;
    mini_os_irq_t  irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    woken = mini_os_semaphore_wake_one(semaphore);
    if (woken != MINI_OS_FALSE)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_OK; /* unit handed straight to the oldest waiter */
    }
    if (semaphore->count >= semaphore->max_count)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY;
    }
    semaphore->count++;
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

#if MINI_OS_FIND_BY_NAME
/**
 * @brief Find a semaphore by name
 * @param[in] name name to look for
 * @return semaphore handle on success; MINI_OS_NULL when name is MINI_OS_NULL or
 *         no semaphore carries that name
 */
mini_os_semaphore_t* mini_os_get_semaphore_by_name(const char* name)
{
    mini_os_list_t* node;

    if (name == MINI_OS_NULL)
        return MINI_OS_NULL;
    for (node = g_semaphore_list.next; node != &g_semaphore_list; node = node->next)
    {
        mini_os_semaphore_t* semaphore = mini_os_container_of(node, mini_os_semaphore_t, g_list_node);

        if (MINI_OS_STRCMP(semaphore->name, name) == 0)
            return semaphore;
    }
    return MINI_OS_NULL;
}
#endif
