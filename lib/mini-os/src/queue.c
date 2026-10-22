/**
 * @file queue.c
 * @author H-000-H
 * @brief queue implementation
 * @note send/receive park on a wait list and, for a finite timeout, also in the
 *       time wheel; both are parked inside the caller's critical section, so no
 *       space/message event can be missed, and a retry re-parks with the
 *       remaining time only, which keeps the total timeout strict
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include <queue.h>

#include "minierr.h"
#include <memory.h>
#include <obj.h>
#include <redef.h>
#include <schedule.h>

/**
 * @brief Address of the message slot behind a slot index
 * @param[in] queue queue handle
 * @param[in] idx slot index (0 .. max_depth - 1)
 * @return pointer to the slot payload
 * @details the pool is contiguous, so a slot is pure index arithmetic and no
 *          per-slot bookkeeping is stored
 */
static mini_os_uint8_t* mini_os_queue_slot_at(mini_os_queue_t* queue, mini_os_uint8_t idx) { return (mini_os_uint8_t*)queue->msg_base + (mini_os_size_t)idx * queue->msg_size; }

/**
 * @brief Initialize a queue descriptor over an already-provided message pool
 * @param[in] queue descriptor to initialize
 * @param[in] name queue name (MINI_OS_NULL = empty name)
 * @param[in] msg_size message payload size in bytes
 * @param[in] depth maximum number of messages
 * @param[in] msg_base message pool storage (depth * msg_size bytes)
 * @param[in] heap_owned MINI_OS_TRUE when descriptor and pool came from the heap
 * @return MINI_OS_OK always
 * @note both wait lists become self-referencing sentinels, which is the empty
 *       state of the mini-os list convention
 */
static mini_os_err_t mini_os_queue_init(mini_os_queue_t* queue, const char* name, mini_os_uint16_t msg_size, mini_os_uint8_t depth, void* msg_base, mini_os_bool_t heap_owned)
{
    mini_os_set_name(queue->name, name, MINI_OS_QUEUE_NAME_LEN);
    queue->msg_size = msg_size;
    queue->max_depth = depth;
    queue->depth = 0;
    queue->write_idx = 0;
    queue->read_idx = 0;
    mini_os_obj_init(&queue->obj, heap_owned);
    queue->msg_base = msg_base;
    mini_os_list_init(&queue->send_list);
    mini_os_list_init(&queue->receive_list);
    return MINI_OS_OK;
}

/**
 * @brief Wake the oldest waiter of a wait list
 * @param[in] wait_list send or receive wait list of the queue
 * @return MINI_OS_TRUE when a thread was moved back to the ready list
 * @details the waiter is unparked through mini_os_sync_wake_thread() with
 *          wait_status set, so its mini_os_sync_wait_park() returns MINI_OS_OK
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_queue_wake_one(mini_os_list_t* wait_list)
{
    mini_os_thread_t* thread;

    if (mini_os_list_is_empty(wait_list))
        return MINI_OS_FALSE;
    thread = mini_os_container_of(wait_list->next, mini_os_thread_t, wait_node);
    mini_os_sync_wake_thread(thread, MINI_OS_TRUE);
    return MINI_OS_TRUE;
}

/**
 * @brief Unlink every waiter of one wait list with the wait killed
 * @param[in] report_dead result stored on each woken waiter: MINI_OS_TRUE leaves
 *            the re-check to the caller (park returns MINI_OS_OK so the send or
 *            receive loop reports MINI_OS_ERR_DEAD), MINI_OS_FALSE makes the park
 *            itself report MINI_OS_ERR_TIMEOUT
 * @param[in] wait_list send or receive wait list of the queue
 * @return MINI_OS_TRUE when at least one thread was moved back to the ready list
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_queue_kill_list(mini_os_bool_t report_dead, mini_os_list_t* wait_list)
{
    return mini_os_sync_wake_all(wait_list, report_dead);
}

/**
 * @brief Unlink every blocked sender and receiver of a queue
 * @param[in] queue queue whose wait lists are drained
 * @return MINI_OS_TRUE when at least one thread was moved back to the ready list
 * @details the object's kill policy is read once and shared by both wait lists
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_queue_kill_waiters(mini_os_queue_t* queue)
{
    mini_os_bool_t killed = MINI_OS_FALSE;
    /* DEAD is re-checked by the caller, so its park must return OK for that to
     * happen; a TIMEOUT is reported by the park itself, so its park must fail */
    mini_os_bool_t report_dead = (mini_os_obj_kill_error(&queue->obj) == MINI_OS_ERR_DEAD) ? MINI_OS_TRUE : MINI_OS_FALSE;

    if (mini_os_queue_kill_list(report_dead, &queue->send_list) != MINI_OS_FALSE)
        killed = MINI_OS_TRUE;
    if (mini_os_queue_kill_list(report_dead, &queue->receive_list) != MINI_OS_FALSE)
        killed = MINI_OS_TRUE;
    return killed;
}

/**
 * @brief Drop one reference of a queue, reclaiming the heap pool once the last
 *        in-flight user of a destroyed queue leaves
 * @param[in,out] queue queue whose reference is dropped
 * @details once the destroy request is set no new call can acquire, so a single
 *          remaining reference means we are the last user: the heap message pool
 *          goes now and the descriptor goes with the release below (obj_release
 *          reclaims it when the count reaches 0), so both outlive every in-flight
 *          call
 * @note caller-provided storage is never freed, only cleared by the descriptor
 *       reclaim, so the static message pool is left untouched
 */
static void mini_os_queue_release(mini_os_queue_t* queue)
{
    mini_os_irq_t irq = mini_os_irq_save();

    if (mini_os_obj_user_count(&queue->obj) == 1u && mini_os_obj_is_destroyed(&queue->obj) != MINI_OS_FALSE)
    {
        if (mini_os_obj_is_heap_owned(&queue->obj) != MINI_OS_FALSE && queue->msg_base != MINI_OS_NULL)
            (void)mini_os_free(queue->msg_base);
    }
    mini_os_irq_restore(irq);
    mini_os_obj_release(&queue->obj, queue, sizeof(mini_os_queue_t));
}

mini_os_queue_t* mini_os_queue_create(const char* name, mini_os_uint16_t msg_size, mini_os_uint8_t depth)
{
    mini_os_queue_t* queue;
    void*            pool;

    if (msg_size == 0 || depth == 0)
        return MINI_OS_NULL;

    queue = (mini_os_queue_t*)mini_os_malloc(sizeof(mini_os_queue_t));
    if (queue == MINI_OS_NULL)
        return MINI_OS_NULL;

    pool = mini_os_malloc((mini_os_size_t)depth * msg_size);
    if (pool == MINI_OS_NULL)
    {
        mini_os_free(queue);
        return MINI_OS_NULL;
    }

    mini_os_queue_init(queue, name, msg_size, depth, pool, MINI_OS_TRUE);
    return queue;
}

mini_os_queue_t* mini_os_queue_create_static(const char* name, mini_os_uint16_t msg_size, mini_os_uint8_t depth, mini_os_queue_t* queue_buffer, void* msg_buffer, mini_os_size_t buffer_size)
{
    if (msg_size == 0 || depth == 0 || queue_buffer == MINI_OS_NULL || msg_buffer == MINI_OS_NULL || buffer_size < (mini_os_size_t)depth * msg_size)
        return MINI_OS_NULL;

    mini_os_queue_init(queue_buffer, name, msg_size, depth, msg_buffer, MINI_OS_FALSE);
    return queue_buffer;
}

/**
 * @brief Shared delete path of the heap and static variants
 * @param[in] queue queue to delete
 * @param[in] is_static MINI_OS_TRUE for caller storage (clear only), MINI_OS_FALSE
 *            for heap storage (free)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_NOTSUPP for the wrong variant; MINI_OS_ERR_REJECT while API
 *         calls are still in flight and kill is not enabled
 * @details the destroy request is set inside the same critical section as the
 *          kill wake, so no new call can slip in between them. When other users
 *          are still in flight the descriptor and the heap message pool are
 *          handed to the last one that leaves (mini_os_queue_release), so they
 *          outlive every in-flight call; only the sole-user case reclaims both
 *          right away
 * @note with kill enabled the blocked senders/receivers are unlinked and their
 *          send/receive reports MINI_OS_ERR_DEAD
 */
static mini_os_err_t mini_os_queue_delete_common(mini_os_queue_t* queue, mini_os_bool_t is_static)
{
    mini_os_irq_t  irq;
    mini_os_bool_t killed = MINI_OS_FALSE;

    if (queue == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (mini_os_obj_is_static(&queue->obj) != is_static)
        return MINI_OS_ERR_NOTSUPP; /* the caller picked the other delete variant */

    irq = mini_os_irq_save();
    if (mini_os_obj_user_count(&queue->obj) > 1u)
    {
        if (mini_os_obj_is_kill_enabled(&queue->obj) == MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            return MINI_OS_ERR_REJECT; /* users in flight: retry once they leave */
        }
        killed = mini_os_queue_kill_waiters(queue);
    }
    /* sole user: obj_request_destroy reclaims the descriptor right away, so the
     * heap pool goes with it; with users still in flight the last one releases both */
    if (mini_os_obj_user_count(&queue->obj) == 1u && is_static == MINI_OS_FALSE && queue->msg_base != MINI_OS_NULL)
        (void)mini_os_free(queue->msg_base);
    (void)mini_os_obj_request_destroy(&queue->obj, queue, sizeof(mini_os_queue_t));
    mini_os_irq_restore(irq);

    if (killed != MINI_OS_FALSE)
        (void)mini_os_schedule_yield(); /* killed waiters may outrank the caller */
    return MINI_OS_OK;
}

mini_os_err_t mini_os_queue_delete(mini_os_queue_t* queue) { return mini_os_queue_delete_common(queue, MINI_OS_FALSE); }

mini_os_err_t mini_os_queue_delete_static(mini_os_queue_t* queue) { return mini_os_queue_delete_common(queue, MINI_OS_TRUE); }

mini_os_err_t mini_os_queue_enable_kill(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    mini_os_obj_enable_kill(&queue->obj);
    return MINI_OS_OK;
}

mini_os_err_t mini_os_queue_send(mini_os_queue_t* queue, const void* msg, mini_os_tick_t timeout_tick)
{
    mini_os_bool_t   woken;
    mini_os_err_t    err;
    mini_os_uint32_t deadline = 0;

    if (queue == MINI_OS_NULL || msg == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&queue->obj);
    if (err != MINI_OS_OK)
        return err;
    if (timeout_tick > 0 && timeout_tick != MINI_OS_WAIT_FOREVER)
    {
        mini_os_tick_t now = 0;

        (void)mini_os_get_tick(&now);
        deadline = (mini_os_uint32_t)now + (mini_os_uint32_t)timeout_tick; /* strict total timeout */
    }

    for (;;)
    {
        mini_os_irq_t irq = mini_os_irq_save();

        if (mini_os_obj_is_destroyed(&queue->obj) != MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            err = MINI_OS_ERR_DEAD; /* destroy requested: the queue is gone */
            break;
        }
        if (queue->depth < queue->max_depth)
        {
            MINI_OS_MEMCPY(mini_os_queue_slot_at(queue, queue->write_idx), msg, queue->msg_size);
            queue->write_idx++;
            if (queue->write_idx >= queue->max_depth)
                queue->write_idx = 0;
            queue->depth++;
            woken = mini_os_queue_wake_one(&queue->receive_list);
            mini_os_irq_restore(irq);
            if (woken)
                (void)mini_os_schedule_yield();
            err = MINI_OS_OK;
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
         * the time wheel (list_node), inside the critical section so a space
         * event cannot be missed; park restores irq and yields. A retry parks
         * with the remaining time only, keeping the total timeout strict. */
        if (timeout_tick != MINI_OS_WAIT_FOREVER)
        {
            mini_os_uint32_t remain = mini_os_tick_until(deadline);

            if (remain == 0u)
            {
                mini_os_irq_restore(irq);
                err = MINI_OS_ERR_TIMEOUT;
                break;
            }
            if (mini_os_sync_wait_park(&queue->send_list, 0u, (mini_os_tick_t)remain, irq, &queue->obj, queue, sizeof(mini_os_queue_t)) != MINI_OS_OK)
            {
                err = MINI_OS_ERR_TIMEOUT; /* park restored irq on this path */
                break;
            }
        }
        else if (mini_os_sync_wait_park(&queue->send_list, 0u, timeout_tick, irq, &queue->obj, queue, sizeof(mini_os_queue_t)) != MINI_OS_OK)
        {
            err = MINI_OS_ERR_TIMEOUT; /* park restored irq on this path */
            break;
        }
    }
    mini_os_queue_release(queue);
    return err;
}

mini_os_err_t mini_os_queue_receive(mini_os_queue_t* queue, void* msg, mini_os_tick_t timeout_tick)
{
    mini_os_bool_t   woken;
    mini_os_err_t    err;
    mini_os_uint32_t deadline = 0;

    if (queue == MINI_OS_NULL || msg == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    err = mini_os_obj_acquire(&queue->obj);
    if (err != MINI_OS_OK)
        return err;
    if (timeout_tick > 0 && timeout_tick != MINI_OS_WAIT_FOREVER)
    {
        mini_os_tick_t now = 0;

        (void)mini_os_get_tick(&now);
        deadline = (mini_os_uint32_t)now + (mini_os_uint32_t)timeout_tick; /* strict total timeout */
    }

    for (;;)
    {
        mini_os_irq_t irq = mini_os_irq_save();

        if (mini_os_obj_is_destroyed(&queue->obj) != MINI_OS_FALSE)
        {
            mini_os_irq_restore(irq);
            err = MINI_OS_ERR_DEAD; /* destroy requested: the queue is gone */
            break;
        }
        if (queue->depth > 0)
        {
            MINI_OS_MEMCPY(msg, mini_os_queue_slot_at(queue, queue->read_idx), queue->msg_size);
            queue->read_idx++;
            if (queue->read_idx >= queue->max_depth)
                queue->read_idx = 0;
            queue->depth--;
            woken = mini_os_queue_wake_one(&queue->send_list);
            mini_os_irq_restore(irq);
            if (woken)
                (void)mini_os_schedule_yield();
            err = MINI_OS_OK;
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
         * the time wheel (list_node), inside the critical section so a message
         * event cannot be missed; park restores irq and yields. A retry parks
         * with the remaining time only, keeping the total timeout strict. */
        if (timeout_tick != MINI_OS_WAIT_FOREVER)
        {
            mini_os_uint32_t remain = mini_os_tick_until(deadline);

            if (remain == 0u)
            {
                mini_os_irq_restore(irq);
                err = MINI_OS_ERR_TIMEOUT;
                break;
            }
            if (mini_os_sync_wait_park(&queue->receive_list, 0u, (mini_os_tick_t)remain, irq, &queue->obj, queue, sizeof(mini_os_queue_t)) != MINI_OS_OK)
            {
                err = MINI_OS_ERR_TIMEOUT; /* park restored irq on this path */
                break;
            }
        }
        else if (mini_os_sync_wait_park(&queue->receive_list, 0u, timeout_tick, irq, &queue->obj, queue, sizeof(mini_os_queue_t)) != MINI_OS_OK)
        {
            err = MINI_OS_ERR_TIMEOUT; /* park restored irq on this path */
            break;
        }
    }
    mini_os_queue_release(queue);
    return err;
}

mini_os_err_t mini_os_queue_send_isr(mini_os_queue_t* queue, const void* msg)
{
    mini_os_irq_t irq;

    if (queue == MINI_OS_NULL || msg == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    if (queue->depth >= queue->max_depth)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_AGAIN; /* ISR path = timeout 0 path: never block */
    }
    MINI_OS_MEMCPY(mini_os_queue_slot_at(queue, queue->write_idx), msg, queue->msg_size);
    queue->write_idx++;
    if (queue->write_idx >= queue->max_depth)
        queue->write_idx = 0;
    queue->depth++;
    (void)mini_os_queue_wake_one(&queue->receive_list);
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

mini_os_err_t mini_os_queue_receive_isr(mini_os_queue_t* queue, void* msg)
{
    mini_os_irq_t irq;

    if (queue == MINI_OS_NULL || msg == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    if (queue->depth == 0)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_AGAIN; /* ISR path = timeout 0 path: never block */
    }
    MINI_OS_MEMCPY(msg, mini_os_queue_slot_at(queue, queue->read_idx), queue->msg_size);
    queue->read_idx++;
    if (queue->read_idx >= queue->max_depth)
        queue->read_idx = 0;
    queue->depth--;
    (void)mini_os_queue_wake_one(&queue->send_list);
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

mini_os_bool_t mini_os_queue_is_empty(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return MINI_OS_FALSE;
    return (queue->depth == 0) ? MINI_OS_TRUE : MINI_OS_FALSE;
}

mini_os_bool_t mini_os_queue_is_full(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return MINI_OS_FALSE;
    return (queue->depth == queue->max_depth) ? MINI_OS_TRUE : MINI_OS_FALSE;
}

mini_os_uint8_t mini_os_queue_get_depth(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return 0;
    return queue->depth;
}
