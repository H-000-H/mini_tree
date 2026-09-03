/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file mutex.h
 * @brief mutex implementation (recursive variant, priority inheritance over a binary semaphore)
 * @author H-000-H
 */
#ifndef MUTEX_H
#define MUTEX_H
#include "redef.h"
#include "semaphore.h"
#include "thread.h"
#if defined(__cplusplus)
extern "C"
{
#endif
// clang-format off
typedef struct mini_os_mutex mini_os_mutex_t;
/**
 * @brief Mutex structure
 * @note inheritance through the embedded binary semaphore (count/max = 1).
 *       No priority is stored per mutex: while held, the mutex is linked into
 *       owner->hold_list through hold_node and the owner's effective priority
 *       is recomputed as min(base_priority, highest waiter of every held
 *       mutex), so a thread holding several mutexes keeps the highest
 *       requirement of all of them
 */
struct mini_os_mutex
{
    mini_os_semaphore_t semaphore;      /**< mutex inheritance through Semaphore */
    mini_os_uint8_t     depth;          /**< recursion depth (same-owner lock count, 0 = free) */
    mini_os_bool_t      is_recuring;    /**< MINI_OS_TRUE: the owner may re-lock (depth++) */
    mini_os_thread_t*   owner;          /**< current owner, MINI_OS_NULL when free */
    mini_os_list_t      hold_node;      /**< links the mutex into owner->hold_list while held */
    mini_os_bool_t      kill_enable;    /**< MINI_OS_TRUE after mini_os_mutex_enable_kill: delete may
                                   force-release */
#if MINI_OS_FIND_BY_NAME
    mini_os_list_t g_list_node;         /**< node of the global by-name registry */
#endif
};

/**
 * @brief Create a non-recursive mutex (heap, created unlocked)
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @return A pointer to the created mutex, or MINI_OS_NULL on failure
 */
mini_os_mutex_t* mini_os_mutex_create(const char* name);

/**
 * @brief Delete a heap-created mutex and free its memory
 * @param[in] mutex A pointer to the mutex to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_NOTSUPP for a static mutex (use mini_os_mutex_delete_static);
 *         MINI_OS_ERR_BUSY while owned or parked waiters exist (or a unit is in
 *         flight), unless mini_os_mutex_enable_kill() was called
 * @note with kill enabled a busy mutex is force-released: parked waiters are
 *       unlinked and their mini_os_mutex_lock returns MINI_OS_ERR_TIMEOUT, the
 *       owner's later unlock returns MINI_OS_ERR_INVAL
 */
mini_os_err_t mini_os_mutex_delete(mini_os_mutex_t* mutex);

/**
 * @brief Create a non-recursive mutex over caller-provided storage (created unlocked)
 * @param[in] mutex storage for the mutex descriptor
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @return A pointer to the created mutex, or MINI_OS_NULL on invalid arguments
 * @note the storage must remain valid for the mutex lifetime
 */
mini_os_mutex_t* mini_os_mutex_create_static(mini_os_mutex_t* mutex, const char* name);

/**
 * @brief Delete a static mutex (clears the caller-provided storage, never frees)
 * @param[in] mutex A pointer to the mutex to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments;
 *         MINI_OS_ERR_NOTSUPP for a heap mutex (use mini_os_mutex_delete);
 *         MINI_OS_ERR_BUSY like mini_os_mutex_delete, kill_enable honored
 */
mini_os_err_t mini_os_mutex_delete_static(mini_os_mutex_t* mutex);

/**
 * @brief Create a recursive mutex (heap, created unlocked)
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @return A pointer to the created mutex, or MINI_OS_NULL on failure
 * @note the owner may re-lock: each lock deepens depth, each unlock releases
 *       one level, only the final unlock hands the mutex over
 */
mini_os_mutex_t* mini_os_mutex_recuring_create(const char* name);

/**
 * @brief Create a recursive mutex over caller-provided storage (created unlocked)
 * @param[in] name mutex name (MINI_OS_NULL = unnamed)
 * @param[in] mutex storage for the mutex descriptor
 * @return A pointer to the created mutex, or MINI_OS_NULL on invalid arguments
 */
mini_os_mutex_t* mini_os_mutex_recuring_create_static(const char* name, mini_os_mutex_t* mutex);

/**
 * @brief Lock a mutex (priority inheritance on contention)
 * @param[in] mutex A pointer to the mutex to lock
 * @param[in] timeout_tick 0 = non-blocking, MINI_OS_WAIT_FOREVER = block until
 *            available, otherwise block up to timeout_tick ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments or no
 *         thread context; MINI_OS_ERR_AGAIN when non-blocking and contested;
 *         MINI_OS_ERR_BUSY when the owner re-locks a non-recursive mutex or the
 *         recursion depth overflows; MINI_OS_ERR_TIMEOUT when the wait expired
 *         (or the mutex was kill-deleted while parked)
 * @note when the caller must block, the owner's effective priority is recomputed
 *       as the highest (smallest number) of its base priority and every waiter
 *       of every mutex it holds, so holding several mutexes keeps the highest
 *       requirement. If the owner is itself blocked on another mutex the
 *       requirement is pushed on to that mutex's owner too, up to
 *       MINI_OS_MUTEX_PI_CHAIN_MAX links
 * @note the boost is dropped when the caller times out, when the owner releases
 *       the mutex, or when the mutex is kill-deleted: the owner is recomputed
 *       from its base priority and the mutexes it still holds
 */
mini_os_err_t mini_os_mutex_lock(mini_os_mutex_t* mutex, mini_os_tick_t timeout_tick);

/**
 * @brief Unlock a mutex (owner only; the final unlock recomputes the owner priority)
 * @param[in] mutex A pointer to the mutex to unlock
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when not called by the owner;
 *         MINI_OS_ERR_BUSY when the embedded semaphore give fails
 * @note a recursive lock is released one level per call; the final level hands
 *       the unit to the oldest parked waiter (yielding when one wakes) or
 *       republishes it. The owner settles on the highest priority still
 *       required by the mutexes it keeps holding, not on the saved base of this
 *       mutex alone
 */
mini_os_err_t mini_os_mutex_unlock(mini_os_mutex_t* mutex);

/**
 * @brief Try-lock a mutex from ISR context (never blocks, never yields)
 * @param[in] mutex A pointer to the mutex to lock
 * @return MINI_OS_OK on success (ownership attributed to the interrupted thread);
 *         MINI_OS_ERR_INVAL on invalid arguments or no interrupted thread;
 *         MINI_OS_ERR_AGAIN when contested; MINI_OS_ERR_BUSY on a non-recursive
 *         owner re-lock or recursion overflow
 * @note no priority inheritance happens for a failed ISR try-lock (the ISR does
 *       not park as a waiter)
 */
mini_os_err_t mini_os_mutex_lock_isr(mini_os_mutex_t* mutex);

/**
 * @brief Unlock a mutex from ISR context (the interrupted thread must be the owner)
 * @param[in] mutex A pointer to the mutex to unlock
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when the interrupted thread is
 *         not the owner; MINI_OS_ERR_BUSY when the embedded semaphore give fails
 * @note wakes the oldest waiter but never triggers the context switch itself:
 *       use the mini_os_queue_isr_is_heigher_priority() + mini_os_yield_trigger()
 *       pattern at the end of the ISR (same as the other ISR APIs)
 */
mini_os_err_t mini_os_mutex_unlock_isr(mini_os_mutex_t* mutex);

/**
 * @brief Allow mini_os_mutex_delete/_static to force-release this mutex
 * @param[in] mutex A pointer to the mutex to arm
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments
 * @note parked waiters of a kill-deleted mutex are released with
 *       MINI_OS_ERR_TIMEOUT from their mini_os_mutex_lock call; the owner's
 *       later unlock returns MINI_OS_ERR_INVAL
 */
mini_os_err_t mini_os_mutex_enable_kill(mini_os_mutex_t* mutex);

/**
 * @brief Recompute a thread's effective priority from its base priority, the
 *        mutexes it holds and the mutex it waits on (kernel API)
 * @param[in] thread Thread to recompute
 * @return MINI_OS_OK on success, MINI_OS_ERR_INVAL on invalid arguments
 * @note called by mini_os_thread_set_priority after base_priority changed, so a
 *       lock holder keeps the boost its waiters still require and a thread
 *       blocked on a mutex pushes its new priority on to that mutex's owner
 */
mini_os_err_t mini_os_mutex_priority_recompute(mini_os_thread_t* thread);

/**
 * @brief Force-release every mutex a disappearing thread still holds (kernel API)
 * @param[in] thread Thread that is about to be exited or deleted
 * @return MINI_OS_TRUE when at least one parked waiter was released, so the
 *         caller should yield; MINI_OS_FALSE otherwise (or on invalid arguments)
 * @note kill semantics: each mutex is left free (owner MINI_OS_NULL, depth 0,
 *       unit republished) and its parked waiters are released with
 *       MINI_OS_ERR_TIMEOUT from their mini_os_mutex_lock call. The unit is not
 *       handed over to a waiter because the protected resource may be in an
 *       inconsistent state after its owner disappeared
 * @note never yields by itself, so it is safe to call inside a critical section
 */
mini_os_bool_t mini_os_mutex_kill_held(mini_os_thread_t* thread);

#if MINI_OS_FIND_BY_NAME
/**
 * @brief Get a mutex by name
 * @param[in] name mutex name
 * @return A pointer to the mutex, or MINI_OS_NULL if not found
 */
mini_os_mutex_t* mini_os_mutex_find_by_name(const char* name);
#endif
#if defined(__cplusplus)
}
#endif

#endif /* MUTEX_H */
