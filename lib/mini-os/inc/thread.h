/**
 * @file thread.h
 * @author H-000-H
 * @brief mini-os thread
 * @note
 *  - not support multi-core
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef MINI_OS_THREAD_H
#define MINI_OS_THREAD_H
#include "redef.h"
#if defined(__cplusplus)
extern "C"
{
#endif
#include "memory.h"
#include <list.h>
#include <mini_config.h>
#include <redef.h>
struct mini_os_semaphore;
struct mini_os_mutex;
// clang-format off
typedef struct mini_os_thread mini_os_thread_t;
/**
 * @brief Structure representing a mini-os thread
 */
typedef enum
{
    MINI_OS_THREAD_STATE_INIT,                                  /**< Thread is initialized */
    MINI_OS_THREAD_STATE_RUNNING,                               /**< Thread is running */
    MINI_OS_THREAD_STATE_READY,                                 /**< Thread is ready to run */
    MINI_OS_THREAD_STATE_SUSPENDED,                             /**< Thread is suspended */
    MINI_OS_THREAD_STATE_BLOCKED,                               /**< Thread is blocked */
    MINI_OS_THREAD_STATE_INVALID,                               /**< Thread is invalid */
    MINI_OS_THREAD_STATE_TERMINATED,                            /**< Thread is terminated */
} mini_os_thread_state_t;

#if MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE
/**
 * @brief Scheduling policy of a thread
 * @note NORMAL is priority + FIFO / round-robin; EDF orders a thread inside its
 *       own priority level by earliest deadline; DEADLINE is a class above every
 *       priority level with global EDF and CBS budget enforcement
 */
typedef enum
{
    MINI_OS_SCHED_NORMAL = 0, /**< priority + FIFO / round-robin */
#if MINI_OS_THREAD_EDF
    MINI_OS_SCHED_EDF,        /**< priority + earliest-deadline-first inside the level */
#endif
#if MINI_OS_THREAD_DEADLINE
    MINI_OS_SCHED_DEADLINE,   /**< own class above priority, global EDF + CBS */
#endif
} mini_os_sched_policy_t;
#endif

/**
 * @brief Structure representing a mini-os thread
 */
struct mini_os_thread
{

    void*                 sp;                                    /**< Stack pointer for the thread must in first position in tcp */
    char                  thread_name[MINI_OS_THREADS_NAME_LEN]; /**< Name of the thread */
    mini_os_list_t        list_node;                             /**< List node for the thread (ready/running list or time wheel) */
    mini_os_list_t        wait_node;                             /**< List node for sync-object wait lists (queue send/receive...) */
    mini_os_list_t*       wait_list;                             /**< Wait list wait_node is parked on (MINI_OS_NULL = no sync wait) */
    mini_os_bool_t        wait_done;                             /**< Sync wait satisfied by an event (MINI_OS_TRUE) or timed out */
    struct mini_os_mutex* wait_mutex;                            /**< Mutex this thread is blocked on (MINI_OS_NULL = no mutex wait) */
    void (*entry)(void*);                                        /**< Entry function for the thread */
    void*                  param;                                /**< Parameter for the entry function */
    void*                  stack_addr;                           /**< Stack address for the thread */
    mini_os_uint32_t       stack_size;                           /**< Stack size for the thread */
    mini_os_thread_state_t state;                                /**< State of the thread */
    mini_os_err_t          err;                                  /**< Error code for the thread */
    mini_os_uint8_t        priority;                             /**< Effective priority (inheritance may raise it) */
    mini_os_uint8_t        base_priority;                        /**< Priority requested by the creator/user, inheritance starts here */
    mini_os_list_t         hold_list;                            /**< Mutexes currently held, linked through mini_os_mutex::hold_node */
    mini_os_uint32_t       round;                                /**< time wheel round (revolutions until expiry) */
    mini_os_tick_t         resume_time;                          /**< remaining delay ticks captured at suspend (0 = no wheel wait pending) */
    mini_os_uint8_t wheel_slot;                                  /**< wheel slot while BLOCKED in the wheel; MINI_OS_TICK_WHEEL = not in wheel */
    mini_os_user_data_t user_data;                               /**< User data for the thread */
    void (*thread_cleanup)(void*);                               /**< Cleanup function for the thread */
#if MINI_OS_EVENT
    mini_os_uint32_t wait_mask;                                 /**< Expected event mask while parked on an event-group wait list */
#endif
#if MINI_OS_FIND_BY_NAME
    mini_os_list_t g_list_node;                                 /**< List node for the global thread-by-name registry */    
#endif
#if MINI_OS_THREAD_EDF || MINI_OS_THREAD_DEADLINE
    mini_os_sched_policy_t sched_policy;                        /**< Scheduling policy: NORMAL / EDF / DEADLINE */
    mini_os_tick_t         dl_deadline_time;                    /**< Absolute deadline in ticks (0 = none) */
#endif
#if MINI_OS_THREAD_DEADLINE
    mini_os_tick_t         dl_run_time;                         /**< DEADLINE: CPU budget per period (ticks) */
    mini_os_tick_t         dl_budget;                           /**< DEADLINE: budget left in the current period */
    mini_os_uint32_t       dl_budget_frac;                      /**< DEADLINE: fraction of a tick already spent (1/2^20 units) */
    mini_os_tick_t         dl_period;                           /**< DEADLINE: replenishment period (ticks) */
    mini_os_bool_t         dl_throttled;                        /**< DEADLINE: parked until the next period boundary */
    mini_os_bool_t         dl_job_done;                         /**< DEADLINE: current job finished via mini_os_deadline_job_finish() */
    struct mini_os_semaphore* dl_activation;                    /**< DEADLINE: per-task activation semaphore used by job_finish() */
    mini_os_uint32_t       dl_miss_count;                       /**< DEADLINE: deadlines missed so far */
    mini_os_uint32_t       dl_finish_count;                     /**< DEADLINE: jobs finished via mini_os_deadline_job_finish() */
    mini_os_uint32_t       dl_throttle_count;                   /**< DEADLINE: times the budget ran out (throttled) */
#endif
#if MINI_OS_TIME_SLICE
    mini_os_tick_t init_tick_num;                               /**< Initial tick for time-slice */
    mini_os_tick_t remain_tick;                                 /**< Remaining tick for time-slice */
#endif
#if MINI_OS_THREAD_DETACH
    mini_os_bool_t            is_detach;                        /**< enabled detached */
    mini_os_bool_t            is_terminated;                    /**< enabled terminated */
    void*                     exit_retval;                      /**< exit return value */
    struct mini_os_semaphore* join_wait_sem;                    /**< join wait semaphore (incomplete type pointer) */
#endif
// clang-format on
};

/* Stack alignment: keep the stack base and size 8-byte aligned  */
#define MINI_OS_STACK_ALIGN_SIZE 8u                                                                            /**< stack alignment in bytes (Cortex-M: 8) */
#define MINI_OS_STACK_ALIGN_UP(x) (((x) + (MINI_OS_STACK_ALIGN_SIZE - 1u)) & ~(MINI_OS_STACK_ALIGN_SIZE - 1u)) /**< round up to MINI_OS_STACK_ALIGN_SIZE */

/**
 * @brief Create a thread stack (pads any size up to the 8-byte granularity)
 * @param[in] size requested stack bytes (must be non-zero)
 * @param[in] stack caller-provided stack base, or MINI_OS_NULL to allocate from the heap
 * @param[out] out_aligned receives the required stack bytes (ALIGN_UP(size))
 * @return stack base (8-byte aligned) on success; MINI_OS_NULL on failure
 * @note
 *  - Whether creating statically or dynamically, you have to go through this function first.
 *  - Memory is 8-byte aligned.
 *  - A caller-provided stack must ALREADY be 8-byte aligned; an unaligned one
 *    is rejected with MINI_OS_NULL (no offset is applied).
 *  - If you don't use this function, you have to pass the stack yourself.
 */
MINI_OS_STATIC_INLINE mini_os_uint32_t* mini_os_stack_create(mini_os_size_t size, mini_os_uint32_t* stack, mini_os_size_t* out_aligned)
{
    mini_os_size_t aligned;

    if (out_aligned == MINI_OS_NULL || size == 0u)
        return MINI_OS_NULL;

    aligned = MINI_OS_STACK_ALIGN_UP(size);
    *out_aligned = aligned;

    if (stack == MINI_OS_NULL)
        return (mini_os_uint32_t*)mini_os_calloc(1u, aligned);
    if (((mini_os_size_t)stack & (MINI_OS_STACK_ALIGN_SIZE - 1u)) != 0u)
    {
        *out_aligned = 0u;
        return MINI_OS_NULL; /* caller-provided stack must be 8-byte aligned */
    }
    return stack;
}

/**
 * @brief Create a thread
 * @param[in] name Name of the thread
 * @param[in] stack_size Stack size for the thread
 * @param[in] priority Priority of the thread
 * @param[in] entry Entry function for the thread
 * @param[in] param Parameter for the entry function
 * @return mini_os_thread_t* on success, MINI_OS_NULL on failure
 * @note the thread is made ready immediately (INIT -> READY)
 */
mini_os_thread_t* mini_os_thread_create(const char* name, mini_os_uint32_t stack_size, mini_os_uint8_t priority, void (*entry)(void*), void* param);

/**
 * @brief Delete a thread
 * @param[in] thread Thread to delete
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL for a NULL target, the running
 *         thread or an already terminated one, MINI_OS_ERR_BUSY while a joiner
 *         has the corpse pinned
 * @note every mutex the target still owns is force-released through the mutex
 *       kill path: its parked waiters fail with MINI_OS_ERR_TIMEOUT and the
 *       guarded data may be left inconsistent, so unlocking properly before the
 *       delete is still the preferred way. Deleting a thread that only waits for
 *       a mutex is safe, the owner drops the inherited boost when it releases
 */
mini_os_err_t mini_os_thread_delete(mini_os_thread_t* thread);

/**
 * @brief Terminate the current thread (never returns)
 * @param[in] retval exit value, retrievable by a joiner
 * @note called automatically when a thread entry returns (via the wrapper);
 *       every mutex still owned is force-released (see mini_os_thread_delete),
 *       a joiner parked on this thread is woken, and the TCB is queued for
 *       reclamation by the idle thread
 */
MINI_OS_NO_RETURN void mini_os_thread_exit(void* retval);

/**
 * @brief Create a thread statically
 * @param[in] name Name of the thread
 * @param[in] stack_size Stack size for the thread
 * @param[in] priority Priority of the thread
 * @param[in] entry Entry function for the thread
 * @param[in] param Parameter for the entry function
 * @param[in] stack_buffer Stack buffer for the thread
 * @param[in] task_buffer Thread control block storage (mini_os_thread_t)
 * @return mini_os_thread_t* on success, MINI_OS_NULL on failure
 * @note the thread is made ready immediately (INIT -> READY)
 */
mini_os_thread_t* mini_os_thread_create_static(const char* name, mini_os_uint32_t stack_size, mini_os_uint8_t priority, void (*entry)(void*), void* param, mini_os_uint32_t* stack_buffer, mini_os_thread_t* task_buffer);

/**
 * @brief Delete a thread statically
 * @param[in] thread Thread to delete
 * @param[in] stack_buffer Stack buffer for the thread
 * @param[in] task_buffer Task buffer for the thread
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL for a NULL argument, the running
 *         thread or an already terminated one, MINI_OS_ERR_BUSY while a joiner has
 *         the corpse pinned
 * @note same teardown as mini_os_thread_delete() (held mutexes are force-released),
 *       only the caller-provided storage is cleared instead of freed
 */
mini_os_err_t mini_os_thread_delete_static(mini_os_thread_t* thread, mini_os_uint32_t* stack_buffer, mini_os_thread_t* task_buffer);

/**
 * @brief Find a thread by name
 * @param[in] name Name of the thread
 * @return mini_os_thread_t* on success, MINI_OS_NULL on failure
 */
#if MINI_OS_FIND_BY_NAME
mini_os_thread_t* mini_os_find_by_name(const char* name);
#endif

/**
 * @brief Yield the CPU to the scheduler
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_yield(void);

/**
 * @brief Suspend a thread
 * @param[in] thread Thread to suspend
 * @return mini_os_err_t on success, 0 on failure
 * @note
 *  - a READY/RUNNING thread is unlinked from the ready/running list
 *  - a BLOCKED thread is unlinked from its wait list; a wheel-parked one
 *    keeps the remaining delay in resume_time (frozen), a sync-object wait
 *    is canceled
 *  - suspending the current thread triggers a context switch
 */
mini_os_err_t mini_os_thread_suspend(mini_os_thread_t* thread);

/**
 * @brief Resume a suspended thread
 * @param[in] thread Thread to resume
 * @return mini_os_err_t on success, 0 on failure
 * @note
 *  - with a captured resume_time: re-parked in the time wheel (BLOCKED),
 *    the frozen delay continues exactly
 *  - otherwise: put back into the ready/running list
 */
mini_os_err_t mini_os_thread_resume(mini_os_thread_t* thread);

/**
 * @brief Get the current thread
 * @return mini_os_thread_t* on success, MINI_OS_NULL on failure
 */
mini_os_thread_t* mini_os_thread_current(void);

/**
 * @brief Delay for a specified number of ticks
 * @param[in] ticks Number of ticks to delay
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_delay_tick(mini_os_uint32_t ticks);

/**
 * @brief Delay for a specified number of milliseconds
 * @param[in] ms Number of milliseconds to delay
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_delay_ms(mini_os_uint32_t ms);

/**
 * @brief Delay until a specified ticks
 * @param[in] ticks absolute tick value to delay until
 * @return mini_os_err_t MINI_OS_OK on success; MINI_OS_ERR_INVAL outside
 *         thread context
 * @note returns immediately when the target tick has already passed
 *       (tick-wrap safe); thread context only
 */
mini_os_err_t mini_os_thread_delay_tick_until(mini_os_uint32_t ticks);

/**
 * @brief Set the name of a thread
 * @param[in] thread Thread to set the name for
 * @param[in] name Name to set
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_set_name(mini_os_thread_t* thread, const char* name);

/**
 * @brief Get the name of a thread
 * @param[in] thread Thread to get the name for
 * @param[out] name Buffer to store the name in
 * @param[in] name_len Length of the name buffer
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_get_name(mini_os_thread_t* thread, char* name, mini_os_uint32_t* name_len);

/**
 * @brief Set the priority of a thread
 * @param[in] thread Thread to set the priority for
 * @param[in] priority Priority to set
 * @return mini_os_err_t on success, 0 on failure
 * @note a READY/RUNNING thread is re-linked into the ready/running list under
 *       the new priority (bitmap updated, one critical section); BLOCKED or
 *       SUSPENDED threads only change the field
 * @note this changes the thread base priority. A thread boosted by mutex
 *       inheritance keeps the highest requirement of the mutexes it holds until
 *       the next inheritance event, then settles on max(this priority, waiters)
 */
mini_os_err_t mini_os_thread_set_priority(mini_os_thread_t* thread, mini_os_uint8_t priority);

/**
 * @brief Apply an effective priority without touching the base priority
 * @param[in] thread Thread to re-link
 * @param[in] priority Effective priority to apply
 * @return mini_os_err_t on success, 0 on failure
 * @note kernel API for mutex priority inheritance: base_priority is left alone
 *       so the next recompute can still derive the boost from it
 */
mini_os_err_t mini_os_thread_priority_apply(mini_os_thread_t* thread, mini_os_uint8_t priority);

/**
 * @brief Get the priority of a thread
 * @param[in] thread Thread to get the priority for
 * @param[out] priority Buffer to store the priority in
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_get_priority(mini_os_thread_t* thread, mini_os_uint8_t* priority);

/**
 * @brief Set the timeslice of a thread
 * @param[in] thread Thread to set the timeslice for
 * @param[in] tick Timeslice to set
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_set_timeslice(mini_os_thread_t* thread, mini_os_tick_t tick);

/**
 * @brief Get the timeslice of a thread
 * @param[in] thread Thread to get the timeslice for
 * @param[out] tick Buffer to store the timeslice in
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_get_timeslice(mini_os_thread_t* thread, mini_os_tick_t* tick);

/**
 * @brief Get the state of a thread
 * @param[in] thread Thread to get the state for
 * @param[out] state Buffer to store the state in
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_get_state(mini_os_thread_t* thread, mini_os_thread_state_t* state);

/**
 * @brief Set the user data of a thread
 * @param[in] thread Thread to set the user data for
 * @param[in] user_data User data to set
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_set_user_data(mini_os_thread_t* thread, mini_os_user_data_t user_data);

/**
 * @brief Get the user data of a thread
 * @param[in] thread Thread to get the user data for
 * @param[out] user_data Buffer to store the user data in
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_get_user_data(mini_os_thread_t* thread, mini_os_user_data_t* user_data);

/**
 * @brief Set the cleanup function of a thread
 * @param[in] thread Thread to set the cleanup function for
 * @param[in] cleanup Cleanup function to set
 * @param[in] arg Argument to pass to the cleanup function
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_set_cleanup(mini_os_thread_t* thread, void (*cleanup)(void*), void* arg);

#if MINI_OS_THREAD_DETACH
/**
 * @brief Detach a thread (hand its corpse back to the idle reaper)
 * @param[in] thread Thread to detach
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 * @note threads are created detached, so this is only needed to cancel the pin a
 *       join installed or to release a corpse a joiner abandoned
 */
mini_os_err_t mini_os_thread_detach(mini_os_thread_t* thread);

/**
 * @brief Join a thread: block until it terminates, then collect its return value
 * @param[in] thread Thread to join
 * @param[out] thread_return Buffer to store the thread return value in (MINI_OS_NULL = discard)
 * @param[in] timeout_tick 0 = poll, MINI_OS_WAIT_FOREVER = wait forever, otherwise
 *            the maximum number of ticks to wait
 * @return MINI_OS_OK when the thread terminated and the value was collected,
 *         MINI_OS_ERR_INVAL on a NULL target, a self-join or no thread context,
 *         MINI_OS_ERR_NOMEM when the join semaphore could not be created,
 *         MINI_OS_ERR_AGAIN when timeout_tick is 0 and the target still runs,
 *         MINI_OS_ERR_TIMEOUT when a timed wait expired
 * @note the join pins the corpse so the idle reaper cannot free the TCB while the
 *       return value is still needed; a successful join releases it again, a
 *       timed-out one restores the previous detach state. A poll (timeout_tick 0)
 *       and an already terminated target need neither the pin nor a semaphore.
 *       Only one joiner at a time is supported, and deleting a pinned thread fails
 *       with MINI_OS_ERR_BUSY.
 */
mini_os_err_t mini_os_thread_join(mini_os_thread_t* thread, void** thread_return, mini_os_tick_t timeout_tick);
#endif /* MINI_OS_THREAD_DETACH */

/**
 * @brief Get the idle thread handle
 * @return idle thread handle, MINI_OS_NULL before mini_os_thread_idle_create()
 *         succeeded
 */
mini_os_thread_t* mini_os_thread_get_idle_handle(void);

typedef void (*idle_hook_t)(void*);
/**
 * @brief Set the idle hook for a thread
 * @param[in] hook Idle hook function
 * @param[in] param Argument passed to the idle hook
 * @return mini_os_err_t on success, 0 on failure
 */
mini_os_err_t mini_os_thread_idle_hook(idle_hook_t hook, void* param);

/**
 * @brief Default idle thread body (runs the registered idle hook, then WFI)
 * @param[in] param argument passed to the idle hook
 */
void mini_os_thread_idle(void* param);

#if MINI_OS_THREAD_DEADLINE
/**
 * @brief Create a deadline thread
 * @param[in] name Thread name
 * @param[in] stack_size Thread stack size
 * @param[in] deadline Thread deadline
 * @param[in] period Thread period
 * @param[in] runtime Thread runtime
 * @param[in] entry Thread entry function
 * @param[in] parameter Thread entry parameter
 * @return mini_os_thread_t* on success, other on failure
 * @note The task body must end every period with mini_os_deadline_job_finish().
 *       Using mini_os_thread_delay_tick()/delay_ms()/delay_tick_until() as the
 *       period wait of a DL task is neither recommended nor allowed: only the
 *       kernel-provided finish hands out the next job (budget refill,
 *       dl_deadline_time += dl_period) and keeps the CBS accounting and the
 *       activation semaphore consistent.
 * @note This is not a dynamic-deadline scheduler: the kernel never changes a
 *       job's deadline except advancing it by exactly dl_period at each period
 *       boundary (creation fixes it at now + deadline). There is no deadline
 *       push-back, postponement or reclaiming-driven deadline adjustment.
 */
mini_os_thread_t *mini_os_deadline_thread_create(           const char *name, 
                                                            mini_os_uint32_t stack_size,
                                                            mini_os_tick_t deadline,
                                                            mini_os_tick_t period,
                                                            mini_os_tick_t runtime,
                                                            void( *entry)(void*),
                                                            void * const parameter);
                                                
/**
 * @brief Create a deadline thread with static stack
 * @param[in] name Thread name
 * @param[in] stack_size Thread stack size
 * @param[in] deadline Thread deadline
 * @param[in] period Thread period
 * @param[in] runtime Thread runtime
 * @param[in] entry Thread entry function
 * @param[in] parameter Thread entry parameter
 * @param[in] stack_buffer Thread stack buffer
 * @param[in] task_buffer Thread task buffer
 * @return mini_os_thread_t* on success, other on failure
 * @note The task body must end every period with mini_os_deadline_job_finish().
 *       Using mini_os_thread_delay_tick()/delay_ms()/delay_tick_until() as the
 *       period wait of a DL task is neither recommended nor allowed: only the
 *       kernel-provided finish hands out the next job (budget refill,
 *       dl_deadline_time += dl_period) and keeps the CBS accounting and the
 *       activation semaphore consistent.
 * @note This is not a dynamic-deadline scheduler: the kernel never changes a
 *       job's deadline except advancing it by exactly dl_period at each period
 *       boundary (creation fixes it at now + deadline). There is no deadline
 *       push-back, postponement or reclaiming-driven deadline adjustment.
 */
mini_os_thread_t *mini_os_deadline_thread_create_static(    const char *name, 
                                                            mini_os_uint32_t stack_size,
                                                            mini_os_tick_t deadline,
                                                            mini_os_tick_t period,
                                                            mini_os_tick_t runtime,
                                                            void( *entry)(void*),
                                                            void * const parameter,
                                                            mini_os_uint32_t* stack_buffer,
                                                            mini_os_thread_t* task_buffer);

/**
 * @brief Renegotiate the budget and period of a DL thread (bandwidth change)
 * @param[in] thread DL thread to reconfigure
 * @param[in] runtime new budget per period in ticks (0 < runtime <= period), or
 *            0 to keep the current runtime
 * @param[in] period new period in ticks (> 0), or 0 to keep the current period
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL for a NULL argument, a
 *         non-DL thread or invalid parameters; MINI_OS_ERR_BUSY while the thread
 *         is READY/RUNNING (contending) or when the new bandwidth does not fit
 *         the DL admission cap (the old reservation is kept)
 * @details The new runtime/period take effect when the next job starts: its
 *          budget is refilled with the new runtime and the deadline then advances
 *          by the new period. Pass 0 for either field to keep its current value,
 *          so a single value can be changed on its own (e.g. set_bandwidth(t, 4, 0)
 *          changes only the runtime). The relative deadline and the current
 *          absolute dl_deadline_time are NOT changed -- this is not a
 *          dynamic-deadline scheduler.
 * @note only allowed while the thread is not contending (BLOCKED or SUSPENDED),
 *       so no ready-list re-link and no wheel re-park is needed; the reservation
 *       is re-checked (old ticket released, new one reserved, rolled back on
 *       failure)
 */
mini_os_err_t mini_os_deadline_thread_set_bandwidth(mini_os_thread_t* thread, mini_os_tick_t runtime, mini_os_tick_t period);

#endif /* MINI_OS_THREAD_DEADLINE */

#if MINI_OS_THREAD_EDF
/**
 * @brief Attach or clear a deadline on an ordinary thread (same-priority EDF)
 * @param[in] thread thread to configure; must not be a MINI_OS_SCHED_DEADLINE thread
 * @param[in] deadline relative deadline in ticks from now; 0 clears the deadline
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument or a
 *         MINI_OS_SCHED_DEADLINE thread
 * @note the thread keeps its priority: inside that level it is ordered by
 *       earliest absolute deadline instead of FIFO, and it is queued ahead of
 *       the ordinary threads of the level. A READY/RUNNING thread is re-linked.
 */
mini_os_err_t mini_os_thread_set_deadline(mini_os_thread_t* thread, mini_os_tick_t deadline);

#endif /* MINI_OS_THREAD_EDF */

#if MINI_OS_THREAD_DEADLINE
/**
 * @brief Read how many deadlines a DL thread has missed
 * @param[in] thread thread to query
 * @param[out] count receives the number of missed deadlines
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_thread_get_dl_miss_count(mini_os_thread_t* thread, mini_os_uint32_t* count);

/**
 * @brief Finish the current DL job and wait for the next period
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when the caller is not a DL thread
 * @details A DL thread calls this when its job is done. The completion is recorded
 *          (dl_finish_count and the finish hook) and the thread blocks until its
 *          current absolute deadline; at that boundary the scheduler starts a new
 *          job (budget refilled, dl_deadline_time += dl_period) and the call
 *          returns, so the caller never computes delays or tracks the period.
 * @note thread context only. The finish hook runs here in thread context, unlike
 *       the miss hook which runs in the SysTick handler.
 */
mini_os_err_t mini_os_deadline_job_finish(void);

/**
 * @brief Read how many jobs a DL thread has finished
 * @param[in] thread thread to query
 * @param[out] count receives the number of finished jobs
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_thread_get_dl_finish_count(mini_os_thread_t* thread, mini_os_uint32_t* count);

/**
 * @brief Read how many times a DL thread exhausted its budget (was throttled)
 * @param[in] thread thread to query
 * @param[out] count receives the throttle count
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 * @note incremented every time the CBS budget of a DL thread runs out and the
 *       thread is parked until its next period; every other policy keeps it at 0
 * @note a throttle always implies a deadline miss for that job: once the budget
 *       is exhausted the task cannot complete before its deadline, so the period
 *       boundary reports the miss and dl_miss_count is incremented as well
 */
mini_os_err_t mini_os_thread_get_dl_throttle_count(mini_os_thread_t* thread, mini_os_uint32_t* count);
#endif /* MINI_OS_THREAD_DEADLINE */

/**
 * @brief Create the idle thread (weak default; override to customize)
 */
void mini_os_thread_idle_create(void);
#if defined(__cplusplus)
}
#endif
#endif /* MINI_OS_THREAD_H */
