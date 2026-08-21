/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file safe_state.c
 *@brief safe state 实现
 *@author H-000-H
 *@details
 *   safe_state.c — 安全状态与启动循环退避实现
 *   s_panic_counter 累计异常启动次数, ≥5 (BOOTLOOP_THRESHOLD) 触发永久锁死
 *   enter_safe_state 顺序: hal_platform_critical_hardware_lock → 挂起调度器 → 关中断 → 死循环
 *   NMI 紧急标记委托 hal_platform_nmi_emergency_stamp (平台须置于 IRAM)
 */

#include "safe_state.h"

#include "hal_amp.h"
#include "hal_platform_safety.h"
#include "osal.h"
#include <stdint.h>

#include "compiler_compat_poison.h"

/* Bootloop 退避阈值: 连续 Panic/软件复位 ≥ BOOTLOOP_THRESHOLD 次 → 永久安全锁死 */
#define BOOTLOOP_THRESHOLD 5

/*
 * Bootloop 退避计数器.
 * 连续 Panic/软件复位 ≥ BOOTLOOP_THRESHOLD 次 → 进入永久安全锁死,
 * 拒绝一切 Flash 写入和软重启, 防止 100,000 次擦写烧穿 SPI Flash.
 */
static volatile uint32_t s_panic_counter = 0;

/**
 * @brief bootloop 检查
 * @return true 可启动
 */
bool safe_state_check_bootloop(void)
{
    if (s_panic_counter >= BOOTLOOP_THRESHOLD)
    {
        enter_safe_state("BOOTLOOP DETECTED > 5 — SYSTEM FROZEN");
        return false;
    }
    s_panic_counter++;
    return true;
}

/**
 * @brief 清零计数
 */
void safe_state_clear_bootloop(void) { s_panic_counter = 0; }

/**
 * @brief 进入不可恢复的安全状态 (硬件闭锁 → 冻结调度 → 关中断 → 死循环)
 * @param reason 触发原因描述 (当前忽略, 预留日志)
 */
void enter_safe_state(const char* reason)
{
    (void)reason;

    /* 平台具体硬件闭锁 (PWM/I2S/SPI 停止, LED, 蜂鸣器) */
    COMPAT_IGNORE_RESULT(hal_platform_critical_hardware_lock());

    /* 冻结 OS (单向不可恢复) */
    osal_sched_freeze();
    osal_int_freeze();

    while (1)
        __asm__ volatile("nop");
}

/**
 * @brief NMI 紧急标记 (委托平台 IRAM 安全实现)
 */
void safe_state_nmi_emergency_stamp(void) { hal_platform_nmi_emergency_stamp(); }
