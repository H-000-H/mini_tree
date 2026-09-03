/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_systick.c
 *@brief hal systick 实现
 *@author H-000-H
 *@details
 *   hal_systick — SysTick 系统滴答默认实现
 *   与 hal_tim 的 "weak stub + 板级强实现" 不同: SysTick 是 ARM Cortex-M 内核
 *   私有标准件, 寄存器布局 (CTRL/LOAD/VAL) 与基址 (0xE000E010) 由架构固定,
 *   故本文件提供默认真实现, 开箱即用; 仅当某芯片 SysTick 行为异常时,
 *   板级才以强符号覆盖 init/deinit。
 *   频率全部来自 DTS 生成的宏 (dt_config_gen.h):
 *   - tick 频率:   DTC_GEN_TICK_RATE_HZ (chosen tick-rate)
 *   - CPU 主频:    DTC_GEN_CPU_CLOCK_HZ (/cpus/cpu@0 clock-frequency)
 *   本层不写死频率; 仅写死基址 HAL_SYSTICK_BASE (默认 0xE000E010, 可覆盖)。
 *   中断向量 SysTick_Handler 在本文件 weak 定义, 内部调用 hal_systick_irq_handler;
 *   hal_systick_irq_handler 亦为 weak 空钩子, 由使用方 (如调度器) 强符号覆盖以累加滴答。
 *   非 Cortex-M 平台 (RISC-V 等) 无 SysTick, hal_systick_init 返回 MINI_ERR_NOTSUPP,
 *   调度器据此回退 DTS chosen TIM。
 */

#include "hal_systick.h"

#include "compiler_compat.h"
#include "dt_config_gen.h"
#include "status.h"
#include <stdint.h>

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — ESP 的 tick 由 IDF 管理, hal_systick 不接硬件。
 * (文件内原有 __CORTEX_M 分支为 ARM 默认真实现, 非 ESP 构建保留。) */
#else
/* SysTick 寄存器偏移 (相对 HAL_SYSTICK_BASE) */
#define HAL_SYSTICK_REG_CTRL 0x00u           /* 控制状态寄存器 */
#define HAL_SYSTICK_REG_LOAD 0x04u           /* 重装载值寄存器 (24 位) */
#define HAL_SYSTICK_REG_VAL 0x08u            /* 当前计数值寄存器 */

/* CTRL 位定义 (ARMv7-M/ARMv8-M) */
#define HAL_SYSTICK_CTRL_ENABLE (1u << 0)    /* bit0: SysTick 使能 */
#define HAL_SYSTICK_CTRL_TICKINT (1u << 1)   /* bit1: 计数到 0 触发 SysTick 异常 */
#define HAL_SYSTICK_CTRL_CLKSOURCE (1u << 2) /* bit2: 1=处理器时钟(HCLK), 0=外部时钟 */
#define HAL_SYSTICK_LOAD_MASK 0xFFFFFFu      /* LOAD 24 位最大值 */

/* 同时覆盖 CMSIS 宏 (__CORTEX_M*) 与编译器宏 (__ARM_ARCH_*): 各工具链均能正确判定 */
#if defined(__CORTEX_M0) || defined(__CORTEX_M0PLUS) || defined(__CORTEX_M3) || defined(__CORTEX_M4) || defined(__CORTEX_M4F) ||                     \
    defined(__CORTEX_M7) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) ||    \
    defined(__ARM_ARCH_8M_MAIN__)

int hal_systick_init(uint32_t tick_hz)
{
    if (tick_hz == 0u)
        return MINI_ERR_INVAL;

    uint32_t cpu_hz = (uint32_t)DTC_GEN_CPU_CLOCK_HZ;
    if (cpu_hz == 0u)
        return MINI_ERR_INVAL;

    /* reload = cpu_hz / tick_hz - 1, 24 位上限检查 */
    uint32_t reload = cpu_hz / tick_hz;
    if (reload == 0u || reload > (HAL_SYSTICK_LOAD_MASK + 1u))
        return MINI_ERR_INVAL;
    reload -= 1u;

    /* 先关断再配置, 避免中间态触发 */
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_CTRL, 0u);
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_LOAD, reload);
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_VAL, 0u);
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_CTRL, HAL_SYSTICK_CTRL_CLKSOURCE | HAL_SYSTICK_CTRL_TICKINT | HAL_SYSTICK_CTRL_ENABLE);
    return MINI_OK;
}

int hal_systick_deinit(void)
{
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_CTRL, 0u);
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_LOAD, 0u);
    MINI_REG_WRITE32(HAL_SYSTICK_BASE + HAL_SYSTICK_REG_VAL, 0u);
    return MINI_OK;
}

/**
 * @brief SysTick 异常入口 (weak, 允许板级/启动代码强覆盖)
 */
MINI_WEAK void SysTick_Handler(void) { hal_systick_irq_handler(); }

#else /* 非 Cortex-M: 无 SysTick, 返回 NOTSUPP 让调度器回退 DTS chosen TIM */

int hal_systick_init(uint32_t tick_hz)
{
    (void)tick_hz;
    return MINI_ERR_NOTSUPP;
}

int hal_systick_deinit(void) { return MINI_OK; }

#endif /* __CORTEX_M* / __ARM_ARCH_*M__ */

/**
 * @brief SysTick 中断业务钩子默认空实现 (weak)
 * @note  由调度器等使用方强符号覆盖; 本弱符号保证链接期总有定义。
 */
MINI_WEAK void hal_systick_irq_handler(void) {}
#endif /* ESP_PLATFORM */
