/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_systick.h
 *@brief SysTick 系统滴答硬件直投层(HAL)接口定义
 *@author H-000-H
 *@details
 *   @note        与 hal_tim 不同: SysTick 为 ARM Cortex-M 内核私有标准件,
 *   寄存器布局与基址由 ARMv7-M/ARMv8-M 架构固定, 故本层提供默认真实现
 *   (见 hal_systick.c), 无需板级强符号覆盖; 仅当某芯片 SysTick 行为
 *   异常时才需板级以强符号覆盖。
 *   @note        频率参数 (CPU 主频 / tick 频率) 由 DTS 管理, 经 DTC_GEN_* 宏注入,
 *   本层不写死频率, 仅写死基地址 (亦可被 HAL_SYSTICK_BASE 覆盖)。
 */

#ifndef HAL_SYSTICK_H
#define HAL_SYSTICK_H

#include "compiler_compat.h"
#include "status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /*===========================================================================================================================================================*/
    /* 宏定义 */
    /*===========================================================================================================================================================*/
    /**
     * @brief SysTick 寄存器基址 (Cortex-M SCS 私有外设总线, 架构固定)
     * @note  默认 0xE000E010; 板级/工程可通过编译宏覆盖 (视为写死, 亦可经 DTS 注入)
     */
#ifndef HAL_SYSTICK_BASE
#define HAL_SYSTICK_BASE 0xE000E010u
#endif

    /*===========================================================================================================================================================*/
    /* 硬件直投层核心 API */
    /*===========================================================================================================================================================*/
    /**
     * @brief 初始化并启动 SysTick
     * @param tick_hz tick 中断频率 (Hz), 来自 DTS chosen tick-rate (DTC_GEN_TICK_RATE_HZ)
     * @return VFS_OK 成功; VFS_ERR_NOTSUPP 平台无 SysTick (非 Cortex-M); 负的 VFS_ERR_* 配置失败
     * @note  CPU 主频从 DTS /cpus/cpu@0 clock-frequency (DTC_GEN_CPU_CLOCK_HZ) 读取,
     *        由此计算 LOAD 装载值; 非 Cortex-M 平台返回 VFS_ERR_NOTSUPP, 调度器回退 DTS chosen
     * TIM。
     */
    int COMPAT_WARN_UNUSED_RESULT hal_systick_init(uint32_t tick_hz);

    /**
     * @brief 停止并关闭 SysTick
     * @return VFS_OK 成功
     */
    int COMPAT_WARN_UNUSED_RESULT hal_systick_deinit(void);

    /**
     * @brief SysTick 中断业务钩子 — 由 SysTick_Handler 调用
     * @note  默认空实现 (weak); 使用方 (如调度器) 以强符号覆盖, 在其中累加系统滴答。
     *        之所以留空: SysTick 中断如何驱动业务 (累加 tick) 由上层决定, HAL 不耦合具体调度器。
     */
    void hal_systick_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYSTICK_H */
