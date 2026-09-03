/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file thread.c
 * @brief Thread management functions
 */
#include "thread.h"

#include "err.h"
#include "list.h"
#include "memory.h"
#include "mini_config.h"
#include "mutex.h"
#include "port.h"
#include "redef.h"
#include "schedule.h"
#include "semaphore.h"
#if MINI_OS_FIND_BY_NAME
/** @brief Global registry of every thread, used by mini_os_find_by_name() */
static mini_os_list_t g_threads_list;

/**
 * @brief Initialize the global thread registry (constructor, runs before main)
 * @note the list head becomes a self-referencing sentinel, so threads can be
 *       linked before the scheduler is up
 */
MINI_OS_CONSTRUCTOR(MINI_OS_FIND_BY_NAME_CONSTRUCTOR) void mini_os_global_list_init(void) { mini_os_list_init(&g_threads_list); }
#endif
static void mini_os_thread_entry_wrapper(void* param);

/**
 * @brief Build the initial stack frame of a thread (Cortex-M exception frame)
 * @param[in] stack_buf stack base
 * @param[in] stack_size stack size in bytes
 * @param[in] param argument handed to the entry function through R0
 * @return the stack pointer to store in the TCB
 * @details the frame is what the port pops on the first switch: xPSR, PC (the
 *          entry wrapper, Thumb bit set), LR, R12-R1 and R0 = param, followed by
 *          R11-R4. With MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU one extra word
 *          is reserved below them as the "no s16-s31 saved yet" flag
 * @note the frame grows downwards from the stack top, so sp ends below every
 *       pushed register
 */
static void* mini_os_thread_stack_init(mini_os_uint8_t* stack_buf, mini_os_uint32_t stack_size, void* param)
{
    volatile mini_os_uint32_t* sp = (volatile mini_os_uint32_t*)(stack_buf + stack_size);
    *(--sp) = 0x01000000U;                                                         /**< xPSR */
    *(--sp) = (mini_os_uint32_t)(mini_os_size_t)mini_os_thread_entry_wrapper | 1u; /**< PC: wrapper (runs entry, then cleanup + exit) */
    *(--sp) = 0xFFFFFFFFU;                                                         /**< LR */
    *(--sp) = 0U;                                                                  /**< R12 */
    *(--sp) = 0U;                                                                  /**< R3 */
    *(--sp) = 0U;                                                                  /**< R2 */
    *(--sp) = 0U;                                                                  /**< R1 */
    *(--sp) = (mini_os_uint32_t)(mini_os_size_t)param;                             /**< R0: param entry */

    *(--sp) = 0U; /**< r11 */
    *(--sp) = 0U; /**< r10 */
    *(--sp) = 0U; /**< r9 */
    *(--sp) = 0U; /**< r8 */
    *(--sp) = 0U; /**< r7 */
    *(--sp) = 0U; /**< r6 */
    *(--sp) = 0U; /**< r5 */
    *(--sp) = 0U; /**< r4 */
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    *(--sp) = 0U; /**< FPU flag: 0 = no s16-s31 saved yet */
#endif
    return (void*)sp;
}

/**
 * @brief Fill a thread control block over already-provided storage
 * @param[in] thread TCB to initialize (heap or caller storage)
 * @param[in] name thread name (MINI_OS_NULL = empty name)
 * @param[in] stack_size stack size in bytes (8-byte aligned, >= minimum)
 * @param[in] priority thread priority (< MINI_OS_PRIORITY)
 * @param[in] entry thread entry function
 * @param[in] param argument passed to entry
 * @param[in] stack_buffer stack storage (8-byte aligned)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on any invalid argument
 * @details apart from the plain field copies the TCB is brought into a usable
 *          invariant: every list node is self-referencing (mini-os list
 *          convention), the thread is INIT, holds no mutex and waits on nothing,
 *          and the initial stack frame is built by
 *          mini_os_thread_stack_init()
 * @note with MINI_OS_THREAD_DETACH a thread is created detached: the idle
 *       thread reaps its corpse unless a joiner pins it
 */
static mini_os_err_t mini_os_thread_init(mini_os_thread_t* thread, const char* name, mini_os_size_t stack_size, mini_os_uint8_t priority, void (*entry)(void*), void* param, mini_os_uint32_t* stack_buffer)
{

    if (entry == MINI_OS_NULL || priority >= MINI_OS_PRIORITY || stack_size < MINI_OS_THREAD_MIN_STACK_SIZE || (stack_size & 7U) != 0U || stack_buffer == MINI_OS_NULL || ((mini_os_size_t)stack_buffer & 7U) != 0U || thread == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    /* thread name (bounded copy, always NUL-terminated) */
    mini_os_set_name(thread->thread_name, name, MINI_OS_THREADS_NAME_LEN);

    thread->entry = entry;
    thread->param = param;
    thread->stack_size = stack_size;
    thread->stack_addr = stack_buffer;
    thread->priority = priority;
    thread->base_priority = priority;
#if MINI_OS_FIND_BY_NAME
    mini_os_list_init(&thread->g_list_node);
    mini_os_list_tail(&thread->g_list_node, &g_threads_list);
#endif
    thread->state = MINI_OS_THREAD_STATE_INIT;
    thread->err = MINI_OS_OK;

    thread->sp = mini_os_thread_stack_init((mini_os_uint8_t*)stack_buffer, stack_size, param);
    thread->round = 0;
    thread->resume_time = 0;
    thread->wheel_slot = (mini_os_uint8_t)MINI_OS_TICK_WHEEL;
#if MINI_OS_TIME_SLICE
    thread->remain_tick = 0;
    thread->init_tick_num = 0;
#endif

    /* list nodes must be self-referencing (mini-os list convention) */
    mini_os_list_init(&thread->list_node);
    mini_os_list_init(&thread->wait_node);
    mini_os_list_init(&thread->hold_list);
    thread->wait_list = MINI_OS_NULL;
    thread->wait_done = MINI_OS_TRUE;
    thread->wait_mutex = MINI_OS_NULL;
#if MINI_OS_EVENT
    thread->wait_mask = 0u;
#endif

    thread->thread_cleanup = MINI_OS_NULL;
    thread->user_data = 0;

#if MINI_OS_THREAD_DETACH
    /* detach by default: idle reaps every corpse unless a joiner is involved */
    thread->is_detach = MINI_OS_TRUE;
    thread->is_terminated = MINI_OS_FALSE;
    thread->exit_retval = MINI_OS_NULL;
    thread->join_wait_sem = MINI_OS_NULL;
#endif

    return MINI_OS_OK;
}

/** @brief Current thread TCB pointer, shared with the port assembly (port.c) */
mini_os_thread_t* mini_os_current_thread = MINI_OS_NULL;

/**
 * @brief Get the TCB of the running thread
 * @return current thread handle; MINI_OS_NULL before the scheduler started
 */
mini_os_thread_t* mini_os_thread_current(void) { return mini_os_current_thread; }

/** @brief Idle thread TCB, published by mini_os_thread_idle_create() */
static mini_os_thread_t* s_idle_thread = MINI_OS_NULL;

/**
 * @brief Get the idle thread handle
 * @return idle thread handle; MINI_OS_NULL before mini_os_thread_idle_create()
 *         succeeded
 */
mini_os_thread_t* mini_os_thread_get_idle_handle(void) { return s_idle_thread; }

/** @brief Corpses of terminated threads, drained (freed) by the idle thread */
static mini_os_list_t s_defunct_list;

/**
 * @brief Enqueue a terminated thread into the corpse queue
 * @param[in] thread thread that has just been marked TERMINATED
 * @note caller must hold the IRQ lock; lazy-inits the queue on first use;
 *       the corpse stays linked here until the idle thread reclaims it
 */
static void mini_os_defunct_list_insert(mini_os_thread_t* thread)
{
    if (s_defunct_list.next == MINI_OS_NULL)
        mini_os_list_init(&s_defunct_list); /* lazy init (constructor-free) */
    mini_os_list_tail(&thread->list_node, &s_defunct_list);
}

/**
 * @brief Run the user entry, then cleanup and exit
 * @param[in] param argument passed to the thread entry function
 * @note pushed as the initial PC so a returning entry is handled, not faulted
 */
static void mini_os_thread_entry_wrapper(void* param)
{
    mini_os_thread_t* thread = mini_os_current_thread;

    thread->entry(param);

    if (thread->thread_cleanup != MINI_OS_NULL)
        thread->thread_cleanup(param);

    mini_os_thread_exit(MINI_OS_NULL);
}

/**
 * @brief Reclaim terminated threads (called from the idle thread)
 * @details walks the corpse queue under the IRQ lock and frees the stack and
 *          the TCB of every corpse; heap-backed memory goes back to the heap,
 *          caller-owned (static) memory is left untouched because
 *          mini_os_free() rejects non-heap pointers
 * @note a corpse still pinned for a join (is_detach == MINI_OS_FALSE) is
 *       skipped: the joiner reads exit_retval out of the TCB and unpins it when
 *       it is done
 * @note the list head is zeroed until the first corpse arrives, so a head that
 *       was never initialized must not be walked: it would not report empty
 */
static void mini_os_thread_defunct_execute(void)
{
    mini_os_list_t* node;
    mini_os_list_t* next;
    mini_os_irq_t   irq = mini_os_irq_save();

    if (s_defunct_list.next == MINI_OS_NULL)
    {
        /* nothing was ever enqueued: the head is still zeroed and would not
         * report itself empty, so there is nothing to walk */
        mini_os_irq_restore(irq);
        return;
    }

    for (node = s_defunct_list.next; node != &s_defunct_list; node = next)
    {
        mini_os_thread_t* dead;

        next = node->next; /* captured first: list_remove re-links the node */
        dead = mini_os_container_of(node, mini_os_thread_t, list_node);
#if MINI_OS_THREAD_DETACH
        if (dead->is_detach == MINI_OS_FALSE)
            continue; /* a joiner is still working with this TCB */
        if (dead->join_wait_sem != MINI_OS_NULL)
        {
            (void)mini_os_semaphore_delete(dead->join_wait_sem);
            dead->join_wait_sem = MINI_OS_NULL;
        }
#endif
#if MINI_OS_FIND_BY_NAME
        mini_os_list_remove(&dead->g_list_node);
#endif
        mini_os_list_remove(node);
        mini_os_free(dead->stack_addr);
        mini_os_free(dead);
    }
    mini_os_irq_restore(irq);
}

/**
 * @brief Terminate the current thread (never returns)
 * @param[in] retval exit value, retrievable by a joiner
 * @details running inside the dying thread's own context: it force-releases the
 *          mutexes it still owns, leaves the ready/running list, publishes the
 *          exit value, wakes a parked joiner and queues the corpse
 * @note the last step switches away and spins: the TCB and stack are freed
 *       later by the idle thread, never by this one
 */
MINI_OS_NO_RETURN void mini_os_thread_exit(void* retval)
{
    mini_os_thread_t* thread = mini_os_current_thread;
    mini_os_irq_t     irq;

    if (thread == MINI_OS_NULL)
        while (1)
        {
        } /* fatal: exit called outside a thread */

    irq = mini_os_irq_save();

    /* a dying thread must not take its locks with it: force-release them so the
     * parked waiters fail with MINI_OS_ERR_TIMEOUT instead of waiting forever.
     * No yield here, the switch below covers the released waiters */
    (void)mini_os_mutex_kill_held(thread);

    /* a running thread is still linked in the ready/running list */
    mini_os_remove_thread_from_ready_running_list(thread);

    thread->state = MINI_OS_THREAD_STATE_TERMINATED;
#if MINI_OS_THREAD_DETACH
    thread->is_terminated = MINI_OS_TRUE;
    thread->exit_retval = retval;
    if (thread->join_wait_sem != MINI_OS_NULL)
    {
        /* publish the exit: hands the unit to the oldest parked joiner, or keeps
         * it for a join that arrives after this thread is gone */
        (void)mini_os_semaphore_give(thread->join_wait_sem);
    }
#else
    (void)retval; /* join fields are compiled out without MINI_OS_THREAD_DETACH */
#endif

    /* corpse queue: reclaimed by the idle thread */
    mini_os_defunct_list_insert(thread);

    mini_os_irq_restore(irq);
    mini_os_schedule_yield(); /* switch away, never return */
    while (1)
    {
    }
}

/**
 * @brief Suspend a thread
 * @param[in] thread thread to suspend
 * @return MINI_OS_OK on success or when it is already SUSPENDED;
 *         MINI_OS_ERR_INVAL when thread is MINI_OS_NULL;
 *         MINI_OS_ERR_BUSY for an INIT or TERMINATED thread
 * @details
 *  - a READY/RUNNING thread is unlinked from the ready/running list
 *  - a BLOCKED thread is unlinked from its wait list; a wheel-parked one keeps
 *    the remaining delay in resume_time (frozen), a sync-object wait is
 *    canceled (wait_done = MINI_OS_FALSE), and the owner of the mutex it waited
 *    on drops the boost this waiter justified
 *  - suspending the current thread triggers a context switch
 */
mini_os_err_t mini_os_thread_suspend(mini_os_thread_t* thread)
{
    if (thread == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    if (thread->state == MINI_OS_THREAD_STATE_SUSPENDED)
        return MINI_OS_OK;

    mini_os_irq_t irq = mini_os_irq_save();
    if (thread->state == MINI_OS_THREAD_STATE_READY || thread->state == MINI_OS_THREAD_STATE_RUNNING)
    {
        mini_os_remove_thread_from_ready_running_list(thread);
    }
    else if (thread->state == MINI_OS_THREAD_STATE_BLOCKED)
    {
        /* Capture the remaining delay before unlinking: a wheel-parked thread
         * (plain delay or a timed sync wait) keeps its countdown frozen in
         * resume_time; a forever sync-object waiter is canceled (resume_time = 0). */
        thread->resume_time = (mini_os_tick_t)mini_os_wheel_remain(thread);
        if (thread->wait_list != MINI_OS_NULL)
        {
            mini_os_mutex_t* waited = thread->wait_mutex;

            if (waited != MINI_OS_NULL && thread->wait_list != &waited->semaphore.wait_list)
                waited = MINI_OS_NULL; /* stale back pointer: nothing to recompute */
            mini_os_list_remove(&thread->wait_node);
            thread->wait_list = MINI_OS_NULL;
            thread->wait_mutex = MINI_OS_NULL; /* no longer parked on a mutex */
            thread->wait_done = MINI_OS_FALSE; /* the sync wait is canceled */
            if (waited != MINI_OS_NULL && waited->owner != MINI_OS_NULL)
            {
                /* same unwind as unlink_blocked: the owner must not keep the
                 * boost a suspended waiter justified */
                (void)mini_os_mutex_priority_recompute(waited->owner);
            }
        }
        if (!mini_os_list_is_empty(&thread->list_node))
            mini_os_list_remove(&thread->list_node);
        thread->wheel_slot = (mini_os_uint8_t)MINI_OS_TICK_WHEEL;
        thread->round = 0;
    }
    else /**< INIT/TERMINATED: nothing to detach, nothing to suspend */
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY;
    }

    thread->state = MINI_OS_THREAD_STATE_SUSPENDED;

    mini_os_irq_restore(irq);
    if (thread == mini_os_current_thread)
        mini_os_schedule_yield();
    return MINI_OS_OK;
}

/**
 * @brief Resume a suspended thread
 * @param[in] thread thread to resume
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 *         or not SUSPENDED
 * @details
 *  - with a captured resume_time: re-parked in the time wheel (BLOCKED), the
 *    frozen delay continues exactly
 *  - otherwise: put back into the ready/running list
 */
mini_os_err_t mini_os_thread_resume(mini_os_thread_t* thread)
{
    if (thread == MINI_OS_NULL || thread->state != MINI_OS_THREAD_STATE_SUSPENDED)
        return MINI_OS_ERR_INVAL;

    mini_os_irq_t irq = mini_os_irq_save();
    if (thread->resume_time > 0)
    {
        /* was wheel-suspended (plain delay or a timed sync wait): re-park
         * with the exact remaining ticks */
        mini_os_wheel_insert(thread, (mini_os_uint32_t)thread->resume_time);
        thread->resume_time = 0;
    }
    else
    {
        /* suspended from the ready list or a canceled sync wait: back to ready */
        mini_os_add_thread_to_ready_running_list(thread);
    }
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Create a thread from heap memory
 * @param[in] name thread name (MINI_OS_NULL = empty name)
 * @param[in] stack_size stack size in bytes
 * @param[in] priority thread priority (< MINI_OS_PRIORITY)
 * @param[in] entry thread entry function
 * @param[in] param argument passed to entry
 * @return thread handle on success; MINI_OS_NULL on invalid arguments, out of
 *         memory, or when the thread could not be made ready
 * @details the TCB and the stack are allocated separately, so either allocation
 *          can fail independently; both are given back before any error return
 * @note the thread is made ready immediately (INIT -> READY), so it may start
 *       running before the creator returns
 */
mini_os_thread_t* mini_os_thread_create(const char* name, mini_os_uint32_t stack_size, mini_os_uint8_t priority, void (*entry)(void*), void* param)
{
    mini_os_thread_t* thread;
    mini_os_uint32_t* stack;

    if (!name || stack_size == 0 || priority >= MINI_OS_PRIORITY || !entry)
        return MINI_OS_NULL;

    thread = (mini_os_thread_t*)mini_os_malloc(sizeof(mini_os_thread_t));
    if (thread == MINI_OS_NULL)
        return MINI_OS_NULL;

    stack = (mini_os_uint32_t*)mini_os_malloc(stack_size);
    if (stack == MINI_OS_NULL)
    {
        mini_os_free(thread);
        return MINI_OS_NULL;
    }

    if (mini_os_thread_init(thread, name, stack_size, priority, entry, param, stack) != MINI_OS_OK)
    {
        mini_os_free(stack);
        mini_os_free(thread);
        return MINI_OS_NULL;
    }
    /* auto-start: the thread becomes ready immediately */
    if (mini_os_add_thread_to_ready_running_list(thread) != MINI_OS_OK)
    {
        mini_os_free(stack);
        mini_os_free(thread);
        return MINI_OS_NULL;
    }
    return thread;
}

/**
 * @brief Detach a BLOCKED thread from everywhere it is parked
 * @param[in] thread BLOCKED thread to detach (wheel and/or sync wait list)
 * @details leaves the sync-object wait list (clearing wait_list/wait_mutex, and
 *          recomputing the owner of the mutex it waited on, whose boost this
 *          waiter justified) and then the wheel slot, if it is in one
 * @note caller must hold the IRQ lock
 */
static void mini_os_thread_unlink_blocked(mini_os_thread_t* thread)
{
    if (thread->wait_list != MINI_OS_NULL)
    {
        mini_os_mutex_t* waited = thread->wait_mutex;

        if (waited != MINI_OS_NULL && thread->wait_list != &waited->semaphore.wait_list)
            waited = MINI_OS_NULL; /* stale back pointer: nothing to recompute */
        mini_os_list_remove(&thread->wait_node);
        thread->wait_list = MINI_OS_NULL;
        thread->wait_mutex = MINI_OS_NULL; /* no longer parked on a mutex */
        if (waited != MINI_OS_NULL && waited->owner != MINI_OS_NULL)
        {
            /* the boost this waiter justified goes away with it: recompute the
             * owner (and, through the wait chain, whoever the owner waits on) */
            (void)mini_os_mutex_priority_recompute(waited->owner);
        }
    }
    if (thread->wheel_slot < MINI_OS_TICK_WHEEL)
        (void)mini_os_remove_thread_from_blocked_list(thread);
}

/**
 * @brief Delete a thread and free its heap memory
 * @param[in] thread thread to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL for a NULL target, the
 *         running thread or an already terminated one; MINI_OS_ERR_BUSY while a
 *         joiner has the corpse pinned
 * @details the target is unlinked from wherever it sits (ready/running list or
 *          the wheel plus a sync wait list), then every mutex it still owns is
 *          force-released so the parked waiters fail with MINI_OS_ERR_TIMEOUT
 *          instead of blocking forever
 * @note the guarded data may be left inconsistent by the force-release, so
 *       unlocking properly before the delete is still the preferred way.
 *       Deleting a thread that only waits for a mutex is safe: the owner drops
 *       the inherited boost when it releases
 */
mini_os_err_t mini_os_thread_delete(mini_os_thread_t* thread)
{
    mini_os_bool_t woken;
    mini_os_irq_t  irq;

    if (thread == MINI_OS_NULL || thread == mini_os_current_thread || thread->state == MINI_OS_THREAD_STATE_TERMINATED)
        return MINI_OS_ERR_INVAL; /* running thread; TERMINATED ones are reclaimed by idle */

    irq = mini_os_irq_save();
#if MINI_OS_THREAD_DETACH
    if (thread->is_detach == MINI_OS_FALSE)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY; /* pinned by a joiner, the TCB must outlive the join */
    }
    if (thread->join_wait_sem != MINI_OS_NULL)
    {
        (void)mini_os_semaphore_delete(thread->join_wait_sem);
        thread->join_wait_sem = MINI_OS_NULL;
    }
#endif
    if (thread->state == MINI_OS_THREAD_STATE_READY || thread->state == MINI_OS_THREAD_STATE_RUNNING)
        mini_os_remove_thread_from_ready_running_list(thread);
    else if (thread->state == MINI_OS_THREAD_STATE_BLOCKED)
        mini_os_thread_unlink_blocked(thread);
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&thread->g_list_node);
#endif
    /* the target must not take its locks with it: force-release them so the parked
     * waiters fail with MINI_OS_ERR_TIMEOUT instead of blocking forever. Parked
     * wait queues are already covered by mini_os_thread_unlink_blocked() above */
    woken = mini_os_mutex_kill_held(thread);
    mini_os_irq_restore(irq);

    mini_os_free(thread->stack_addr);
    mini_os_free(thread);
    if (woken != MINI_OS_FALSE)
        (void)mini_os_schedule_yield();
    return MINI_OS_OK;
}

/**
 * @brief Create a thread over caller-provided storage
 * @param[in] name thread name (MINI_OS_NULL = empty name)
 * @param[in] stack_size stack size in bytes
 * @param[in] priority thread priority (< MINI_OS_PRIORITY)
 * @param[in] entry thread entry function
 * @param[in] param argument passed to entry
 * @param[in] stack_buffer stack storage, already passed through
 *            mini_os_stack_create()
 * @param[in] task_buffer TCB storage (mini_os_thread_t)
 * @return thread handle on success; MINI_OS_NULL on invalid arguments or when
 *         the thread could not be made ready
 * @note nothing is freed on failure and nothing is freed on delete: the storage
 *       belongs to the caller and must outlive the thread
 */
mini_os_thread_t* mini_os_thread_create_static(const char* name, mini_os_uint32_t stack_size, mini_os_uint8_t priority, void (*entry)(void*), void* param, mini_os_uint32_t* stack_buffer, mini_os_thread_t* task_buffer)
{
    mini_os_size_t aligned;

    if (!name || stack_size == 0 || priority >= MINI_OS_PRIORITY || !entry || !stack_buffer || !task_buffer)
        return MINI_OS_NULL;
    /* the stack must go through mini_os_stack_create first (8-byte alignment gate) */
    if (mini_os_stack_create(stack_size, stack_buffer, &aligned) == MINI_OS_NULL)
        return MINI_OS_NULL;
    if (mini_os_thread_init(task_buffer, name, stack_size, priority, entry, param, stack_buffer) != MINI_OS_OK)
        return MINI_OS_NULL;
    /* auto-start: the thread becomes ready immediately */
    if (mini_os_add_thread_to_ready_running_list(task_buffer) != MINI_OS_OK)
        return MINI_OS_NULL;
    return task_buffer;
}

/**
 * @brief Delete a statically created thread (clears the caller storage)
 * @param[in] thread thread to delete
 * @param[in] stack_buffer stack storage handed to mini_os_thread_create_static()
 * @param[in] task_buffer TCB storage handed to mini_os_thread_create_static()
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL for a NULL argument, the
 *         running thread or an already terminated one; MINI_OS_ERR_BUSY while a
 *         joiner has the corpse pinned
 * @details same teardown as mini_os_thread_delete(): the thread is unlinked and
 *          its mutexes are force-released, then the TCB and the stack are zeroed
 *          instead of freed
 * @note thread == task_buffer is the normal case, so the stack fields are
 *       captured before the TCB is wiped
 */
mini_os_err_t mini_os_thread_delete_static(mini_os_thread_t* thread, mini_os_uint32_t* stack_buffer, mini_os_thread_t* task_buffer)
{
    mini_os_size_t    stack_size;
    mini_os_uint32_t* stack_addr;
    mini_os_bool_t    woken;

    if (!thread || !stack_buffer || !task_buffer || thread == mini_os_current_thread || thread->state == MINI_OS_THREAD_STATE_TERMINATED)
        return MINI_OS_ERR_INVAL; /* running thread; TERMINATED ones are reclaimed by idle */
    /* thread == task_buffer: capture fields before the TCB is zeroed */
    stack_size = thread->stack_size;
    stack_addr = thread->stack_addr;

    mini_os_irq_t irq = mini_os_irq_save();
#if MINI_OS_THREAD_DETACH
    if (thread->is_detach == MINI_OS_FALSE)
    {
        mini_os_irq_restore(irq);
        return MINI_OS_ERR_BUSY; /* pinned by a joiner, the TCB must outlive the join */
    }
    if (thread->join_wait_sem != MINI_OS_NULL)
    {
        /* the semaphore itself is heap owned even for a static thread */
        (void)mini_os_semaphore_delete(thread->join_wait_sem);
        thread->join_wait_sem = MINI_OS_NULL;
    }
#endif
    if (thread->state == MINI_OS_THREAD_STATE_READY || thread->state == MINI_OS_THREAD_STATE_RUNNING)
        mini_os_remove_thread_from_ready_running_list(thread);
    else if (thread->state == MINI_OS_THREAD_STATE_BLOCKED)
        mini_os_thread_unlink_blocked(thread);
#if MINI_OS_FIND_BY_NAME
    mini_os_list_remove(&thread->g_list_node);
#endif
    /* force-release the mutexes the target still owns, see mini_os_thread_delete() */
    woken = mini_os_mutex_kill_held(thread);

    MINI_OS_MEMSET(task_buffer, 0, sizeof(mini_os_thread_t));
    MINI_OS_MEMSET(stack_addr, 0, stack_size);
    mini_os_irq_restore(irq);
    if (woken != MINI_OS_FALSE)
        (void)mini_os_schedule_yield();
    return MINI_OS_OK;
}

#if MINI_OS_THREAD_DETACH
/**
 * @brief Detach a thread (hand its corpse back to the idle reaper)
 * @param[in] thread thread to detach
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 * @note threads are created detached, so the idle reaper collects them without
 *       this call; detaching is only needed to cancel the pin a join installed,
 *       or to release a corpse a joiner abandoned
 */
mini_os_err_t mini_os_thread_detach(mini_os_thread_t* thread)
{
    mini_os_irq_t irq;

    if (thread == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    thread->is_detach = MINI_OS_TRUE;
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Join a thread: block until it terminates, then collect its return value
 * @param[in] thread thread to join
 * @param[out] thread_return buffer to store the thread return value in (MINI_OS_NULL = discard)
 * @param[in] timeout_tick 0 = poll, MINI_OS_WAIT_FOREVER = wait forever, otherwise
 *            the maximum number of ticks to wait
 * @return MINI_OS_OK when the thread terminated and the value was collected,
 *         MINI_OS_ERR_INVAL on a NULL target, a self-join or no thread context,
 *         MINI_OS_ERR_NOMEM when the join semaphore could not be created,
 *         MINI_OS_ERR_AGAIN when timeout_tick is 0 and the target still runs,
 *         MINI_OS_ERR_TIMEOUT when a timed wait expired
 * @details a blocking join publishes a binary join semaphore on the target,
 *          which mini_os_thread_exit() gives when the thread dies; the joiner
 *          then takes it with the requested timeout
 * @note the join pins the corpse (is_detach = MINI_OS_FALSE) so the idle reaper
 *       cannot free the TCB while the return value is still needed; a successful
 *       join releases it again, a timed-out one restores the previous detach state
 *       so no zombie is left behind. Only one joiner at a time is supported, and
 *       mini_os_thread_delete() refuses a pinned thread with MINI_OS_ERR_BUSY
 */
mini_os_err_t mini_os_thread_join(mini_os_thread_t* thread, void** thread_return, mini_os_tick_t timeout_tick)
{
    mini_os_semaphore_t* fresh;
    mini_os_semaphore_t* stale;
    mini_os_semaphore_t* wait_sem;
    mini_os_bool_t       create_sem;
    mini_os_bool_t       was_detach;
    mini_os_bool_t       terminated;
    mini_os_err_t        ret;
    void*                retval;
    mini_os_irq_t        irq;

    if (thread == MINI_OS_NULL || thread == mini_os_current_thread || mini_os_current_thread == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL; /* invalid arg, self-join deadlock or no thread context */

    irq = mini_os_irq_save();
    terminated = thread->is_terminated;
    retval = thread->exit_retval;
    if (terminated || timeout_tick == 0)
    {
        /* nothing to block on: the value is already there or the caller only polls,
         * so no semaphore is created and the corpse is left to the idle reaper */
        mini_os_irq_restore(irq);
        if (!terminated)
            return MINI_OS_ERR_AGAIN;
        if (thread_return != MINI_OS_NULL)
            *thread_return = retval;
        return MINI_OS_OK;
    }
    create_sem = (mini_os_bool_t)(thread->join_wait_sem == MINI_OS_NULL);
    mini_os_irq_restore(irq);

    fresh = MINI_OS_NULL;
    if (create_sem != MINI_OS_FALSE)
    {
        fresh = mini_os_semaphore_create(MINI_OS_NULL, 1u, 0u);
        if (fresh == MINI_OS_NULL)
            return MINI_OS_ERR_NOMEM;
    }

    stale = MINI_OS_NULL;
    irq = mini_os_irq_save();
    if (thread->join_wait_sem == MINI_OS_NULL)
        thread->join_wait_sem = fresh; /* published: mini_os_thread_exit() gives it */
    else
        stale = fresh; /* published while the semaphore was created: wait on that one */
    wait_sem = thread->join_wait_sem;
    /* pin the corpse first, otherwise the idle reaper can free the TCB under us */
    was_detach = thread->is_detach;
    thread->is_detach = MINI_OS_FALSE;
    terminated = thread->is_terminated;
    retval = thread->exit_retval;
    mini_os_irq_restore(irq);

    if (stale != MINI_OS_NULL)
        (void)mini_os_semaphore_delete(stale);

    if (terminated)
        ret = MINI_OS_OK; /* died while the semaphore was being created, no give came */
    else
        ret = mini_os_semaphore_take(wait_sem, timeout_tick);

    irq = mini_os_irq_save();
    terminated = thread->is_terminated;
    retval = thread->exit_retval;
    if (ret == MINI_OS_OK || terminated)
    {
        /* collected: the corpse belongs to the idle reaper again */
        thread->is_detach = MINI_OS_TRUE;
    }
    else
    {
        thread->is_detach = was_detach; /* timed out: do not leave a pinned zombie */
    }
    mini_os_irq_restore(irq);

    if (ret != MINI_OS_OK && !terminated)
        return ret; /* take failed and the target is still alive */
    if (thread_return != MINI_OS_NULL)
        *thread_return = retval;
    return MINI_OS_OK;
}
#endif /* MINI_OS_THREAD_DETACH */

/**
 * @brief Yield the CPU to the next ready thread of the same priority
 * @return MINI_OS_OK always
 */
mini_os_err_t mini_os_thread_yield(void)
{
    mini_os_yield_trigger();
    return MINI_OS_OK;
}

/**
 * @brief Delay the current thread for a number of ticks
 * @param[in] ticks delay length in ticks; 0 returns immediately
 * @return MINI_OS_OK always
 */
mini_os_err_t mini_os_thread_delay_tick(mini_os_uint32_t ticks)
{
    if (ticks == 0)
        return MINI_OS_OK;
    mini_os_schedule_delay(ticks);
    return MINI_OS_OK;
}

/**
 * @brief Delay the current thread for a number of milliseconds
 * @param[in] ms delay length in milliseconds; 0 returns immediately
 * @return MINI_OS_OK always
 * @note the value is converted with MINI_OS_MS_TO_TICK, so the real delay is
 *       rounded to whole ticks
 */
mini_os_err_t mini_os_thread_delay_ms(mini_os_uint32_t ms)
{
    if (ms == 0)
        return MINI_OS_OK;
    mini_os_schedule_delay(MINI_OS_MS_TO_TICK(ms));
    return MINI_OS_OK;
}

/**
 * @brief Delay the current thread until an absolute tick value
 * @param[in] ticks absolute tick value to delay until
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL outside thread context
 * @note returns immediately when the target tick has already passed; the
 *       comparison is tick-wrap safe
 */
mini_os_err_t mini_os_thread_delay_tick_until(mini_os_uint32_t ticks)
{
    mini_os_uint32_t remain;

    if (ticks == 0)
        return MINI_OS_OK;
    if (mini_os_thread_current() == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL; /* thread context only */

    remain = mini_os_tick_until(ticks);
    if (remain == 0u)
        return MINI_OS_OK; /* the target tick has already passed */
    mini_os_schedule_delay(remain);
    return MINI_OS_OK;
}

/**
 * @brief Set the name of a thread
 * @param[in] thread thread to rename
 * @param[in] name new name (bounded copy, always NUL-terminated)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 */
mini_os_err_t mini_os_thread_set_name(mini_os_thread_t* thread, const char* name)
{
    if (!thread)
        return MINI_OS_ERR_INVAL;
    mini_os_set_name(thread->thread_name, name, MINI_OS_THREADS_NAME_LEN);
    return MINI_OS_OK;
}

/**
 * @brief Get the name of a thread
 * @param[in] thread thread to query
 * @param[out] name buffer receiving the NUL-terminated name
 * @param[out] name_len receives the name length in characters (without NUL)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_thread_get_name(mini_os_thread_t* thread, char* name, mini_os_uint32_t* name_len)
{
    if (!thread || !name || !name_len)
        return MINI_OS_ERR_INVAL;
    uint8_t len = 0;
    for (uint8_t i = 0; i < (mini_os_size_t)(MINI_OS_THREADS_NAME_LEN - 1) && thread->thread_name[i] != '\0'; i++)
    {
        name[i] = thread->thread_name[i];
        len++;
    }
    name[len] = '\0';
    *name_len = len;
    return MINI_OS_OK;
}

/**
 * @brief Apply an effective priority without touching the base priority
 * @param[in] thread thread to re-link
 * @param[in] priority effective priority to apply
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 *         or priority is out of range
 * @details a READY/RUNNING thread is re-linked because the ready list is indexed
 *          by priority: the node is removed, the field is changed, the state is
 *          temporarily SUSPENDED (remove leaves it alone and add refuses
 *          READY/RUNNING), the thread is added back and RUNNING is restored when
 *          add() demoted it to READY
 * @note kernel API for mutex priority inheritance: base_priority is left alone,
 *       so the next recompute can still derive the boost from it. Interrupts are
 *       masked across the whole re-link, so nobody observes the transient state
 * @note BLOCKED/SUSPENDED threads only change the field: wait lists are FIFO and
 *       the time wheel is priority-agnostic
 */
mini_os_err_t mini_os_thread_priority_apply(mini_os_thread_t* thread, mini_os_uint8_t priority)
{
    mini_os_thread_state_t state;
    mini_os_irq_t          irq;

    if (!thread || priority >= MINI_OS_PRIORITY)
        return MINI_OS_ERR_INVAL;

    state = thread->state;
    if (priority == thread->priority)
        return MINI_OS_OK;

    irq = mini_os_irq_save();
    if (state == MINI_OS_THREAD_STATE_READY || state == MINI_OS_THREAD_STATE_RUNNING)
    {
        /* re-link: the ready list is indexed by priority, so the bitmap and
         * list head would go stale if the field changed in place */
        (void)mini_os_remove_thread_from_ready_running_list(thread);
        thread->priority = priority;
        /* remove() leaves the state alone and add() refuses READY/RUNNING
         * threads, so mark it off-list for the re-link; interrupts are off here,
         * nobody can observe the transient state */
        thread->state = MINI_OS_THREAD_STATE_SUSPENDED;
        (void)mini_os_add_thread_to_ready_running_list(thread);
        if (state == MINI_OS_THREAD_STATE_RUNNING)
            thread->state = MINI_OS_THREAD_STATE_RUNNING; /* add() demoted to READY */
    }
    else
    {
        /* BLOCKED/SUSPENDED: wait lists are FIFO and the wheel is
         * priority-agnostic, only the field changes */
        thread->priority = priority;
    }
    mini_os_irq_restore(irq);
    return MINI_OS_OK;
}

/**
 * @brief Set the base priority of a thread
 * @param[in] thread thread to configure
 * @param[in] priority new base priority (< MINI_OS_PRIORITY)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 *         or priority is out of range
 * @details the effective priority is not simply the new base: a thread holding
 *          mutexes keeps the boost its waiters still require, and a thread
 *          blocked on a mutex pushes the change on to that mutex's owner
 * @note the final value is therefore produced by
 *       mini_os_mutex_priority_recompute(), whose result is returned here
 */
mini_os_err_t mini_os_thread_set_priority(mini_os_thread_t* thread, mini_os_uint8_t priority)
{
    mini_os_irq_t irq;

    if (!thread || priority >= MINI_OS_PRIORITY)
        return MINI_OS_ERR_INVAL;

    irq = mini_os_irq_save();
    thread->base_priority = priority;
    mini_os_irq_restore(irq);

    /* the effective priority is not simply the new base: a thread holding
     * mutexes keeps the boost its waiters still require, and a thread blocked
     * on a mutex pushes the change on to that mutex's owner */
    return mini_os_mutex_priority_recompute(thread);
}

/**
 * @brief Get the effective priority of a thread
 * @param[in] thread thread to query
 * @param[out] priority receives the effective priority (inheritance included)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_thread_get_priority(mini_os_thread_t* thread, mini_os_uint8_t* priority)
{
    if (!thread || !priority)
        return MINI_OS_ERR_INVAL;
    *priority = thread->priority;
    return MINI_OS_OK;
}

/**
 * @brief Get the state of a thread
 * @param[in] thread thread to query
 * @param[out] state receives the current thread state
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_thread_get_state(mini_os_thread_t* thread, mini_os_thread_state_t* state)
{
    if (!thread || !state)
        return MINI_OS_ERR_INVAL;
    *state = thread->state;
    return MINI_OS_OK;
}

/**
 * @brief Set the user data word of a thread
 * @param[in] thread thread to configure
 * @param[in] user_data value to store
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 */
mini_os_err_t mini_os_thread_set_user_data(mini_os_thread_t* thread, mini_os_user_data_t user_data)
{
    if (!thread)
        return MINI_OS_ERR_INVAL;
    thread->user_data = user_data;
    return MINI_OS_OK;
}

/**
 * @brief Get the user data word of a thread
 * @param[in] thread thread to query
 * @param[out] user_data receives the stored value
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on a NULL argument
 */
mini_os_err_t mini_os_thread_get_user_data(mini_os_thread_t* thread, mini_os_user_data_t* user_data)
{
    if (!thread || !user_data)
        return MINI_OS_ERR_INVAL;
    *user_data = thread->user_data;
    return MINI_OS_OK;
}

/**
 * @brief Set the cleanup function of a thread
 * @param[in] thread thread to configure
 * @param[in] cleanup function run by the entry wrapper after the entry returns
 * @param[in] arg unused: the cleanup is invoked with the thread entry param
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 */
mini_os_err_t mini_os_thread_set_cleanup(mini_os_thread_t* thread, void (*cleanup)(void*), void* arg)
{
    (void)arg; /* cleanup is invoked with the thread entry param by the wrapper */
    if (!thread)
        return MINI_OS_ERR_INVAL;
    thread->thread_cleanup = cleanup;

    return MINI_OS_OK;
}

#if MINI_OS_FIND_BY_NAME
/**
 * @brief Find a thread by name
 * @param[in] name name to look for
 * @return thread handle on success; MINI_OS_NULL when name is MINI_OS_NULL or
 *         no thread carries that name
 */
mini_os_thread_t* mini_os_find_by_name(const char* name)
{
    if (!name)
        return MINI_OS_NULL;
    for (mini_os_list_t* node = g_threads_list.next; node != &g_threads_list; node = node->next)
    {
        mini_os_thread_t* thread = mini_os_container_of(node, mini_os_thread_t, g_list_node);
        if (MINI_OS_STRCMP(thread->thread_name, name) == 0)
            return thread;
    }
    return MINI_OS_NULL;
}
#endif
#if MINI_OS_TIME_SLICE
/**
 * @brief Set the time slice of a thread (ticks per round-robin quantum)
 * @param[in] thread thread to configure
 * @param[in] tick slice length in ticks; 0 = no slice limit
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread is MINI_OS_NULL
 * @note only has an effect when MINI_OS_TIME_SLICE is enabled
 */
mini_os_err_t mini_os_thread_set_timeslice(mini_os_thread_t* thread, mini_os_tick_t tick)
{
    if (thread == MINI_OS_NULL || tick < 0)
        return MINI_OS_ERR_INVAL;
    thread->init_tick_num = tick;
    thread->remain_tick = tick;
    return MINI_OS_OK;
}

/**
 * @brief Get the configured time slice of a thread
 * @param[in] thread thread to query
 * @param[out] tick receives the configured slice length in ticks
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when thread/tick is MINI_OS_NULL
 */
mini_os_err_t mini_os_thread_get_timeslice(mini_os_thread_t* thread, mini_os_tick_t* tick)
{
    if (thread == MINI_OS_NULL || tick == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    *tick = thread->init_tick_num;
    return MINI_OS_OK;
}
#endif

/**
 * @brief Default idle hook body: reap corpses, check the stack, run the hook, sleep
 * @param[in] hook user hook run once per idle pass (MINI_OS_NULL = none)
 * @param[in] param argument passed to the hook
 * @return never returns
 * @note weak default: override it to change the idle behaviour
 */
MINI_OS_WEAK mini_os_err_t mini_os_thread_idle_hook(idle_hook_t hook, void* param)
{
    while (1)
    {
        mini_os_thread_defunct_execute(); /* reclaim terminated threads */
#if MINI_OS_STACK_OVERFLOW_CHECK
        mini_os_stack_overflow_check(); /* halt when the system stack ran over */
#endif
        if (hook != MINI_OS_NULL)
            hook(param);
        mini_os_wfi();
    }
    return MINI_OS_OK;
}

/**
 * @brief Default idle thread body (runs the idle hook loop)
 * @param[in] param argument forwarded to the idle hook
 */
MINI_OS_WEAK void mini_os_thread_idle(void* param) { mini_os_thread_idle_hook(MINI_OS_NULL, param); }

/**
 * @brief Create the idle thread at the lowest priority
 * @note weak default: override it to customize the idle thread. The idle thread
 *       must never block, so it is created at MINI_OS_PRIORITY - 1
 */
MINI_OS_WEAK void mini_os_thread_idle_create(void) { s_idle_thread = mini_os_thread_create(MINI_OS_IDLE_THREAD_NAME, MINI_OS_DEFAULT_IDLE_STACK_SIZE, MINI_OS_PRIORITY - 1, mini_os_thread_idle, MINI_OS_NULL); }
