/* SPDX-License-Identifier: Apache-2.0 */
/*
 * safe_state.c — 安全状态与启动循环退避实现
 *
 * s_panic_counter 累计异常启动次数, ≥5 (BOOTLOOP_THRESHOLD) 触发永久锁死
 * enter_safe_state 顺序: hal_platform_critical_hardware_lock → 挂起调度器 → 关中断 → 死循环
 * NMI 紧急标记委托 hal_platform_nmi_emergency_stamp (平台须置于 IRAM)
 */
#include "safe_state.h"
#include "hal_platform_safety.h"
#include "hal_cpu.h"
#include "osal.h"

#include <stdint.h>
#include "compiler_compat_poison.h"

/* Bootloop 退避阈值: 连续 Panic/软件复位 ≥ BOOTLOOP_THRESHOLD 次 → 永久安全锁死 */
#define BOOTLOOP_THRESHOLD  5

/*
 * Bootloop 退避计数器.
 * 连续 Panic/软件复位 ≥ BOOTLOOP_THRESHOLD 次 → 进入永久安全锁死,
 * 拒绝一切 Flash 写入和软重启, 防止 100,000 次擦写烧穿 SPI Flash.
 */
static volatile uint32_t s_panic_counter = 0;

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

void safe_state_clear_bootloop(void)
{
    s_panic_counter = 0;
}

/*
 * enter_safe_state — 不可恢复的安全状态
 *
 * 顺序:
 *   1. 平台硬件闭锁: 停外设, 亮故障灯, 蜂鸣器
 *   2. 挂起调度器 + 关中断
 *   3. 死循环 (外设由硬件自主维持)
 */
void enter_safe_state(const char* reason)
{
    (void)reason;

    /* 平台具体硬件闭锁 (PWM/I2S/SPI 停止, LED, 蜂鸣器) */
    hal_platform_critical_hardware_lock();

    /* 冻结 OS */
    osal_sched_suspend();
    osal_int_disable();

    while (1)
    {
        __asm__ volatile("nop");
    }
}

/*
 * NMI 紧急标记: 委托平台实现 (IRAM 安全)
 */
void safe_state_nmi_emergency_stamp(void)
{
    hal_platform_nmi_emergency_stamp();
}
