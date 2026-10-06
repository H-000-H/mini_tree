/**
 * @file spinlock.h
 * @author H-000-H
 * @brief Header-only spinlock (two modes, selected by MINI_OS_SPINLOCK_ATOMIC)
 * @note Off by default (MINI_OS_SPINLOCK = 0): when off, this header defines no
 *       entity at all. Enable it with CONFIG_MINI_OS_SPINLOCK=1 (injected from
 *       config/Kconfig) or by predefining it externally.
 * @note Atomic mode (MINI_OS_SPINLOCK_ATOMIC = 1): TAS spinning for multi-core /
 *       cross-core use. Not reentrant - recursive locking self-deadlocks - and
 *       the critical section must be extremely short. The retry path is a plain
 *       delay + schedule_yield by default; CONFIG_MINI_OS_SPINLOCK_YIELD=1 turns
 *       on the for-yield polling loop.
 * @note Single-core mode (default): IRQ-masked critical section + nesting count,
 *       equivalent to a reentrant critical-section lock.
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef SPINLOCK_H
#define SPINLOCK_H
#include "mini_config.h"

#if MINI_OS_SPINLOCK /* default 0: compiled only with CONFIG_MINI_OS_SPINLOCK=1 (or an external predefinition) */
#ifdef __cplusplus
extern "C"
{
#endif
#include "err.h"
#include "port.h"
#include "redef.h"
#include "schedule.h"

typedef struct mini_os_spinlock mini_os_spinlock_t;

struct mini_os_spinlock
{
#if MINI_OS_SPINLOCK_ATOMIC
    mini_os_atomic_int8_t locked; /**< 0 = unlocked, 1 = locked (atomic mode) */
#else
    mini_os_irq_t   irq;  /**< interrupt state saved at the outermost lock (single-core mode) */
    mini_os_uint8_t nest; /**< nesting depth (single-core mode) */
#endif /* MINI_OS_SPINLOCK_ATOMIC */
};

/**
 * @brief Initialize a spinlock
 * @param[in] spinlock spinlock to initialize
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when the argument is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_init(mini_os_spinlock_t* spinlock)
{
    if (spinlock == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    MINI_OS_ATOMIC_STORE(&spinlock->locked, 0, MINI_OS_RELAXED);
#else
    spinlock->irq = 0;
    spinlock->nest = 0;
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}

/**
 * @brief Acquire the lock
 * @param[in] spinlock target spinlock
 * @return MINI_OS_OK on success (atomic mode: possibly only after spinning/yielding)
 * @note Atomic mode TAS convention: TRUE means the lock was already taken; the
 *       loop exits only once the lock is acquired (FALSE). Single-core mode is
 *       reentrant (nesting count) and saves the interrupt restore point at the
 *       outermost level.
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_lock(mini_os_spinlock_t* spinlock)
{
    if (spinlock == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    while (MINI_OS_ATOMIC_TEST_AND_SET(&spinlock->locked, MINI_OS_ACQUIRE))
    {
#if MINI_OS_SPINLOCK_YIELD /* default 0: with for-yield off, a plain delay + schedule_yield is enough */
        mini_os_uint32_t i;

        for (i = 0; i < MINI_OS_SPINLOCK_NUM; i++)
        {
            if (MINI_OS_ATOMIC_LOAD(&spinlock->locked, MINI_OS_RELAXED) == 0)
                break;       /* looks released: go back to the TAS retry */
            mini_os_pause(); /* spin-wait hint (port.S: yield instruction) */
        }
#else
        mini_os_pause(); /* plain delay (port.S: yield instruction), no polling */
#endif                                  /* MINI_OS_SPINLOCK_YIELD */
        (void)mini_os_schedule_yield(); /* yield the CPU so the lock holder is not starved */
    }
#else
    {
        mini_os_irq_t irq = mini_os_irq_save();

        if (spinlock->nest == 0u)
            spinlock->irq = irq; /* only the outermost level needs to remember the restore point */
        spinlock->nest++;
    }
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}

/**
 * @brief Release the lock
 * @param[in] spinlock target spinlock
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when the argument is NULL or
 *         the lock is not held (single-core mode)
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_unlock(mini_os_spinlock_t* spinlock)
{
    if (spinlock == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    MINI_OS_ATOMIC_STORE(&spinlock->locked, 0, MINI_OS_RELEASE);
#else
    if (spinlock->nest == 0u)
        return MINI_OS_ERR_INVAL; /* unlock without a matching lock */
    spinlock->nest--;
    if (spinlock->nest == 0u)
        mini_os_irq_restore(spinlock->irq);
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}

/**
 * @brief Report whether the spinlock is held
 * @param[in] spinlock target spinlock
 * @param[out] locked receives the result: MINI_OS_TRUE = held
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when an argument is NULL
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_islocked(mini_os_spinlock_t* spinlock, mini_os_bool_t* locked)
{
    if (spinlock == MINI_OS_NULL || locked == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    *locked = (MINI_OS_ATOMIC_LOAD(&spinlock->locked, MINI_OS_RELAXED) != 0) ? MINI_OS_TRUE : MINI_OS_FALSE;
#else
    *locked = (spinlock->nest > 0u) ? MINI_OS_TRUE : MINI_OS_FALSE;
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}
#ifdef __cplusplus
}
#endif
#endif /* MINI_OS_SPINLOCK */
#endif /* SPINLOCK_H */
