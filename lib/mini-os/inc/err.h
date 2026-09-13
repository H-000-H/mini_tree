/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file err.h
 * @brief Error codes
 * @author H-000-H
 */
#ifndef ERR_H
#define ERR_H
/**
 * @brief Error codes if config.h and compiler_compat.h are available using status.h otherwise using
 * self defined error codes
 */
#include "redef.h"
// clang-format off
#if __has_include(<config.h>) && __has_include(<compiler_compat.h>)
#include <status.h>
#define MINI_OS_OK MINI_OK
#define MINI_OS_ERR_INVAL MINI_ERR_INVAL       /**<invalid parameter */
#define MINI_OS_ERR_ISR MINI_ERR_ISR           /**<interrupt context illegal call */
#define MINI_OS_ERR_NOMEM MINI_ERR_NOMEM       /**<memory insufficient */
#define MINI_OS_ERR_IO MINI_ERR_IO             /**<physical IO error */
#define MINI_OS_ERR_BUSY MINI_ERR_BUSY         /**<device busy */
#define MINI_OS_ERR_AGAIN MINI_ERR_AGAIN       /**<retry */
#define MINI_OS_ERR_NOSPC MINI_ERR_NOSPC       /**<no remaining space/channel */
#define MINI_OS_ERR_TIMEOUT MINI_ERR_TIMEOUT   /**<lock acquisition/operation timeout */
#define MINI_OS_ERR_HW_FATAL MINI_ERR_HW_FATAL /**<hardware physical fault, unrecoverable */
#define MINI_OS_ERR_DEFER MINI_ERR_DEFER       /**<dependency not ready, retry later */
#define MINI_OS_ERR_NODEV MINI_ERR_NODEV       /**<device removed or not exist */
#define MINI_OS_ERR_NOTSUPP MINI_ERR_NOTSUPP   /**<operation not supported/implemented */
#define MINI_OS_ERR_STATE MINI_ERR_STATE       /**<invalid state transition */

/* ── 内核私有码 (mini-os 片: -256..-287) ── */
#define MINI_OS_ERR_DEAD (-256)        /**<thread terminated / deleted (killed) */
#define MINI_OS_ERR_NOTREADY (-257)    /**<scheduler not started or no ready thread */
#define MINI_OS_ERR_RANGE (-258)       /**<priority or parameter out of range */
#define MINI_OS_ERR_DESTROYED (-259)   /**<object destroyed, or storage variant mismatch */
#define MINI_OS_ERR_OVERFLOW (-260)    /**<recursion depth / counter overflow */
#define MINI_OS_ERR_CORRUPT (-261)     /**<heap block corrupt / double free / bad pointer */
#define MINI_OS_ERR_PERM (-262)        /**<not the owner (e.g. unlocking someone else's mutex) */
#define MINI_OS_ERR_NOT_STARTED (-263) /**<timer not started (or already stopped) */
#else
#define MINI_OS_OK 0
#define MINI_OS_ERR_INVAL -1                    /**<invalid parameter */
#define MINI_OS_ERR_ISR -2                      /**<interrupt context illegal call */
#define MINI_OS_ERR_NOMEM -3                    /**<memory insufficient */
#define MINI_OS_ERR_IO -4                       /**<physical IO error */
#define MINI_OS_ERR_BUSY -5                     /**<device busy */
#define MINI_OS_ERR_AGAIN -6                    /**<retry */
#define MINI_OS_ERR_NOSPC -7                    /**<no remaining space/channel */
#define MINI_OS_ERR_TIMEOUT -8                  /**<lock acquisition/operation timeout */
#define MINI_OS_ERR_HW_FATAL -9                 /**<hardware physical fault, unrecoverable */
#define MINI_OS_ERR_DEFER -10                   /**<dependency not ready, retry later */
#define MINI_OS_ERR_NODEV -11                   /**<device removed or not exist */
#define MINI_OS_ERR_NOTSUPP -12                 /**<operation not supported/implemented */
#define MINI_OS_ERR_STATE -13                   /**<invalid state transition */

/* ── 内核私有码 (mini-os 片: -256..-287) ── */
#define MINI_OS_ERR_DEAD (-256)        /**<thread terminated / deleted (killed) */
#define MINI_OS_ERR_NOTREADY (-257)    /**<scheduler not started or no ready thread */
#define MINI_OS_ERR_RANGE (-258)       /**<priority or parameter out of range */
#define MINI_OS_ERR_DESTROYED (-259)   /**<object destroyed, or storage variant mismatch */
#define MINI_OS_ERR_OVERFLOW (-260)    /**<recursion depth / counter overflow */
#define MINI_OS_ERR_CORRUPT (-261)     /**<heap block corrupt / double free / bad pointer */
#define MINI_OS_ERR_PERM (-262)        /**<not the owner (e.g. unlocking someone else's mutex) */
#define MINI_OS_ERR_NOT_STARTED (-263) /**<timer not started (or already stopped) */
#endif
// clang-format on
#endif
