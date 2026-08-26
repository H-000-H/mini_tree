/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file osal_null.h
 *@brief 裸机后端移植辅助接口
 *@author H-000-H
 *@details
 *   仅 CONFIG_OSAL_NULL 后端使用, 提供 ISR 入口/出口。
 *   osal_null_isr_enter/exit 维护 ISR 嵌套计数, 驱动 osal_in_isr() 判定;
 *   单调 ms 时钟由 time_slice/task 的 scheduler tick_count 提供。
 */

#ifndef OSAL_NULL_H
#define OSAL_NULL_H

#ifndef CONFIG_OSAL_NULL
#error "osal_null.h requires CONFIG_OSAL_NULL"
#endif

#include "compiler_compat.h"
#include "compiler_inline.h"
#include <stdint.h>

/* 前置声明: osal_periodic_task_wrap 仅持有 TCB 指针, 无需完整 xtask.h */
struct x_task;
#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * @brief 周期任务包装: 保存原始回调、TCB 指针与周期
     */
    struct osal_periodic_task_wrap
    {
        void (*orig_callback)(struct x_task*); /**< 原始的回调函数(x_task*) */
        struct x_task* x_task; /**< 对应的任务控制块指针 */
        uint32_t period_ms; /**< 周期时间 (ms) */
    };
    /**
     * @brief 在 ISR 入口调用 (维护嵌套计数)
     */
    void osal_null_isr_enter(void);

    /**
     * @brief 在 ISR 出口调用 (维护嵌套计数)
     */
    void osal_null_isr_exit(void);

    /**
     * @brief 关全局中断并保存中断使能状态 (裸机临界区入口)
     * @return 进入前的中断状态 (Cortex-M: PRIMASK; RISC-V: mstatus)
     * @note 必须与 osal_null_irq_restore 成对使用, 支持嵌套
     */
    MINI_STATIC_INLINE uint32_t osal_null_irq_disable(void)
    {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) ||           \
    defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)
        uint32_t primask;
        __asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(primask)::"memory");
        return primask;
#elif defined(__riscv)
    uintptr_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
    return (uint32_t)mstatus;
#else
    return 0U;
#endif
    }

    /**
     * @brief 恢复全局中断 (裸机临界区出口)
     * @param[in] state osal_null_irq_disable 返回的中断状态
     */
    MINI_STATIC_INLINE void osal_null_irq_restore(uint32_t state)
    {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) ||           \
    defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)
        __asm__ volatile("msr primask, %0" ::"r"(state) : "memory");
#elif defined(__riscv)
    if (state & 8U)
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
#else
   MINI_UNUSED_PARAM(state);
#endif
    }

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
/* C++ 下必须提供 x_task_handle_t / x_task* (osal_task.cpp 与 osal_task_create
 * 重载声明依赖); 纯 C 文件 (如 lwIP sys_arch.c) 不进此段, 不会拉入
 * xtask -> hal 整条依赖链。 */
#include "xtask.h"

#if defined(CONFIG_OSAL_NULL_TASK_CPP)
#include "etl/optional.h"

#ifndef CONFIG_XTASK_PREEMPT
/**
 * @brief 裸机专用 C++ 重载 osal_task_create (协调式, 定义见 osal_task.cpp)
 * @param[in] name 任务名
 * @param[in] stack_size 任务周期 period (ms) (裸机忽略栈)
 * @param[in] period 任务周期 (ms), 协调式无优先级概念
 * @param[in] entry 任务入口回调 (void(*)(x_task*), 参数为 x_task* TCB 本身)
 * @param[in] param1 任务参数 1 (可空)
 * @param[in] param2 任务参数 2 (默认 nullptr)
 * @param[in] core_id 核心号 (默认 -1)
 * @return 任务句柄 optional; 创建失败为空
 * @note 裸机 C API (osal.h) 恒返回 OSAL_ERR_NOTSUPP, 任务创建请走本重载 (xtask 周期任务)
 */
etl::optional<x_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,
                                                uint32_t period, void (*entry)(x_task*),
                                                void* param1, void* param2 = nullptr,
                                                int core_id = -1);
#else
/**
 * @brief 裸机专用 C++ 重载 osal_task_create (抢占式, 定义见 osal_task.cpp)
 * @param[in] name 任务名
 * @param[in] stack_size 任务周期 period (ms) (裸机忽略栈)
 * @param[in] priority 优先级 0..X_PREEMPT_PRIO_LEVELS-1, 越大越优先
 * @param[in] entry 任务入口回调 (void(*)(x_task*), 与 xtask 一致)
 * @param[in] param1 忽略 (抢占式任务池内部自分配)
 * @param[in] param2 任务参数 2 (默认 nullptr)
 * @param[in] core_id 核心号 (默认 -1)
 * @return 任务句柄 optional; 创建失败为空
 */
etl::optional<x_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,
                                                uint32_t priority, void (*entry)(x_task*),
                                                void* param1, void* param2 = nullptr,
                                                int core_id = -1);
#endif /* CONFIG_XTASK_PREEMPT */
#endif /* CONFIG_OSAL_NULL_TASK_CPP */
#endif /* __cplusplus */

#ifdef CONFIG_XTASK_COROUTINE
/**
 * @brief 协程让出式延时 (osal 命名宏别名)
 * @param[in] task 任务 TCB 指针 (x_task*)
 * @param[in] ms 延时毫秒数
 * @note 仅任务回调内可用, 本质为 PT_DELAY 宏转发: 挂起当前任务到到期时刻,
 *       其他任务继续跑, 到期后从让出点恢复。
 * @note 必须用宏 (非函数) 才能在调用处展开 case __LINE__ 让出点;
 *       主循环 / 非协程上下文请用 osal_delay_ms(ms) (WFI 忙等)。
 */
#define osal_delay_coro(task, ms) PT_DELAY((task), (ms))
#endif /* CONFIG_XTASK_COROUTINE */

#endif /* OSAL_NULL_H */