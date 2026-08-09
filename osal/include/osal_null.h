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

#if defined(__cplusplus) && defined(CONFIG_OSAL_NULL_TASK_CPP)
#include "etl/optional.h"

#ifndef CONFIG_XTASK_PREEMPT
/* ── 裸机专用 C++ 重载: osal_task_create (协调式, 定义见 osal_task.cpp) ──
 * 由 Kconfig CONFIG_OSAL_NULL_TASK_CPP 控制 (依赖 SYSTEM_CPP, 默认开启):
 * 开启 = 走统一 OSAL 路径; 关闭 = 靠 xtask 自己 (不编译本封装, 直接调 xtask API).
 * 裸机 C API (osal.h) 恒返回 OSAL_ERR_NOTSUPP, 任务创建请走本重载 (xtask 周期任务).
 * 入口函数签名与 xtask 回调一致 (void(*)(x_task*), 参数为 x_task* TCB 本身), 无需任何函数指针转换.
 *
 * 协调式: period 参数为任务周期 (ms), 无优先级概念. */
etl::optional<x_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,uint32_t period, void (*entry)(x_task*),void* param1, void* param2=nullptr, int core_id=-1);
#else
/* ── 裸机专用 C++ 重载: osal_task_create (抢占式, 定义见 osal_task.cpp) ──
 * 参数语义 (对齐 OSAL C API 的 priority 位置):
 *   name      任务名
 *   stack_size 复用为任务周期 period (ms) (裸机忽略栈)
 *   priority  优先级 0..X_PREEMPT_PRIO_LEVELS-1, 越大越优先
 *   entry     void(*)(x_task*) 回调, 与 xtask 一致
 *   param1    忽略 (抢占式任务池内部自分配)
 * 入口 void(*)(x_task*) 与 xtask 回调一致. */
etl::optional<x_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,uint32_t priority, void (*entry)(x_task*),void* param1, void* param2=nullptr, int core_id=-1);
#endif
#endif

#endif /* OSAL_NULL_H */