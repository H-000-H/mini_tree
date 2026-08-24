/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_amp.h
 *@brief hal amp 头文件
 *@author H-000-H
 *@details
 *   CPU HAL 层 — 硬件抽象接口 (STM32/CH32)
 *   职责: CPU 紧急停止、AMP 启动、ISR 检测、NVIC/全局中断控制。
 *   所有中断控制 API 为 inline, 直接操作 NVIC/PRIMASK 寄存器, 零开销。
 *   平台差异:
 *   - ARM Cortex-M: 通过 MRS/MSR 访问 IPSR/PRIMASK
 *   - RISC-V:       通过 CSR 访问 MCAUSE
 */

#ifndef HAL_CPU_H
#define HAL_CPU_H

#include "compiler_compat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*CPU 紧急停止与 AMP API*/
    /* -------------------------------------------------------------------------- */
    /**
     * @brief CPU 紧急停止全部核心
     * @note 单核模式: 仅关当前核心中断; 双核模式: 关中断 + 跨核暂停 (挂起对端核心)
     */
    void hal_cpu_emergency_stop_all_cores(void);

    /**
     * @brief 从核启动入口 (AMP 模式下由主核引导调用)
     */
    void hal_cpu_secondary_startup(void);
    /**
     * @brief 裸金属主循环入口 (无 RTOS 时)
     */
    void hal_cpu_baremetal_entry(void);
    /**
     * @brief 获取当前核心 ID
     * @return 核心编号 (0=主核, 1=从核)
     */
    int hal_cpu_get_id(void);
    /* -------------------------------------------------------------------------- */

    /*ISR 检测 inline*/
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 判断当前是否处于中断上下文 (读 IPSR/MCAUSE)
     * @return 非零 = 在 ISR; 0 = 在任务上下文
     */
    COMPAT_STATIC_INLINE int hal_is_in_isr(void)
    {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) ||           \
    defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)
        int ipsr;
        __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
        return ipsr;
#elif defined(__riscv)
    int mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    return mcause;
#else
    return 0;
#endif
    }

#ifndef DEBUG
#define HAL_ASSERT_NOT_ISR() ((void)0)
#else
#include "compiler_compat.h"
#define HAL_ASSERT_NOT_ISR()                                                                       \
    do                                                                                             \
    {                                                                                              \
        if (hal_is_in_isr())                                                                       \
        {                                                                                          \
            COMPAT_TRAP();                                                                         \
        }                                                                                          \
    } while (0)
#endif
    /* -------------------------------------------------------------------------- */

    /*NVIC 中断控制 inline*/
/* -------------------------------------------------------------------------- */
#define HAL_NVIC_ISER_BASE 0xE000E100UL
#define HAL_NVIC_ICER_BASE 0xE000E180UL
#define HAL_NVIC_IPR_BASE 0xE000E400UL

    /**
     * @brief 使能指定 NVIC 中断
     * @param[in] irq_num NVIC 中断号
     */
    COMPAT_STATIC_INLINE void hal_irq_enable(int irq_num)
    {
        uint32_t reg = (uint32_t)(irq_num >> 5) << 2;
        COMPAT_REG_WRITE32(HAL_NVIC_ISER_BASE + reg, COMPAT_BIT(irq_num & 0x1F));
    }

    /**
     * @brief 禁用指定 NVIC 中断
     * @param[in] irq_num NVIC 中断号
     */
    COMPAT_STATIC_INLINE void hal_irq_disable(int irq_num)
    {
        uint32_t reg = (uint32_t)(irq_num >> 5) << 2;
        COMPAT_REG_WRITE32(HAL_NVIC_ICER_BASE + reg, COMPAT_BIT(irq_num & 0x1F));
    }

    /**
     * @brief 设置指定 NVIC 中断优先级
     * @param[in] irq_num NVIC 中断号
     * @param[in] priority 优先级 (0=最高)
     */
    COMPAT_STATIC_INLINE void hal_irq_set_priority(int irq_num, int priority)
    {
        COMPAT_REG_WRITE8(HAL_NVIC_IPR_BASE + (uint32_t)irq_num, (uint8_t)(priority & 0xFF));
    }

    /**
     * @brief 获取指定 NVIC 中断优先级
     * @param[in] irq_num NVIC 中断号
     * @return 当前优先级
     */
    COMPAT_STATIC_INLINE int hal_irq_get_priority(int irq_num)
    {
        return COMPAT_REG_READ8(HAL_NVIC_IPR_BASE + (uint32_t)irq_num);
    }
    /* -------------------------------------------------------------------------- */

    /*全局中断屏蔽 inline*/
/* -------------------------------------------------------------------------- */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) ||           \
    defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)

    /**
     * @brief 关闭全局中断并保存掩码 (临界区入口)
     * @return 中断掩码 (供 hal_irq_restore 恢复)
     */
    COMPAT_STATIC_INLINE uint32_t hal_irq_disable_all(void)
    {
        uint32_t mask;
        __asm__ volatile("mrs %0, primask\n\t"
                         "cpsid i"
                         : "=r"(mask));
        return mask;
    }

    /**
     * @brief 恢复全局中断掩码 (临界区出口)
     * @param[in] mask hal_irq_disable_all 返回的掩码
     */
    COMPAT_STATIC_INLINE void hal_irq_restore(uint32_t mask)
    {
        __asm__ volatile("msr primask, %0" : : "r"(mask));
    }

#else
/* 非 ARM/RISC-V 平台的 fallback: 无全局中断开关, 仅透传 (API 语义同 #if 分支) */
COMPAT_STATIC_INLINE uint32_t hal_irq_disable_all(void)
{
    uint32_t irq_mask;
    __asm__ volatile("" : "=r"(irq_mask));
    return irq_mask;
}
COMPAT_STATIC_INLINE void hal_irq_restore(uint32_t mask) { COMPAT_IGNORE_RESULT(mask); }

#endif
    /* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_H */
