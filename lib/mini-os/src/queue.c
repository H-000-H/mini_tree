/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @brief queue implementation
 * @file queue.c
 * @author H-000-H
 * @note send/receive park on a wait list and, for a finite timeout, also in the
 *       time wheel; both are parked inside the caller's critical section, so no
 *       space/message event can be missed, and a retry re-parks with the
 *       remaining time only, which keeps the total timeout strict
 */
#include "queue.h"

#include "err.h"
#include "memory.h"
#include "redef.h"
#include "schedule.h"

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
    queue->heap_owned = heap_owned;
    queue->msg_base = msg_base;
    mini_os_list_init(&queue->send_list);
    mini_os_list_init(&queue->receive_list);
    return MINI_OS_OK;
}

/**
 * @brief Wake the oldest waiter of a wait list
 * @param[in] wait_list send or receive wait list of the queue
 * @return MINI_OS_TRUE when a thread was moved back to the ready list
 * @details the waiter leaves the wait list and, when it was parked in the time
 *          wheel for a timeout, the wheel as well; wait_done is set so its
 *          mini_os_sync_wait_park() returns MINI_OS_OK
 * @note caller must hold interrupts disabled
 */
static mini_os_bool_t mini_os_queue_wake_one(mini_os_list_t* wait_list)
{
    mini_os_thread_t* thread;

    if (mini_os_list_is_empty(wait_list))
        return MINI_OS_FALSE;
    thread = mini_os_container_of(wait_list->next, mini_os_thread_t, wait_node);
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
 * @brief Create a queue (descriptor and message pool from the heap)
 * @param[in] name queue name (MINI_OS_NULL for an empty name)
 * @param[in] msg_size message payload size in bytes (> 0)
 * @param[in] depth maximum number of messages (> 0)
 * @return queue handle on success; MINI_OS_NULL on invalid arguments or out of
 *         memory
 * @note either allocation can fail on its own, so the descriptor is given back
 *       when the pool allocation fails
 */
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

/**
 * @brief Create a queue over caller-provided storage
 * @param[in] name queue name (MINI_OS_NULL for an empty name)
 * @param[in] msg_size message payload size in bytes (> 0)
 * @param[in] depth maximum number of messages (> 0)
 * @param[out] queue_buffer descriptor storage
 * @param[out] msg_buffer message pool storage (>= depth * msg_size bytes)
 * @param[in] buffer_size size of msg_buffer in bytes
 * @return queue handle on success; MINI_OS_NULL on invalid arguments or a
 *         too-small buffer
 */
mini_os_queue_t* mini_os_queue_create_static(const char* name, mini_os_uint16_t msg_size, mini_os_uint8_t depth, mini_os_queue_t* queue_buffer, void* msg_buffer, mini_os_size_t buffer_size)
{
    if (msg_size == 0 || depth == 0 || queue_buffer == MINI_OS_NULL || msg_buffer == MINI_OS_NULL || buffer_size < (mini_os_size_t)depth * msg_size)
        return MINI_OS_NULL;

    mini_os_queue_init(queue_buffer, name, msg_size, depth, msg_buffer, MINI_OS_FALSE);
    return queue_buffer;
}

/**
 * @brief Delete a heap-created queue and free its memory
 * @param[in] queue queue handle
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_NOTSUPP for a static queue; MINI_OS_ERR_BUSY while threads
 *         are still blocked on it
 */
mini_os_err_t mini_os_queue_delete(mini_os_queue_t* queue)
{
    mini_os_irq_t irq;

    if (queue == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (!queue->heap_owned)
        return MINI_OS_ERR_NOTSUPP;

    irq = mini_os_irq_save();
    if (!mini_os_list_is_empty(&queue->send_list) || !mini_os_list_is_empty(&queue->receive_list))
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY;
    }
    mini_os_irq_restore(irq);

    mini_os_free(queue->msg_base);
    mini_os_free(queue);
    return MINI_OS_OK;
}

/**
 * @brief Send (copy) a message into the queue
 * @param[in] queue queue handle
 * @param[in] msg source buffer of msg_size bytes
 * @param[in] timeout_tick 0 = non-blocking, MINI_OS_WAIT_FOREVER = block until
 *            space is available, otherwise block for at most this many ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments or no
 *         thread context for a blocking wait; MINI_OS_ERR_AGAIN when
 *         non-blocking and the queue is full; MINI_OS_ERR_TIMEOUT when the
 *         finite timeout expires
 * @details the message is copied into the slot at write_idx (wrapping at
 *          max_depth), the depth is raised and the oldest receiver is woken.
 *          A finite timeout parks on the send wait list through wait_node and in
 *          the time wheel through list_node, inside the critical section, so a
 *          space event cannot be missed; a retry re-parks with the remaining
 *          time only, which keeps the total timeout strict
 * @note a woken receiver may outrank the sender, so a yield is triggered
 */
mini_os_err_t mini_os_queue_send(mini_os_queue_t* queue, const void* msg, mini_os_tick_t timeout_tick)
{
    mini_os_bool_t   woken;
    mini_os_uint32_t deadline = 0;

    if (queue == MINI_OS_NULL || msg == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (timeout_tick > 0 && timeout_tick != MINI_OS_WAIT_FOREVER)
    {
        mini_os_tick_t now = 0;

        (void)mini_os_get_tick(&now);
        deadline = (mini_os_uint32_t)now + (mini_os_uint32_t)timeout_tick; /* strict total timeout */
    }

    for (;;)
    {
        mini_os_irq_t irq = mini_os_irq_save();

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
                return MINI_OS_ERR_TIMEOUT;
            }
            if (mini_os_sync_wait_park(&queue->send_list, 0u, (mini_os_tick_t)remain, irq) != MINI_OS_OK)
                return MINI_OS_ERR_TIMEOUT;
        }
        else if (mini_os_sync_wait_park(&queue->send_list, 0u, timeout_tick, irq) != MINI_OS_OK)
        {
            return MINI_OS_ERR_TIMEOUT;
        }
    }
}

/**
 * @brief Receive (copy) the oldest message from the queue
 * @param[in] queue queue handle
 * @param[out] msg destination buffer of msg_size bytes
 * @param[in] timeout_tick 0 = non-blocking, MINI_OS_WAIT_FOREVER = block until
 *            a message is available, otherwise block for at most this many ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments or no
 *         thread context for a blocking wait; MINI_OS_ERR_AGAIN when
 *         non-blocking and the queue is empty; MINI_OS_ERR_TIMEOUT when the
 *         finite timeout expires
 * @details the message is copied out of the slot at read_idx (wrapping at
 *          max_depth), the depth is lowered and the oldest sender is woken.
 *          A finite timeout parks on the receive wait list through wait_node and
 *          in the time wheel through list_node, inside the critical section, so
 *          a message event cannot be missed; a retry re-parks with the remaining
 *          time only, which keeps the total timeout strict
 * @note a woken sender may outrank the receiver, so a yield is triggered
 */
mini_os_err_t mini_os_queue_receive(mini_os_queue_t* queue, void* msg, mini_os_tick_t timeout_tick)
{
    mini_os_bool_t   woken;
    mini_os_uint32_t deadline = 0;

    if (queue == MINI_OS_NULL || msg == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (timeout_tick > 0 && timeout_tick != MINI_OS_WAIT_FOREVER)
    {
        mini_os_tick_t now = 0;

        (void)mini_os_get_tick(&now);
        deadline = (mini_os_uint32_t)now + (mini_os_uint32_t)timeout_tick; /* strict total timeout */
    }

    for (;;)
    {
        mini_os_irq_t irq = mini_os_irq_save();

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
                return MINI_OS_ERR_TIMEOUT;
            }
            if (mini_os_sync_wait_park(&queue->receive_list, 0u, (mini_os_tick_t)remain, irq) != MINI_OS_OK)
                return MINI_OS_ERR_TIMEOUT;
        }
        else if (mini_os_sync_wait_park(&queue->receive_list, 0u, timeout_tick, irq) != MINI_OS_OK)
        {
            return MINI_OS_ERR_TIMEOUT;
        }
    }
}

/**
 * @brief Send (copy) a message into the queue from ISR context (non-blocking)
 * @param[in] queue queue handle
 * @param[in] msg source buffer of msg_size bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_AGAIN when the queue is full
 * @note behaves like mini_os_queue_send() with timeout 0: it never blocks and
 *       does not trigger the context switch, so call
 *       mini_os_schedule_yield_isr() once at the end of the ISR
 */
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

/**
 * @brief Receive (copy) the oldest message from the queue from ISR context
 * @param[in] queue queue handle
 * @param[out] msg destination buffer of msg_size bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_AGAIN when the queue is empty
 * @note behaves like mini_os_queue_receive() with timeout 0: it never blocks and
 *       does not trigger the context switch, so call
 *       mini_os_schedule_yield_isr() once at the end of the ISR
 */
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

/**
 * @brief Check whether a queue is empty
 * @param[in] queue queue handle
 * @return MINI_OS_TRUE when empty; MINI_OS_FALSE otherwise (also on MINI_OS_NULL)
 */
mini_os_bool_t mini_os_queue_is_empty(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return MINI_OS_FALSE;
    return (queue->depth == 0) ? MINI_OS_TRUE : MINI_OS_FALSE;
}

/**
 * @brief Check whether a queue is full
 * @param[in] queue queue handle
 * @return MINI_OS_TRUE when full; MINI_OS_FALSE otherwise (also on MINI_OS_NULL)
 */
mini_os_bool_t mini_os_queue_is_full(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return MINI_OS_FALSE;
    return (queue->depth == queue->max_depth) ? MINI_OS_TRUE : MINI_OS_FALSE;
}

/**
 * @brief Get the number of queued messages
 * @param[in] queue queue handle
 * @return message count; 0 on MINI_OS_NULL
 */
mini_os_uint8_t mini_os_queue_get_depth(mini_os_queue_t* queue)
{
    if (queue == MINI_OS_NULL)
        return 0;
    return queue->depth;
}
