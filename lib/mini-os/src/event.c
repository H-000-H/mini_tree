/**
 * @file event.c
 * @author H-000-H
 * @brief Event group implementation (32 event flags, OR/WHOLE wait semantics,
 *        blocking wait with wait-list wake-up and time-wheel timeout)
 * @note every waiter stores its expected mask on its own TCB (wait_mask), so
 *       several threads can wait for different masks on the same group at once
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include <event.h>

#include "minierr.h"
#include <list.h>
#include <obj.h>
#include <redef.h>
#include <schedule.h>
#include <thread.h>

#if MINI_OS_EVENT

mini_os_event_group_t* mini_os_event_group_create(mini_os_uint32_t event_id, mini_os_event_type_t type)
{
    mini_os_event_group_t* event_group = (mini_os_event_group_t*)mini_os_malloc(sizeof(mini_os_event_group_t));

    if (event_group == MINI_OS_NULL)
        return MINI_OS_NULL;
    mini_os_list_init(&event_group->wait_list);
    event_group->event = event_id;
    event_group->event_type = type;
    event_group->is_auto_clear = MINI_OS_TRUE;
    mini_os_obj_init(&event_group->obj, MINI_OS_TRUE);
    return event_group;
}

mini_os_event_group_t* mini_os_event_group_create_static(mini_os_event_group_t* event_group, mini_os_uint32_t event_id, mini_os_event_type_t type)
{
    if (event_group == MINI_OS_NULL)
        return MINI_OS_NULL;
    mini_os_list_init(&event_group->wait_list);
    event_group->event = event_id;
    event_group->event_type = type;
    event_group->is_auto_clear = MINI_OS_TRUE;
    mini_os_obj_init(&event_group->obj, MINI_OS_FALSE);
    return event_group;
}

/**
 * @brief Unlink every parked waiter with the wait killed
 * @param[in] event_group event group whose wait list is drained
 * @return MINI_OS_TRUE when at least one thread was moved back to the ready list
 * @details wait_status is set from the object's kill policy: with MINI_OS_ERR_DEAD a
 *          woken waiter re-checks the group at the top of its mini_os_event_group_wait()
 *          loop and finds the REJECT flag, with MINI_OS_ERR_TIMEOUT the park
 *          reports it directly, so no per-thread kill flag is needed
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_event_kill_waiters(mini_os_event_group_t* event_group)
{
    /* DEAD is re-checked by the caller, so its park must return OK for that to
     * happen; a TIMEOUT is reported by the park itself, so its park must fail */
    return mini_os_sync_wake_all(&event_group->wait_list, (mini_os_obj_kill_error(&event_group->obj) == MINI_OS_ERR_DEAD) ? MINI_OS_TRUE : MINI_OS_FALSE);
}

mini_os_err_t mini_os_event_group_delete(mini_os_event_group_t* event_group)
{
    mini_os_irq_t  irq;
    mini_os_bool_t killed = MINI_OS_FALSE;

    if (event_group == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (mini_os_obj_is_heap_owned(&event_group->obj) == MINI_OS_FALSE)
        return MINI_OS_ERR_NOTSUPP;

    irq = mini_os_irq_save();
    if (mini_os_obj_user_count(&event_group->obj) > 1u)
    {
        if (mini_os_obj_is_kill_enabled(&event_group->obj) == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_REJECT; /* users in flight: retry once they leave */
        }
        killed = mini_os_event_kill_waiters(event_group);
    }
    /* the wake above and the destroy request stay in the same critical section,
     * so no new user can slip in between them */
    (void)mini_os_obj_request_destroy(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    mini_os_irq_restore(irq);

    if (killed != MINI_OS_FALSE)
        (void)mini_os_schedule_yield(); /* killed waiters may outrank the caller */
    return MINI_OS_OK;
}

mini_os_err_t mini_os_event_group_enable_kill(mini_os_event_group_t* event_group)
{
    if (event_group == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    mini_os_obj_enable_kill(&event_group->obj);
    return MINI_OS_OK;
}

/**
 * @brief Check whether the current flags satisfy a mask
 * @param[in] event_group event group to evaluate
 * @param[in] mask mask to test
 * @return MINI_OS_TRUE when satisfied; MINI_OS_FALSE otherwise
 * @details WHOLE type requires every bit of mask to be set, OR type requires
 *          any bit of mask to be set
 */
static mini_os_bool_t mini_os_event_satisfied(const mini_os_event_group_t* event_group, mini_os_uint32_t mask)
{
    if (event_group->event_type == MINI_OS_EVENT_WHOLE_TYPE)
        return ((event_group->event & mask) == mask) ? MINI_OS_TRUE : MINI_OS_FALSE;
    return ((event_group->event & mask) != 0u) ? MINI_OS_TRUE : MINI_OS_FALSE;
}

/**
 * @brief Wake every waiter whose mask is now satisfied
 * @param[in] event_group event group whose wait list is scanned
 * @return MINI_OS_TRUE when at least one thread was moved back to the ready list
 * @details each satisfied waiter is unparked through mini_os_sync_wake_thread()
 *          with wait_status set, so its mini_os_sync_wait_park() returns MINI_OS_OK
 * @note bits are NOT consumed here even with is_auto_clear: each woken waiter
 *       re-checks the condition and consumes its own bits in
 *       mini_os_event_group_wait(), so a losing waiter can simply park again instead
 *       of missing the event
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_event_wake_satisfied(mini_os_event_group_t* event_group)
{
    mini_os_list_t*   node;
    mini_os_list_t*   next;
    mini_os_thread_t* thread;
    mini_os_bool_t    woken = MINI_OS_FALSE;

    for (node = event_group->wait_list.next; node != &event_group->wait_list; node = next)
    {
        next = node->next;
        thread = mini_os_container_of(node, mini_os_thread_t, wait_node);
        if (!mini_os_event_satisfied(event_group, thread->wait_mask))
            continue;
        mini_os_sync_wake_thread(thread, MINI_OS_TRUE);
        woken = MINI_OS_TRUE;
    }
    return woken;
}

/**
 * @brief Update the flags of a group and wake the satisfied waiters
 * @param[in,out] event_group event group to update
 * @param[in] event flags to apply (WHOLE replaces all flags, OR sets the bits)
 * @return MINI_OS_TRUE when at least one waiter was moved back to ready
 * @note the whole update runs with interrupts masked, so no waiter can miss the
 *       flags it was waiting for
 */
static mini_os_bool_t mini_os_event_apply(mini_os_event_group_t* event_group, mini_os_uint32_t event)
{
    mini_os_irq_t  irq;
    mini_os_bool_t woken;

    irq = mini_os_irq_save();
    if (event_group->event_type == MINI_OS_EVENT_WHOLE_TYPE)
        event_group->event = event; /* WHOLE: the flags are replaced */
    else
        event_group->event |= event;
    woken = mini_os_event_wake_satisfied(event_group);
    mini_os_irq_restore(irq);
    return woken;
}

mini_os_err_t mini_os_event_group_set(mini_os_event_group_t* event_group, mini_os_uint32_t event)
{
    mini_os_err_t  err;
    mini_os_bool_t woken;

    if (event_group == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&event_group->obj);
    if (err != MINI_OS_OK)
        return err;

    woken = mini_os_event_apply(event_group, event);
    mini_os_obj_release(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    if (woken)
        (void)mini_os_schedule_yield();
    return MINI_OS_OK;
}

mini_os_err_t mini_os_event_group_set_isr(mini_os_event_group_t* event_group, mini_os_uint32_t event)
{
    mini_os_err_t err;

    if (event_group == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&event_group->obj);
    if (err != MINI_OS_OK)
        return err;

    /* Wake only; the ISR caller ends with mini_os_schedule_yield_isr(), which
     * sets PendSV pending only when a more urgent thread became ready. */
    (void)mini_os_event_apply(event_group, event);
    mini_os_obj_release(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    return MINI_OS_OK;
}

mini_os_err_t mini_os_event_group_get(mini_os_event_group_t* event_group, mini_os_uint32_t* event)
{
    mini_os_err_t err;
    mini_os_irq_t irq;

    if (event_group == MINI_OS_NULL || event == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&event_group->obj);
    if (err != MINI_OS_OK)
        return err;

    irq = mini_os_irq_save();
    *event = event_group->event;
    mini_os_irq_restore(irq);
    mini_os_obj_release(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    return MINI_OS_OK;
}

mini_os_err_t mini_os_event_group_clear(mini_os_event_group_t* event_group, mini_os_uint32_t event)
{
    mini_os_err_t err;
    mini_os_irq_t irq;

    if (event_group == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&event_group->obj);
    if (err != MINI_OS_OK)
        return err;

    irq = mini_os_irq_save();
    event_group->event &= ~event;
    mini_os_irq_restore(irq);
    mini_os_obj_release(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    return MINI_OS_OK;
}

mini_os_err_t mini_os_event_group_set_auto_clear(mini_os_event_group_t* event_group, mini_os_bool_t is_auto_clear)
{
    mini_os_err_t err;
    mini_os_irq_t irq;

    if (event_group == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&event_group->obj);
    if (err != MINI_OS_OK)
        return err;

    irq = mini_os_irq_save();
    event_group->is_auto_clear = is_auto_clear;
    mini_os_irq_restore(irq);
    mini_os_obj_release(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    return MINI_OS_OK;
}

mini_os_err_t mini_os_event_group_wait(mini_os_event_group_t* event_group, mini_os_uint32_t mask, mini_os_tick_t timeout_tick, mini_os_uint32_t* out_event)
{
    mini_os_uint32_t deadline = 0;
    mini_os_err_t    err;

    if (event_group == MINI_OS_NULL || mask == 0u)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&event_group->obj);
    if (err != MINI_OS_OK)
        return err;
    if (timeout_tick > 0 && timeout_tick != (mini_os_tick_t)-1)
    {
        mini_os_tick_t now = 0;

        (void)mini_os_get_tick(&now);
        deadline = (mini_os_uint32_t)now + (mini_os_uint32_t)timeout_tick; /* strict total timeout */
    }

    for (;;)
    {
        mini_os_irq_t irq = mini_os_irq_save();

        if (mini_os_event_satisfied(event_group, mask))
        {
            if (out_event != MINI_OS_NULL)
                *out_event = event_group->event & mask; /* report before auto-clear consumes */
            if (event_group->is_auto_clear)
                event_group->event &= ~mask; /* consume the retrieved bits */
            mini_os_irq_restore(irq);
            err = MINI_OS_OK;
            break;
        }
        if (mini_os_obj_is_destroyed(&event_group->obj) != MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            err = MINI_OS_ERR_DEAD; /* destroy requested: the group is gone */
            break;
        }
        if (timeout_tick == 0)
        {
            mini_os_irq_restore(irq);
            err = MINI_OS_ERR_AGAIN;
            break;
        }
        if (mini_os_thread_get_current() == MINI_OS_NULL)
        {
            mini_os_irq_restore(irq);
            err = MINI_OS_ERR_INVAL; /* no thread context: only timeout 0 is supported */
            break;
        }
        /* Park on the wait list (wait_node) and, for a finite timeout, also in
         * the time wheel (list_node), inside the critical section so a set
         * event cannot be missed; park restores irq and yields. A retry parks
         * with the remaining time only, keeping the total timeout strict. */
        if (timeout_tick != (mini_os_tick_t)-1)
        {
            mini_os_uint32_t remain = mini_os_tick_until(deadline);

            if (remain == 0u)
            {
                mini_os_irq_restore(irq);
                err = MINI_OS_ERR_TIMEOUT;
                break;
            }
            if (mini_os_sync_wait_park(&event_group->wait_list, mask, (mini_os_tick_t)remain, irq, &event_group->obj, event_group, sizeof(mini_os_event_group_t)) != MINI_OS_OK)
            {
                err = MINI_OS_ERR_TIMEOUT; /* park restored irq on this path */
                break;
            }
        }
        else if (mini_os_sync_wait_park(&event_group->wait_list, mask, timeout_tick, irq, &event_group->obj, event_group, sizeof(mini_os_event_group_t)) != MINI_OS_OK)
        {
            err = MINI_OS_ERR_TIMEOUT; /* park restored irq on this path */
            break;
        }
    }
    mini_os_obj_release(&event_group->obj, event_group, sizeof(mini_os_event_group_t));
    return err;
}

#endif /* MINI_OS_EVENT */
