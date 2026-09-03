/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file semaphore.h
 * @brief semaphore implementation
 * @author H-000-H
 */
#ifndef SEM_H
#define SEM_H
#include "err.h"
#if defined(__cplusplus)
extern "C"
{
#endif
#include "list.h"
#include "redef.h"
typedef struct mini_os_semaphore mini_os_semaphore_t;

/**
 * @brief Semaphore structure
 */
struct mini_os_semaphore
{
    char             name[MINI_OS_SEMAPHORE_NAME_LEN]; /**< Semaphore name */
    mini_os_uint16_t count;                            /**< Semaphore count */
    mini_os_uint16_t max_count;                        /**< Semaphore max count (capacity, >= 1; 1 = binary) */
    mini_os_bool_t   is_static;                        /**< Static semaphore flag */
    mini_os_list_t   wait_list;                        /**< List of threads waiting on the semaphore */
#if MINI_OS_FIND_BY_NAME
    mini_os_list_t g_list_node; /**< Node of the global by-name registry */
#endif
};

/**
 * @brief Convert a counting semaphore into a binary one (max_count collapsed to 1)
 * @param[in,out] semaphore Pointer to the semaphore structure
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL,
 *         MINI_OS_ERR_BUSY unless the semaphore is saturated (count == max_count)
 *         with nobody parked on it
 * @note only a saturated semaphore can be collapsed: while units are still held
 *       by threads, shrinking the capacity would make the holders' later gives
 *       fail on a full count and silently swallow those units. Give everything
 *       back first, then convert.
 */
mini_os_err_t mini_os_semaphore_to_binary(mini_os_semaphore_t* semaphore);

/**
 * @brief Convert a binary semaphore into a counting one (max_count widened)
 * @param[in,out] semaphore Pointer to the semaphore structure
 * @param[in] max_count New capacity (>= 2, a counting semaphore holds more than one unit)
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL or
 *         max_count < 2, MINI_OS_ERR_NOTSUPP when the semaphore is not binary
 *         (max_count != 1)
 * @note widening can never swallow a unit, so it is allowed with threads parked
 *       or with the single unit still held: count is left untouched and the
 *       holder's give stays accounted for. Parked waiters are not woken by the
 *       widening itself, only by the next give.
 */
mini_os_err_t mini_os_semaphore_to_counting(mini_os_semaphore_t* semaphore, mini_os_uint16_t max_count);

/**
 * @brief Create a counting semaphore
 * @param[in] name Semaphore name (MINI_OS_NULL = unnamed)
 * @param[in] max_count Capacity of the semaphore (>= 1; 1 makes it a binary semaphore)
 * @param[in] count Initial count (<= max_count, 0 = unavailable at start)
 * @return A pointer to the created semaphore, or MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_semaphore_create(const char* name, mini_os_uint16_t max_count, mini_os_uint16_t count);

/**
 * @brief Create a counting semaphore over caller-provided storage
 * @param[in] name Semaphore name (MINI_OS_NULL = unnamed)
 * @param[in] max_count Capacity of the semaphore (>= 1; 1 makes it a binary semaphore)
 * @param[in] count Initial count (<= max_count, 0 = unavailable at start)
 * @param[in] semaphore Pointer to the semaphore structure
 * @return A pointer to the created semaphore, or MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_semaphore_create_static(const char* name, mini_os_uint16_t max_count, mini_os_uint16_t count, mini_os_semaphore_t* semaphore);

/**
 * @brief Create a binary semaphore (max_count 1, created given)
 * @param[in] name Semaphore name (MINI_OS_NULL = unnamed)
 * @return A pointer to the created semaphore, or MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_binary_semaphore_create(const char* name);

/**
 * @brief Create a binary semaphore over caller-provided storage (max_count 1, created given)
 * @param[in] name Semaphore name (MINI_OS_NULL = unnamed)
 * @param[in] semaphore Pointer to the semaphore structure
 * @return A pointer to the created semaphore, or MINI_OS_NULL on failure
 */
mini_os_semaphore_t* mini_os_binary_semaphore_create_static(const char* name, mini_os_semaphore_t* semaphore);

/**
 * @brief Delete a semaphore
 * @param[in] semaphore Pointer to the semaphore structure
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL,
 *         MINI_OS_ERR_NOTSUPP for a static semaphore (use
 *         mini_os_semaphore_delete_static), MINI_OS_ERR_BUSY while threads
 *         are still parked on it
 */
mini_os_err_t mini_os_semaphore_delete(mini_os_semaphore_t* semaphore);

/**
 * @brief Delete a static semaphore (clears the caller-provided storage, never frees)
 * @param[in] semaphore Pointer to the semaphore structure
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL,
 *         MINI_OS_ERR_NOTSUPP for a heap semaphore (use mini_os_semaphore_delete),
 *         MINI_OS_ERR_BUSY while threads are still parked on it
 */
mini_os_err_t mini_os_semaphore_delete_static(mini_os_semaphore_t* semaphore);

/**
 * @brief Take a semaphore (consumes a published unit, parks the caller when empty)
 * @param[in] semaphore Pointer to the semaphore structure
 * @param[in] timeout_tick 0 = do not wait, MINI_OS_WAIT_FOREVER = wait forever,
 *            otherwise the maximum number of ticks to wait
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL or there
 *         is no thread context for a blocking wait, MINI_OS_ERR_AGAIN when the
 *         count is 0 and timeout_tick is 0, MINI_OS_ERR_TIMEOUT when the wait
 *         expired or the thread was unlinked by suspend/resume/delete
 * @note a woken take returns OK without touching the count: the giver handed
 *       the unit to this thread directly (RT-Thread style, no retry loop)
 */
mini_os_err_t mini_os_semaphore_take(mini_os_semaphore_t* semaphore, mini_os_tick_t timeout_tick);

/**
 * @brief Give a semaphore (hands the unit to the oldest waiter, else count++)
 * @param[in] semaphore Pointer to the semaphore structure
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL,
 *         MINI_OS_ERR_BUSY when saturated with no waiter (count already at
 *         max_count)
 * @note never blocks; with a waiter parked the count stays untouched, so a
 *       binary semaphore can satisfy several parked takers in a row
 */
mini_os_err_t mini_os_semaphore_give(mini_os_semaphore_t* semaphore);

/**
 * @brief Give a semaphore from ISR context (non-blocking, no context switch)
 * @param[in] semaphore Pointer to the semaphore structure
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL,
 *         MINI_OS_ERR_BUSY when saturated with no waiter (count already at
 *         max_count)
 * @note hands the unit straight to the oldest waiter or publishes it, never
 *       triggers the context switch itself: call mini_os_schedule_yield_isr()
 *       once at the end of the ISR (same pattern as the queue/event ISR APIs)
 */
mini_os_err_t mini_os_semaphore_give_isr(mini_os_semaphore_t* semaphore);

/**
 * @brief Try to take a semaphore without blocking (timeout_tick 0 equivalent)
 * @param[in] semaphore Pointer to the semaphore structure
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if semaphore is NULL,
 *         MINI_OS_ERR_AGAIN when the count is 0
 */
mini_os_err_t mini_os_semaphore_try_take(mini_os_semaphore_t* semaphore);

#if MINI_OS_FIND_BY_NAME
/**
 * @brief Get a semaphore by name
 * @param[in] name Semaphore name
 * @return A pointer to the semaphore, or MINI_OS_NULL if not found
 */
mini_os_semaphore_t* mini_os_get_semaphore_by_name(const char* name);
#endif

/**
 * @brief Get the count of a semaphore
 * @param[in] sem Pointer to the semaphore structure
 * @param[out] count Pointer to the variable that will receive the semaphore count
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL if sem or count is NULL
 */
mini_os_err_t mini_os_semaphore_get_count(mini_os_semaphore_t* semaphore, mini_os_uint16_t* count);

#if defined(__cplusplus)
}
#endif

#endif /* SEM_H */
