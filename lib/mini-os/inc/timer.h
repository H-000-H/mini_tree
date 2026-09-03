/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file timer.h
 * @brief Timer interface
 * @author H-000-H
 * @note only sample things to use soft timer.
 */
#ifndef TIMER_H
#define TIMER_H
#include "list.h"
#include <redef.h>
#if defined(__cplusplus)
extern "C"
{
#endif
typedef void (*mini_os_timer_callback)(void* param);
typedef struct mini_os_timer mini_os_timer_t;
#define MINI_OS_TIMER_FLAG_ONE_SHOT 0x00
#define MINI_OS_TIMER_FLAG_PERIODIC 0x01

#define MINI_OS_TIMER_FLAG_HARD 0x00
#define MINI_OS_TIMER_FLAG_SOFT 0x02

#define MINI_OS_TIMER_FLAG_ACTIVE 0x04
#define MINI_OS_TIMER_FLAG_INACTIVE 0x00

/**
 * @brief Structure representing a mini-os timer
 */
typedef struct mini_os_timer
{
    mini_os_list_t         list_node;                            /**< list node for timer */
    char                   timer_name[MINI_OS_THREADS_NAME_LEN]; /**< timer name */
    mini_os_timer_callback callback;                             /**< callback function */
    void*                  arg;                                  /**< callback function argument */
    mini_os_tick_t         trigger_tick;                         /**< next timer trigger tick */
    mini_os_uint32_t       round;                                /**< timer round */
    mini_os_uint8_t        flag;                                 /**< bit 0: one-shot/periodic; bit 1: hard/soft /bit 2: is active */
#if MINI_OS_FIND_BY_NAME
    mini_os_list_t list_name_node; /**< list node for timer name */
#endif
} mini_os_timer_t;

/**
 * @brief Create a timer on the heap (created stopped)
 * @param[in] name timer name (MINI_OS_NULL = empty name)
 * @param[in] cb callback run when the deadline hits
 * @param[in] arg argument passed to the callback
 * @param[in] trigger_tick period in ticks (> 0)
 * @param[in] trigger_num MINI_OS_TIMER_FLAG_ONE_SHOT or MINI_OS_TIMER_FLAG_PERIODIC
 * @param[in] trigger_mode MINI_OS_TIMER_FLAG_HARD (callback runs in the tick/ISR
 *            context) or MINI_OS_TIMER_FLAG_SOFT (callback runs on the timer
 *            service thread)
 * @return timer handle on success; MINI_OS_NULL on invalid arguments, an invalid
 *         selector pair, or out of memory
 * @note the timer only starts counting once mini_os_timer_start() is called
 */
mini_os_timer_t* mini_os_timer_create(const char* name, mini_os_timer_callback cb, void* arg, mini_os_tick_t trigger_tick, mini_os_uint8_t trigger_num, mini_os_uint8_t trigger_mode);

/**
 * @brief Create a timer over caller-provided storage (created stopped)
 * @param[in] name timer name (MINI_OS_NULL = empty name)
 * @param[in] cb callback run when the deadline hits
 * @param[in] arg argument passed to the callback
 * @param[in] trigger_tick period in ticks (> 0)
 * @param[in] trigger_num MINI_OS_TIMER_FLAG_ONE_SHOT or MINI_OS_TIMER_FLAG_PERIODIC
 * @param[in] trigger_mode MINI_OS_TIMER_FLAG_HARD or MINI_OS_TIMER_FLAG_SOFT
 * @param[in] timer storage for the timer descriptor
 * @return timer handle on success; MINI_OS_NULL on invalid arguments or an
 *         invalid selector pair
 */
mini_os_timer_t* mini_os_timer_create_static(const char* name, mini_os_timer_callback cb, void* arg, mini_os_tick_t trigger_tick, mini_os_uint8_t trigger_num, mini_os_uint8_t trigger_mode, mini_os_timer_t* timer);

/**
 * @brief Stop a heap timer and free its descriptor
 * @param[in] timer timer to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_delete(mini_os_timer_t* timer);

/**
 * @brief Stop a static timer (never frees the caller storage)
 * @param[in] timer timer to delete
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_delete_static(mini_os_timer_t* timer);

/**
 * @brief (Re)start a timer (counts the full period from now)
 * @param[in] timer timer to start
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL or
 *         its period is not positive
 * @note a SOFT timer spawns the timer service thread on its first start, so this
 *       is a thread/main context call for SOFT timers
 */
mini_os_err_t mini_os_timer_start(mini_os_timer_t* timer);

/**
 * @brief Stop a timer (keeps the descriptor and its configuration)
 * @param[in] timer timer to stop
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_stop(mini_os_timer_t* timer);

/**
 * @brief Change the period of a timer
 * @param[in] timer timer to configure
 * @param[in] trigger_tick new period in ticks (> 0)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer is MINI_OS_NULL or
 *         the period is not positive
 * @note a running timer is re-armed with the new period immediately
 */
mini_os_err_t mini_os_timer_set_trigger_tick(mini_os_timer_t* timer, mini_os_tick_t trigger_tick);

/**
 * @brief Change the callback of a timer
 * @param[in] timer timer to configure
 * @param[in] cb new callback
 * @param[in] arg argument passed to the new callback
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when timer or cb is MINI_OS_NULL
 */
mini_os_err_t mini_os_timer_set_callback(mini_os_timer_t* timer, mini_os_timer_callback cb, void* arg);

/**
 * @brief Advance the timer wheel by one tick and service expired timers
 * @note kernel API, called from the SysTick handler (mini_os_systick_handler)
 *       once per OS tick while interrupts are masked:
 *       - a HARD timer runs its callback right here in tick (ISR) context;
 *       - a SOFT timer is queued and its callback runs later on the timer
 *         service thread (woken through a binary semaphore).
 */
void mini_os_timer_tick(void);

#if defined(__cplusplus)
}
#endif
#endif
