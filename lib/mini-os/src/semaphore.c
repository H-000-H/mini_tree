/**
 * @file semaphore.c
 * @author H-000-H
 * @brief semaphore implementation
 * @note a give hands the unit straight to the oldest parked taker instead of
 *       incrementing the count, so a woken take needs no retry loop and a
 *       binary semaphore can satisfy several parked takers in a row
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include <semaphore.h>

#include "minierr.h"
#include <list.h>
#include <memory.h>
#include <mini_config.h>
#include <obj.h>
#include <redef.h>
#include <schedule.h>
#include <thread.h>

#if MINI_OS_FIND_BY_NAME
/** @brief Global registry of every semaphore, used by mini_os_semaphore_find_by_name() */
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
 * @details the waiter is unparked through mini_os_sync_wake_thread() with
 *          wait_status set, so its mini_os_semaphore_take() returns MINI_OS_OK
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_semaphore_wake_one(mini_os_semaphore_t* semaphore)
{
    mini_os_thread_t* thread;

    if (mini_os_list_is_empty(&semaphore->wait_list))
        return MINI_OS_FALSE;
    thread = mini_os_container_of(semaphore->wait_list.next, mini_os_thread_t, wait_node);
    mini_os_sync_wake_thread(thread, MINI_OS_TRUE);
    return MINI_OS_TRUE;
}

/**
 * @brief Unlink every parked taker with the wait killed
 * @param[in] semaphore semaphore whose wait list is drained
 * @return MINI_OS_TRUE when at least one thread was moved back to the ready list
 * @details wait_status is set from the object's kill policy, so each parked
 *          mini_os_semaphore_take() reports whatever mini_os_obj_kill_error()
 *          promises
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_semaphore_kill_waiters(mini_os_semaphore_t* semaphore)
{
    /* DEAD is re-checked by the caller, so its park must return OK for that to
     * happen; a TIMEOUT is reported by the park itself, so its park must fail */
    return mini_os_sync_wake_all(&semaphore->wait_list, (mini_os_obj_kill_error(&semaphore->obj) == MINI_OS_ERR_DEAD) ? MINI_OS_TRUE : MINI_OS_FALSE);
}

/**
 * @brief Initialize a semaphore descriptor over already-provided storage
 * @param[in,out] semaphore semaphore to initialize
 * @param[in] max_count capacity of the semaphore (>= 1; 1 makes it a binary semaphore)
 * @param[in] count initial count (0 = unavailable at start, must be <= max_count)
 * @param[in] heap_owned MINI_OS_TRUE when the storage came from the heap
 * @param[in] name optional name, copied and NUL-terminated (MINI_OS_NULL = empty name)
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL on a NULL semaphore,
 *         max_count == 0 or count > max_count
 */
static mini_os_err_t mini_os_semaphore_init(mini_os_semaphore_t* semaphore, mini_os_uint16_t max_count, mini_os_uint16_t count, mini_os_bool_t heap_owned, const char* name)
{
    if (semaphore == MINI_OS_NULL || max_count == 0u || count > max_count)
        return MINI_OS_ERR_INVAL;

    mini_os_set_name(semaphore->name, name, MINI_OS_SEMAPHORE_NAME_LEN);
    semaphore->max_count = max_count;
    semaphore->count = count;
    mini_os_obj_init(&semaphore->obj, heap_owned);
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

mini_os_semaphore_t* mini_os_semaphore_create(const char* name, mini_os_uint16_t max_count, mini_os_uint16_t count)
{
    mini_os_semaphore_t* semaphore = (mini_os_semaphore_t*)mini_os_malloc(sizeof(mini_os_semaphore_t));

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, max_count, count, MINI_OS_TRUE, name) != MINI_OS_OK)
    {
        (void)mini_os_free(semaphore); /* init failed: give the block back, no leak */
        return MINI_OS_NULL;
    }
    return semaphore;
}

mini_os_semaphore_t* mini_os_binary_semaphore_create(const char* name)
{
    mini_os_semaphore_t* semaphore = (mini_os_semaphore_t*)mini_os_malloc(sizeof(mini_os_semaphore_t));

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, 1u, 1u, MINI_OS_TRUE, name) != MINI_OS_OK)
    {
        (void)mini_os_free(semaphore);
        return MINI_OS_NULL;
    }
    return semaphore;
}

mini_os_semaphore_t* mini_os_semaphore_create_static(const char* name, mini_os_uint16_t max_count, mini_os_uint16_t count, mini_os_semaphore_t* semaphore)
{
    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, max_count, count, MINI_OS_FALSE, name) != MINI_OS_OK)
        return MINI_OS_NULL;
    return semaphore;
}

mini_os_semaphore_t* mini_os_binary_semaphore_create_static(const char* name, mini_os_semaphore_t* semaphore)
{
    if (semaphore == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_semaphore_init(semaphore, 1u, 1u, MINI_OS_FALSE, name) != MINI_OS_OK)
        return MINI_OS_NULL;
    return semaphore;
}

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
 * @brief Shared delete path of the heap and static variants
 * @param[in] semaphore semaphore to delete
 * @param[in] is_static MINI_OS_TRUE for caller storage (clear only), MINI_OS_FALSE
 *            for heap storage (free)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL;
 *         MINI_OS_ERR_NOTSUPP for the wrong variant; MINI_OS_ERR_REJECT while an
 *         API call is in flight or takers are still parked and kill is not enabled
 * @details a kill wake and the destroy request stay in the same critical section,
 *          so no new take can slip in between them; the descriptor is reclaimed by
 *          the last in-flight user that leaves, so it outlives every in-flight
 *          call (heap storage freed, caller storage cleared)
 * @note with kill enabled the parked takers are unlinked and their
 *       mini_os_semaphore_take() reports MINI_OS_ERR_TIMEOUT
 */
static mini_os_err_t mini_os_semaphore_delete_common(mini_os_semaphore_t* semaphore, mini_os_bool_t is_static)
{
    mini_os_irq_t  irq;
    mini_os_bool_t killed = MINI_OS_FALSE;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (mini_os_obj_is_static(&semaphore->obj) != is_static)
        return MINI_OS_ERR_NOTSUPP; /* the caller picked the other delete variant */

    irq = mini_os_irq_save();
    if (mini_os_obj_user_count(&semaphore->obj) > 1u && mini_os_obj_is_kill_enabled(&semaphore->obj) == MINI_OS_FALSE)
    {
        /* an API call is in flight: destroying now would leave it operating on a
         * rejected descriptor, so refuse until it leaves */
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_REJECT;
    }
    if (!mini_os_list_is_empty(&semaphore->wait_list))
    {
        if (mini_os_obj_is_kill_enabled(&semaphore->obj) == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_REJECT; /* takers still parked: waking them is the caller's job */
        }
        killed = mini_os_semaphore_kill_waiters(semaphore);
    }
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&semaphore->g_list_node);
#endif
    (void)mini_os_obj_request_destroy(&semaphore->obj, semaphore, sizeof(mini_os_semaphore_t));
    mini_os_irq_restore(irq);

    if (killed != MINI_OS_FALSE)
        (void)mini_os_schedule_yield(); /* killed takers may outrank the caller */
    return MINI_OS_OK;
}

mini_os_err_t mini_os_semaphore_delete(mini_os_semaphore_t* semaphore) { return mini_os_semaphore_delete_common(semaphore, MINI_OS_FALSE); }

mini_os_err_t mini_os_semaphore_delete_static(mini_os_semaphore_t* semaphore) { return mini_os_semaphore_delete_common(semaphore, MINI_OS_TRUE); }

/**
 * @brief Allow the delete path to force parked takers out
 * @param[in,out] semaphore semaphore to arm
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when semaphore is MINI_OS_NULL
 */
mini_os_err_t mini_os_semaphore_enable_kill(mini_os_semaphore_t* semaphore)
{
    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    /* the take path re-checks nothing after the park, so the killed taker has to
     * be told by the park itself: arm the timeout flavour of the kill policy */
    mini_os_obj_enable_kill_reports_timeout(&semaphore->obj);
    return MINI_OS_OK;
}

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

mini_os_err_t mini_os_semaphore_take(mini_os_semaphore_t* semaphore, mini_os_tick_t timeout_tick)
{
    mini_os_err_t err;
    mini_os_err_t parked;
    mini_os_irq_t irq;

    if (semaphore == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&semaphore->obj);
    if (err != MINI_OS_OK)
        return err;

    irq = mini_os_irq_save();
    if (semaphore->count > 0u)
    {
        semaphore->count--;
        mini_os_irq_restore(irq);
        err = MINI_OS_OK;
    }
    else if (timeout_tick == 0)
    {
        mini_os_irq_restore(irq);
        err = MINI_OS_ERR_AGAIN;
    }
    else if (mini_os_thread_get_current() == MINI_OS_NULL)
    {
        mini_os_irq_restore(irq);
        err = MINI_OS_ERR_INVAL; /* no thread context: only timeout 0 is supported */
    }
    else
    {
        /* Park inside the critical section so a give cannot slip between the count
         * check and the park; park consumes the critical section and yields.
         * No retry loop and no deadline recomputation: MINI_OS_OK means the giver
         * handed the unit to this thread directly, so the count is not touched
         * again here  */
        parked = mini_os_sync_wait_park(&semaphore->wait_list, 0u, timeout_tick, irq, &semaphore->obj, semaphore, sizeof(mini_os_semaphore_t));
        err = (parked == MINI_OS_OK) ? MINI_OS_OK : ((parked == MINI_OS_ERR_TIMEOUT) ? MINI_OS_ERR_TIMEOUT : parked);
    }
    /* the reference is held across the whole call, so a destroy request cannot
     * reclaim the descriptor under a parked taker */
    mini_os_obj_release(&semaphore->obj, semaphore, sizeof(mini_os_semaphore_t));
    return err;
}

mini_os_err_t mini_os_semaphore_try_take(mini_os_semaphore_t* semaphore) { return mini_os_semaphore_take(semaphore, 0); }

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
mini_os_semaphore_t* mini_os_semaphore_find_by_name(const char* name)
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
