/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file event.h
 * @brief Event group definition
 */
#ifndef EVENT_H
#define EVENT_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include "list.h"
#include "memory.h"
#include "mini_config.h"
#include "redef.h"
#include "thread.h"

#if MINI_OS_EVENT

typedef enum mini_os_event_type
{
    MINI_OS_EVENT_WHOLE_TYPE = 0,
    MINI_OS_EVENT_OR_TYPE,
} mini_os_event_type_t;
typedef struct mini_os_event_group mini_os_event_group_t;
/**
 * @brief Event group definition
 * @note waiters are parked on wait_list via their wait_node; each waiter's
 *       expected mask lives on its TCB (wait_mask), so several threads can
 *       wait for different masks at once
 */
struct mini_os_event_group
{
    mini_os_uint32_t     event;         /**< Event flags */
    mini_os_list_t       wait_list;     /**< Wait list */
    mini_os_event_type_t event_type;    /**< the whole bit be used by event group or use only the 32 bit */
    mini_os_bool_t       is_auto_clear; /**< is clear by self */
    mini_os_bool_t       heap_owned;    /**< MINI_OS_TRUE when the descriptor came from the heap */
};

/**
 * @brief Create an event group
 * @param[in] event_id initial event flags
 * @param[in] type event group type (WHOLE or OR)
 * @return mini_os_event_group_t* Event group
 */
mini_os_event_group_t* mini_os_event_group_create(mini_os_uint32_t event_id, mini_os_event_type_t type);

/**
 * @brief Create an event group statically
 * @param[in,out] event_group Event group
 * @param[in] event_id initial event flags
 * @param[in] type event group type (WHOLE or OR)
 * @return mini_os_event_group_t* Event group
 */
mini_os_event_group_t* mini_os_event_group_create_static(mini_os_event_group_t* event_group, mini_os_uint32_t event_id, mini_os_event_type_t type);

/**
 * @brief Delete a heap-created event group and free its memory
 * @param[in] event_group Event group
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_BUSY while threads are still blocked on the group;
 *         MINI_OS_ERR_NOTSUPP for statically created groups
 * @note thread context only
 */
mini_os_err_t mini_os_event_group_delete(mini_os_event_group_t* event_group);

/**
 * @brief Set event group
 * @param[in,out] event_group Event group
 * @param[in] event Event flags
 * @return mini_os_err_t Error code
 * @note WHOLE type replaces all flags, OR type sets the given bits; every
 *       waiter whose mask becomes satisfied is woken; thread context only
 *       (triggers a yield on wake; use the _isr variant from ISR context)
 */
mini_os_err_t mini_os_event_set_group(mini_os_event_group_t* event_group, mini_os_uint32_t event);

/**
 * @brief Set event group from ISR context (never blocks, never yields)
 * @param[in,out] event_group Event group
 * @param[in] event Event flags
 * @return mini_os_err_t Error code
 * @note WHOLE type replaces all flags, OR type sets the given bits; wakes
 *       every satisfied waiter but does NOT trigger the context switch, so
 *       call mini_os_schedule_yield_isr() once at the end of the ISR (same
 *       pattern as the queue ISR API)
 */
mini_os_err_t mini_os_event_set_group_isr(mini_os_event_group_t* event_group, mini_os_uint32_t event);

/**
 * @brief Get event group
 * @param[in] event_group Event group
 * @param[out] event Event flags
 * @return mini_os_err_t Error code
 */
mini_os_err_t mini_os_event_get_group(mini_os_event_group_t* event_group, mini_os_uint32_t* event);

/**
 * @brief Clear event group
 * @param[in,out] event_group Event group
 * @param[in] event Event flags to clear
 * @return mini_os_err_t Error code
 */
mini_os_err_t mini_os_event_clear_group(mini_os_event_group_t* event_group, mini_os_uint32_t event);

/**
 * @brief Configure whether satisfied bits are auto-cleared
 * @param[in,out] event_group Event group
 * @param[in] is_auto_clear MINI_OS_TRUE = consume the satisfied bits on a
 *            successful wait (auto-clear); MINI_OS_FALSE = keep the bits and
 *            clear them manually with mini_os_event_clear_group()
 * @return mini_os_err_t Error code
 * @note both create functions default to auto-clear (MINI_OS_TRUE)
 */
mini_os_err_t mini_os_event_group_set_auto_clear(mini_os_event_group_t* event_group, mini_os_bool_t is_auto_clear);

/**
 * @brief Wait (blocking) for event flags of an event group
 * @param[in,out] event_group Event group
 * @param[in] mask event flags to wait for (> 0)
 * @param[in] timeout_tick 0 = non-blocking, ((mini_os_tick_t)-1) = block until
 *            the mask is satisfied, otherwise block up to timeout_tick ticks
 * @param[out] out_event on success receives the bits of mask that were set
 *            (MINI_OS_NULL when the caller does not need them)
 * @return MINI_OS_OK when the mask is satisfied; MINI_OS_ERR_INVAL on invalid
 *         arguments; MINI_OS_ERR_AGAIN when non-blocking and not satisfied;
 *         MINI_OS_ERR_TIMEOUT when the finite timeout expires
 * @note OR type: any bit of mask set satisfies the wait; WHOLE type: all bits
 *       of mask must be set; with auto-clear enabled the satisfied bits are
 *       consumed by the waiter
 */
mini_os_err_t mini_os_event_wait(mini_os_event_group_t* event_group, mini_os_uint32_t mask, mini_os_tick_t timeout_tick, mini_os_uint32_t* out_event);

#endif /* MINI_OS_EVENT */

#if defined(__cplusplus)
}
#endif

#endif
