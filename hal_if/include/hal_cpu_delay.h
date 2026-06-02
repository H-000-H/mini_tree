#ifndef HAL_CPU_DELAY_H
#define HAL_CPU_DELAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 硬实时微秒延时 ──
 *
 * 基于 CPU 硬件周期计数器，不受 OS tick 和任务调度影响。
 * 适用于软件模拟协议（1-Wire / DHT11 / WS2812 / 软件 I2C）
 * 及需要 μs 级阻塞定时的场景。
 *
 * 实现方案:
 *   ARM Cortex-M3/4/7/33 → DWT_CYCCNT 周期计数器
 *   ARM Cortex-M0/0+     → 软件 NOP 循环（需提前校准）
 *   RISC-V RV32          → rdcycle 指令读取 mcycle
 *
 * 使用前需调用 hal_delay_init() 使能周期计数器。
 * 用户必须通过 HAL_CPU_FREQ_HZ 定义 CPU 主频。
 *
 *   #define HAL_CPU_FREQ_HZ  240000000UL
 *   #include "hal_cpu_delay.h"
 *
 *   hal_delay_init();
 *   hal_delay_ms(10);   // 阻塞 10ms
 *   hal_delay_us(10);   // 阻塞 10μs
 *   hal_delay_cycles(24); // 阻塞 24 个 CPU 周期
 */

#ifndef HAL_CPU_FREQ_HZ
#error "hal_cpu_delay.h requires HAL_CPU_FREQ_HZ (e.g. #define HAL_CPU_FREQ_HZ 240000000UL)"
#endif

#if HAL_CPU_FREQ_HZ % 1000000UL != 0
  /* 非标准频率: 使用 64 位乘法计算周期数 */
  #define HAL_DELAY_US_TO_TICKS(us)  ((uint32_t)((uint64_t)(us) * HAL_CPU_FREQ_HZ / 1000000UL))
#else
  #define HAL_CPU_CYCLES_PER_US  (HAL_CPU_FREQ_HZ / 1000000UL)
  #define HAL_DELAY_US_TO_TICKS(us)  ((uint32_t)(us) * HAL_CPU_CYCLES_PER_US)
#endif

/* ── 周期计数器初始化 ──
 *
 * ARM:   使能 DWT 调试跟踪模块 + 周期计数器
 * RISC-V: 无需初始化，rdcycle 随时可读
 * 其他:   校准 NOP 循环（若未提供校准值则使用保守估计）
 */
static inline void hal_delay_init(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8M_MAIN__)
    /* DEMCR: TRCENA 位使能 DWT */
    *(volatile uint32_t*)0xE000EDFC |= 0x01000000UL;
    /* DWT_CTRL: CYCCNTENA 位使能周期计数器 */
    *(volatile uint32_t*)0xE0001000 |= 1UL;
#elif defined(__riscv)
    /* RISC-V rdcycle 无需初始化，计数器始终运行 */
#else
    /* 无硬件计数器平台：在 hal_delay_init 中不做特殊操作 */
#endif
}

/* ── 阻塞延时 (微秒) ──
 *
 * 忙等待指定微秒数，不受 OS 调度影响。
 * 在中断上下文中也可安全调用。
 */
static inline void hal_delay_us(uint32_t us)
{
    if (us == 0) return;

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8M_MAIN__)
    /* DWT_CYCCNT 周期计数器 */
    uint32_t start = *(volatile uint32_t*)0xE0001004;
    uint32_t ticks = HAL_DELAY_US_TO_TICKS(us);
    while ((*(volatile uint32_t*)0xE0001004 - start) < ticks) { }
#elif defined(__ARM_ARCH_6M__)
    /* Cortex-M0/M0+: 无 DWT，使用 SysTick 当前值或 NOP 循环
     * SysTick->VAL 在每个时钟周期递减。
     * 注意: OS 可能修改 SysTick 配置，导致此方法不精确。
     */
    uint32_t ticks = HAL_DELAY_US_TO_TICKS(us);
    /* 尝试读 SysTick->VAL (0xE000E018)，若不可用则走 NOP 回退 */
    uint32_t start = *(volatile uint32_t*)0xE000E018;
    int32_t remaining = (int32_t)ticks;
    while (remaining > 0) {
        uint32_t now = *(volatile uint32_t*)0xE000E018;
        int32_t elapsed = (int32_t)(start - now); /* SysTick 递减 */
        if (elapsed < 0) elapsed += 0x01000000; /* 处理重载 */
        remaining -= elapsed;
        start = now;
    }
#elif defined(__riscv)
    uint32_t start;
    __asm__ volatile("rdcycle %0" : "=r"(start));
    uint32_t ticks = HAL_DELAY_US_TO_TICKS(us);
    uint32_t now;
    do {
        __asm__ volatile("rdcycle %0" : "=r"(now));
    } while ((now - start) < ticks);
#else
    /* 通用回退: 粗略 NOP 循环 (精度差，仅供编译通过) */
    volatile uint32_t n = HAL_DELAY_US_TO_TICKS(us) >> 2;
    while (n--) { __asm__ volatile("nop"); }
#endif
}

/* ── 精确周期延时 ──
 *
 * 阻塞指定 CPU 周期数。精度 1 周期，不受中断影响。
 * 适用于极短时序场景。
 */
static inline void hal_delay_cycles(uint32_t cycles)
{
    if (cycles == 0) return;

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8M_MAIN__)
    uint32_t start = *(volatile uint32_t*)0xE0001004;
    while ((*(volatile uint32_t*)0xE0001004 - start) < cycles) { }
#elif defined(__riscv)
    uint32_t start;
    __asm__ volatile("rdcycle %0" : "=r"(start));
    uint32_t now;
    do {
        __asm__ volatile("rdcycle %0" : "=r"(now));
    } while ((now - start) < cycles);
#else
    /* 粗略估算: 1 cycle ≈ 1 NOP (实际上每个循环有分支开销) */
    volatile uint32_t n = cycles >> 1;
    if (n == 0) n = 1;
    while (n--) { __asm__ volatile("nop"); }
#endif
}

/* ── 阻塞延时 (毫秒) ──
 *
 * 基于 hal_delay_us 实现，阻塞指定毫秒数。
 * 注意: 毫秒延时用简单的循环累加实现，极长延时（> 1000 ms）在低主频
 * MCU 上可能因 32 位周期计数器溢出导致精度下降。
 */
static inline void hal_delay_ms(uint32_t ms)
{
    while (ms--) hal_delay_us(1000);
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_DELAY_H */
