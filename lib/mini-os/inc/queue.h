/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file queue.h
 * @brief queue implementation (fixed-size messages, FIFO, blocking send/receive)
 * @author H-000-H
 */
#ifndef QUEUE_H
#define QUEUE_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include "list.h"
#include "mini_config.h"
#include "redef.h"
// clang-format off
typedef struct mini_os_queue mini_os_queue_t;

/**
 * @brief Structure representing a mini-os queue
 * @note messages live in a contiguous pool of max_depth * msg_size bytes;
 *       a slot is addressed by index arithmetic (heap + idx * msg_size),
 *       no per-slot bookkeeping is stored
 */
struct mini_os_queue
{
    char             name[MINI_OS_QUEUE_NAME_LEN]; /**< queue name */
    mini_os_uint16_t msg_size;                     /**< message payload size in bytes */
    mini_os_uint8_t  max_depth;                    /**< queue max depth (the maximum number of messages) */
    mini_os_uint8_t  depth;                        /**< queue depth (the number of messages in the queue) */
    mini_os_uint8_t  write_idx;                    /**< next slot index to write (wraps at max_depth) */
    mini_os_uint8_t  read_idx;                     /**< next slot index to read (wraps at max_depth) */
    mini_os_bool_t   heap_owned;                   /**< MINI_OS_TRUE when the descriptor and pool came from the heap */
    void*            msg_base;                     /**< message pool base (max_depth * msg_size bytes) */
    mini_os_list_t   send_list;                    /**< threads blocked while the queue is full (waiting to send) */
    mini_os_list_t   receive_list;                 /**< threads blocked while the queue is empty (waiting to receive) */
};
// clang-format on
/**
 * @brief Create a queue (descriptor and message pool from the heap)
 * @param[in] name queue name (MINI_OS_NULL for an empty name)
 * @param[in] msg_size message payload size in bytes (> 0)
 * @param[in] depth maximum number of messages (> 0)
 * @return queue handle on success; MINI_OS_NULL on invalid arguments or out of memory
 */
mini_os_queue_t* mini_os_queue_create(const char* name, mini_os_uint16_t msg_size, mini_os_uint8_t depth);

/**
 * @brief Create a queue over caller-provided static storage
 * @param[in] name queue name (MINI_OS_NULL for an empty name)
 * @param[in] msg_size message payload size in bytes (> 0)
 * @param[in] depth maximum number of messages (> 0)
 * @param[out] queue_buffer descriptor storage
 * @param[out] msg_buffer message pool storage (>= depth * msg_size bytes)
 * @param[in] buffer_size size of msg_buffer in bytes
 * @return queue handle on success; MINI_OS_NULL on invalid arguments or a too-small buffer
 * @note msg_buffer must remain valid for the queue lifetime
 */
mini_os_queue_t* mini_os_queue_create_static(const char* name, mini_os_uint16_t msg_size, mini_os_uint8_t depth, mini_os_queue_t* queue_buffer, void* msg_buffer, mini_os_size_t buffer_size);

/**
 * @brief Delete a heap-created queue and free its memory
 * @param[in] queue queue handle
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_BUSY while threads are still blocked on the queue;
 *         MINI_OS_ERR_NOTSUPP for statically created queues
 */
mini_os_err_t mini_os_queue_delete(mini_os_queue_t* queue);

/**
 * @brief Send (copy) a message into the queue
 * @param[in] queue queue handle
 * @param[in] msg source buffer of msg_size bytes
 * @param[in] timeout_tick 0 = non-blocking, MINI_OS_QUEUE_WAIT_FOREVER = block
 *            until space is available, otherwise block up to timeout_tick ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_AGAIN when non-blocking and the queue is full;
 *         MINI_OS_ERR_TIMEOUT when the finite timeout expires
 * @note a finite timeout wakes instantly once space is available (wait-list
 *       park); the time wheel enforces the timeout
 */
mini_os_err_t mini_os_queue_send(mini_os_queue_t* queue, const void* msg, mini_os_tick_t timeout_tick);

/**
 * @brief Receive (copy) the oldest message from the queue
 * @param[in] queue queue handle
 * @param[out] msg destination buffer of msg_size bytes
 * @param[in] timeout_tick 0 = non-blocking, MINI_OS_QUEUE_WAIT_FOREVER = block
 *            until a message is available, otherwise block up to timeout_tick ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_AGAIN when non-blocking and the queue is empty;
 *         MINI_OS_ERR_TIMEOUT when the finite timeout expires
 * @note a finite timeout wakes instantly once a message arrives (wait-list
 *       park); the time wheel enforces the timeout
 */
mini_os_err_t mini_os_queue_receive(mini_os_queue_t* queue, void* msg, mini_os_tick_t timeout_tick);

/**
 * @brief Send (copy) a message into the queue from ISR context (non-blocking)
 * @param[in] queue queue handle
 * @param[in] msg source buffer of msg_size bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_AGAIN when the queue is full
 * @note behaves exactly like mini_os_queue_send(queue, msg, 0): never blocks;
 *       does NOT trigger the context switch itself, so call
 *       mini_os_schedule_yield_isr() once at the end of the ISR
 */
mini_os_err_t mini_os_queue_send_isr(mini_os_queue_t* queue, const void* msg);

/**
 * @brief Receive (copy) the oldest message from the queue from ISR context (non-blocking)
 * @param[in] queue queue handle
 * @param[out] msg destination buffer of msg_size bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_AGAIN when the queue is empty
 * @note behaves exactly like mini_os_queue_receive(queue, msg, 0): never blocks;
 *       does NOT trigger the context switch itself, so call
 *       mini_os_schedule_yield_isr() once at the end of the ISR
 */
mini_os_err_t mini_os_queue_receive_isr(mini_os_queue_t* queue, void* msg);

/**
 * @brief Check if a queue is empty
 * @param[in] queue queue handle
 * @return MINI_OS_TRUE if empty, MINI_OS_FALSE otherwise (also on MINI_OS_NULL)
 */
mini_os_bool_t mini_os_queue_is_empty(mini_os_queue_t* queue);

/**
 * @brief Check if a queue is full
 * @param[in] queue queue handle
 * @return MINI_OS_TRUE if full, MINI_OS_FALSE otherwise (also on MINI_OS_NULL)
 */
mini_os_bool_t mini_os_queue_is_full(mini_os_queue_t* queue);

/**
 * @brief Get the number of queued messages
 * @param[in] queue queue handle
 * @return message count; 0 on MINI_OS_NULL
 */
mini_os_uint8_t mini_os_queue_get_depth(mini_os_queue_t* queue);

#if defined(__cplusplus)
}
#endif

#endif /* QUEUE_H */
