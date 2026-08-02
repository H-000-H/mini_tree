/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @License: Apache-2.0
 * @file osal_null.h
 * @brief 裸机后端移植辅助接口
 * @details 仅 CONFIG_OSAL_NULL 后端使用, 提供 ISR 入口/出口
 * @details osal_null_isr_enter/exit 维护 ISR 嵌套计数, 驱动 osal_in_isr() 判定
 * @details 单调 ms 时钟由 time_slice/task 的 scheduler tick_count 提供
 */
#ifndef OSAL_NULL_H
#define OSAL_NULL_H

#ifndef CONFIG_OSAL_NULL
#error "osal_null.h requires CONFIG_OSAL_NULL"
#endif

#include "xtask.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * @brief 周期任务包装结构体
     * @details 用于包装周期任务的原始回调函数和对应的任务控制块指针
     * @details 用于包装周期任务的周期时间
     */
    struct osal_periodic_task_wrap
    {
        void (*orig_callback)(x_task*); /**< 原始的回调函数(x_task*) */
        x_task* x_task; /**< 对应的任务控制块指针 */
        uint32_t period_ms; /**< 周期时间 */
    };
    /**
     * @brief 在 ISR 入口调用
     * @return void
     * @details 在 ISR 入口调用时, 使用 osal_null_isr_enter 在 ISR 入口调用
     */
    void osal_null_isr_enter(void);

    /**
     * @brief 在 ISR 出口调用
     * @return void
     * @details 在 ISR 出口调用时, 使用 osal_null_isr_exit 在 ISR 出口调用
     */
    void osal_null_isr_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_NULL_H */