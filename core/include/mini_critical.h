/**
 * @file mini_critical.h
 * @author H-000-H
 * @brief 可嵌套临界区 (保存/恢复中断现场)
 * @note  嵌套安全: 内层 exit 只恢复到内层 enter 前状态
 * @note  不要与 hal_irq_disable_all (非嵌套版) 混用; 不要用 MINI_ATOMIC_IRQ_SAVE (平台不一定存在)
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINI_CRITICAL_H
#define MINI_CRITICAL_H

#include "compiler_compat.h"
#include "compiler_inline.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 临界区中断现场类型
 * @details ARM (M profile): PRIMASK 值; RISC-V: mstatus 值;
 *          不支持的目标恒为 0 (临界区退化为空操作)。
 */
typedef uint32_t mini_irq_state_t;

/**
 * @brief 进入可嵌套临界区 (保存现场并关中断)
 * @return 进入前的中断使能现场, 必须原样传给 mini_critical_exit
 * @note 支持嵌套; 与 mini_critical_exit 严格成对
 * @note 临界区内禁止调用任何可能阻塞的接口 (互斥锁 / 信号量 / 队列 / 延时)
 */
MINI_STATIC_INLINE mini_irq_state_t mini_critical_enter(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) ||                            \
    defined(__ARM_ARCH_8M_MAIN__) || defined(__CORTEX_M)
    mini_irq_state_t primask;
    __asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(primask) : : "memory");
    return primask;
#elif defined(__riscv)
    uintptr_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
    return (mini_irq_state_t)mstatus;
#else
    /* 未支持的目标 (含 host / x86 构建): 退化为空临界区, 由调用方的
     * 单线程运行假设保证正确性。 */
    return 0U;
#endif
}

/**
 * @brief 退出可嵌套临界区 (写回中断现场)
 * @param[in] state mini_critical_enter 返回的现场值
 * @note 必须与同一次 enter 的返回值配对; 跨层乱序恢复会破坏嵌套语义
 */
MINI_STATIC_INLINE void mini_critical_exit(mini_irq_state_t state)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) ||                            \
    defined(__ARM_ARCH_8M_MAIN__) || defined(__CORTEX_M)
    __asm__ volatile("msr primask, %0" ::"r"(state) : "memory");
#elif defined(__riscv)
    if ((state & 8U) != 0U)
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
#else
    MINI_UNUSED_PARAM(state);
#endif
}

/**
 * @brief 单向关中断 (不可恢复)
 * @details 只关不存, 调用后不会返回"关中断之前"的状态。
 *          用于 fail-fast 前的冻结路径: 进入后系统即进入安全死锁状态, 等待外部看门狗复位。
 * @note 需要可恢复的临界区请用 mini_critical_enter / mini_critical_exit
 */
MINI_STATIC_INLINE void mini_irq_disable(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) ||                            \
    defined(__ARM_ARCH_8M_MAIN__) || defined(__CORTEX_M)
    __asm__ volatile("cpsid i" ::: "memory");
#elif defined(__riscv)
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
#else
    /* 未支持的目标: 无操作 (见 mini_critical_enter 的同款说明) */
#endif
}

/**
 * @brief 单向开中断 (不可恢复)
 * @details 与 mini_irq_disable 对称, 不参考任何保存的现场。
 *          仅用于两个场景:
 *            1. 显式要求"无条件打开中断"的裸机初始化路径;
 *            2. 为 mini-os 内存模块单文件复用提供 port 汇编等价实现
 *               (port.S 的 mini_os_irq_enable 即 cpsie i)。
 * @warning 不要在临界区内调用它, 那会提前打开外层仍在保护的区间;
 *          可恢复的配对请用 mini_critical_enter / mini_critical_exit
 */
MINI_STATIC_INLINE void mini_irq_enable(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) ||                            \
    defined(__ARM_ARCH_8M_MAIN__) || defined(__CORTEX_M)
    __asm__ volatile("cpsie i" ::: "memory");
#elif defined(__riscv)
    __asm__ volatile("csrsi mstatus, 8" ::: "memory");
#else
    /* 未支持的目标: 无操作 */
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* MINI_CRITICAL_H */
