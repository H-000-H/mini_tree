/**
 * @file schedule.h
 * @author H-000-H
 * @brief Scheduling functions
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef SCHEDULE_H
#define SCHEDULE_H
#include "err.h"
#ifdef __cplusplus
extern "C"
{
#endif
#include "list.h"
#include "mini_config.h"
#include "redef.h"
#include "thread.h"
/**
 * @brief Convert ticks to milliseconds
 * @param[in] ticks tick count
 * @return elapsed time in ms
 * @note works for any MINI_OS_DEFAULT_SYSTICK (integer math)
 */
#define MINI_OS_TICK_TO_MS(ticks) (((mini_os_uint32_t)(ticks) * 1000u) / MINI_OS_DEFAULT_SYSTICK)

/**
 * @brief Convert milliseconds to ticks
 * @param[in] ms time in milliseconds
 * @return tick count
 * @note works for any MINI_OS_DEFAULT_SYSTICK (integer math)
 */
#define MINI_OS_MS_TO_TICK(ms) (((mini_os_uint32_t)(ms) * MINI_OS_DEFAULT_SYSTICK) / 1000u)

extern mini_os_uint32_t g_priority; /**< ready/running bitmap: bit i set = priority i has a ready or running thread
                                       (smaller number = higher priority) */

extern mini_os_list_t g_ready_running_list[MINI_OS_PRIORITY]; /**< ready/running list head per priority (running
                                                                 threads stay linked) */
typedef struct mini_os_schedule mini_os_schedule_t;
/**
 * @brief Scheduling structure
 * @note only used by kernel
 */
struct mini_os_schedule
{
    mini_os_tick_t  init_tick;        /**< init tick count */
    mini_os_tick_t  remain_tick;      /**< remaining tick count */
    mini_os_uint8_t init_priority;    /**< initial priority */
    mini_os_uint8_t current_priority; /**< current priority */
};
/**
 * @brief Initialize the scheduler
 * @return 0 on success, negative error code on failure
 */
mini_os_err_t mini_os_schedule_init(void);
/**
 * @brief Start the scheduler
 * @return 0 on success, negative error code on failure
 */
mini_os_err_t mini_os_schedule_start(void);
/**
 * @brief Yield the current thread
 * @return 0 on success, negative error code on failure
 * @note only used by the kernel user cannot call this function directly
 */
mini_os_err_t mini_os_schedule_yield(void);
/**
 * @brief Yield from ISR context: switch to a woken thread only if it outranks
 *        the interrupted one
 * @return MINI_OS_OK always
 * @note call once at the end of an ISR that woke threads through an *_isr API
 *       (mini_os_queue_send_isr()/mini_os_event_set_group_isr()/...). It checks
 *       the ready bitmap itself and sets PendSV pending only when a numerically
 *       lower (more urgent) priority became ready, so the switch happens on ISR
 *       exit and an equal-or-lower priority wake costs nothing -- the generic
 *       form of FreeRTOS xHigherPriorityTaskWoken + portYIELD_FROM_ISR().
 * @note never blocks and never switches inside the ISR; the interrupted thread
 *       keeps running until the hardware exception return. With the BASEPRI
 *       critical-section policy the ISR must run at a priority number >=
 *       MINI_OS_IRQ_MAX_SYSCALL_PRIORITY, as with every other kernel API.
 */
mini_os_err_t mini_os_schedule_yield_isr(void);
/**
 * @brief Delay the current thread for 'ticks' ticks
 * @param[in] ticks number of ticks to delay (0 returns immediately)
 * @note kernel API, used by mini_os_thread_delay_tick()
 */
void mini_os_schedule_delay(mini_os_uint32_t ticks);
/**
 * @brief Add a thread to the ready/running queue
 * @param[in] thread The thread to add
 * @return 0 on success, negative error code on failure
 */
mini_os_err_t mini_os_add_thread_to_ready_running_list(mini_os_thread_t* thread);
/**
 * @brief Remove a thread from the ready/running queue
 * @param[in] thread The thread to remove
 * @return 0 on success, negative error code on failure
 */
mini_os_err_t mini_os_remove_thread_from_ready_running_list(mini_os_thread_t* thread);

/**
 * @brief Remove a thread from the time-wheel blocked list
 * @param[in] thread thread to remove (must be MINI_OS_THREAD_STATE_BLOCKED)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments
 * @note only unlinks the wheel node; the caller decides the next state
 *       (e.g. resume -> add to the ready/running list, delete -> free the TCB)
 */
mini_os_err_t mini_os_remove_thread_from_blocked_list(mini_os_thread_t* thread);

/**
 * @brief Park a thread in the time wheel for 'ticks' ticks (state -> BLOCKED)
 * @param[in] thread thread to park (must not be linked anywhere)
 * @param[in] ticks delay length in ticks (> 0)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments
 * @note caller must hold interrupts disabled
 */
mini_os_err_t mini_os_wheel_insert(mini_os_thread_t* thread, mini_os_uint32_t ticks);

/**
 * @brief Remaining ticks of a wheel-parked thread
 * @param[in] thread thread to query
 * @return remaining ticks; 0 when the thread is not parked in the wheel
 */
mini_os_uint32_t mini_os_wheel_remain(mini_os_thread_t* thread);

/**
 * @brief Park the current thread on a sync-object wait list with a timeout
 * @param[in] wait_list wait list of the sync object (queue/semaphore/event...)
 * @param[in] wait_mask expected event mask stored on the parked thread (event
 *            groups evaluate it on wake; pass 0 for objects without masks)
 * @param[in] timeout_tick ((mini_os_tick_t)-1) = wait forever, otherwise park
 *            in the time wheel for this many ticks
 * @param[in] irq_level IRQ level saved by the caller with mini_os_irq_save();
 *            the caller must have checked the wait condition while holding it
 * @return MINI_OS_OK when woken by an event; MINI_OS_ERR_TIMEOUT when the
 *         wheel timeout expired first; MINI_OS_ERR_INVAL on invalid arguments
 * @note the thread is parked via wait_node on the wait list and (timed case)
 *       via list_node in the time wheel; the wake side unlinks both, so the
 *       caller resumes immediately on the event with no polling
 * @note consumes the caller's critical section (restores irq_level itself)
 *       so the condition check and the park stay atomic; thread context only
 */
mini_os_err_t mini_os_sync_wait_park(mini_os_list_t* wait_list, mini_os_uint32_t wait_mask, mini_os_tick_t timeout_tick, mini_os_irq_t irq_level);

MINI_OS_STATIC_INLINE mini_os_uint8_t mini_os_get_highest_priority(void)
{
    mini_os_uint32_t group = g_priority;

    if (group == 0u)
        return (mini_os_uint8_t)MINI_OS_PRIORITY; /* no ready thread: out-of-range marker */
    return (mini_os_uint8_t)MINI_OS_CTZ(group);
}

#if MINI_OS_LONG_TIME
mini_os_err_t mini_os_get_tick_long_time(mini_os_uint32_t* tick, mini_os_uint32_t* overflow);
#endif

mini_os_err_t mini_os_get_tick(mini_os_tick_t* tick);

/**
 * @brief Remaining ticks until a deadline (tick-wrap safe)
 * @param[in] deadline absolute tick value (now + timeout captured at entry)
 * @return deadline - now; 0 when the deadline has been reached or passed
 * @note for retry loops with a strict total timeout: re-park with the returned
 *       remaining value instead of the original timeout
 */
mini_os_uint32_t mini_os_tick_until(mini_os_uint32_t deadline);

#if MINI_OS_THREAD_DEADLINE
/**
 * @brief DL admission control: reserve runtime/period for a new DL task
 * @param[in] runtime DL budget per period (ticks, > 0)
 * @param[in] period DL period (ticks, > 0)
 * @return MINI_OS_OK when admitted; MINI_OS_ERR_BUSY when the sum of the admitted
 *         bandwidths would exceed the DL cap (the "tax" of the DL class)
 * @note the reservation is released by mini_os_dl_bandwidth_release() on teardown;
 *       blocking a DL task does NOT release it
 */
mini_os_err_t mini_os_dl_bandwidth_reserve(mini_os_tick_t runtime, mini_os_tick_t period);

/**
 * @brief Release the DL bandwidth reserved by a task that is going away
 * @param[in] runtime DL budget per period used when it was admitted (ticks)
 * @param[in] period DL period used when it was admitted (ticks)
 */
void mini_os_dl_bandwidth_release(mini_os_tick_t runtime, mini_os_tick_t period);

/**
 * @brief DL deadline-overrun callback
 * @param[in] thread DL thread whose job did not finish before its deadline
 * @param[in] missed_deadline the absolute deadline that was not met
 * @param[in] param opaque value given to mini_os_dl_miss_hook_set()
 * @note invoked from the SysTick handler with interrupts masked: keep it short
 *       and never call a blocking kernel API from it
 */
typedef void (*mini_os_dl_miss_hook_t)(mini_os_thread_t* thread, mini_os_tick_t missed_deadline, void* param);

/**
 * @brief Register or clear the DL deadline-overrun callback
 * @param[in] hook callback, or MINI_OS_NULL to clear it
 * @param[in] param opaque value forwarded to the callback
 * @return MINI_OS_OK always
 * @note a DL thread is reported when its absolute deadline is reached while it is
 *       still runnable or throttled, i.e. it did not block to signal that the job
 *       finished. Each thread also counts its own misses in dl_miss_count.
 */
mini_os_err_t mini_os_dl_miss_hook_set(mini_os_dl_miss_hook_t hook, void* param);

/**
 * @brief DL job-completion callback
 * @param[in] thread DL thread that finished its job
 * @param[in] deadline absolute deadline of the job that just finished
 * @param[in] param opaque value given to mini_os_dl_finish_hook_set()
 * @note invoked from mini_os_deadline_job_finish() in thread context (not ISR), so
 *       a short non-blocking action or a log line is fine; keep it short
 */
typedef void (*mini_os_dl_finish_hook_t)(mini_os_thread_t* thread, mini_os_tick_t deadline, void* param);

/**
 * @brief Register or clear the DL job-completion callback
 * @param[in] hook callback, or MINI_OS_NULL to clear it
 * @param[in] param opaque value forwarded to the callback
 * @return MINI_OS_OK always
 * @note each thread also counts its finished jobs in dl_finish_count
 */
mini_os_err_t mini_os_dl_finish_hook_set(mini_os_dl_finish_hook_t hook, void* param);
#endif /* MINI_OS_THREAD_DEADLINE */

/**
 * @brief Initialize the SysTick timer
 * @param[in] ticks_per_ms Number of ticks per millisecond
 * @note
 *  - void mini_os_systick_init(mini_os_uint32_t ticks_per_ms)
 */
void mini_os_systick_init(mini_os_uint32_t ticks_per_ms);

/**
 * @brief SysTick interrupt handler (installed in the vector table)
 */
void mini_os_systick_handler(void);

#ifdef __cplusplus
}
#endif
#endif
