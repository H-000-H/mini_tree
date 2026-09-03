/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file critical.h
 * @author H-000-H
 * @brief mini-os critical section macros
 * @details
 *   - included by redef.h after the mini_os_irq_* prototypes; do not include
 *     it standalone before redef.h (the macros expand to those functions)
 */
#ifndef CRITICAL_H
#define CRITICAL_H

/**
 * @brief Enter critical section not supported nested
 */
#define MINI_OS_ENTER_CRITICAL() mini_os_irq_disable()

/**
 * @brief Exit critical section not supported nested
 */
#define MINI_OS_EXIT_CRITICAL() mini_os_irq_enable()

/**
 * @brief Enter critical section strict mode supported nested
 */
#define MINI_OS_ENTER_CRITICAL_STRICT() mini_os_irq_save()

/**
 * @brief Exit critical section strict mode supported nested
 */
#define MINI_OS_EXIT_CRITICAL_STRICT(irq_level) mini_os_irq_restore(irq_level)

#endif /* CRITICAL_H */
