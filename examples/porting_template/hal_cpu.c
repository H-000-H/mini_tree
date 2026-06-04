/*
 * hal_cpu.c — CPU/内核移植模板
 *
 * 实现CPU特定操作：周期计数器、缓存控制等。
 */

#include "hal_cpu.h"
#include <stdint.h>

uint32_t hal_cpu_get_cycle_count(void)
{
    /*
     * TODO: 在 ARM CMx 上返回 DWT->CYCCNT，或在 RISC-V 上返回 rdcycle，或在 POSIX 上返回 clock()。
     *
     * ARM Cortex-M (DWT):
     *   if (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)
     *       return DWT->CYCCNT;
     *   return 0;
     *
     * RISC-V:
     *   uint32_t cycles;
     *   __asm__ volatile("rdcycle %0" : "=r"(cycles));
     *   return cycles;
     */
    return 0;
}

void hal_cpu_busy_wait_us(uint32_t us)
{
    /*
     * TODO: 实现微秒级精度的忙等待循环。
     * 在 ARM 上使用 DWT->CYCCNT，或直接使用定时器寄存器。
     */
    (void)us;
}

/* ════════════════════════════════════════════════════════════════════
 *  AMP (Asymmetric Multi-Processing) — 双核芯片专用
 *
 *  当 Kconfig 中 CONFIG_CPU_CORES=2 时，Core 1 需由板级启动。
 *  框架在 System_Start_Tasks() (Phase 2) 末尾调用
 *  hal_cpu_secondary_startup() 来释放副核复位。
 *
 *  以下是三组必须/可选实现的函数:
 *    1. hal_cpu_secondary_startup  (必须)
 *    2. hal_cpu_baremetal_entry    (可选, 默认 deadloop)
 *    3. hal_cpu_get_id             (可选, 默认 0)
 *
 *  不同 MCU 的典型实现:
 *
 *   STM32H747 (Cortex-M7 + M4):
 *     void hal_cpu_secondary_startup(void) {
 *         RCC->GCR |= RCC_GCR_BOOT_C2;      // 释放 CM4 核复位
 *     }
 *     int hal_cpu_get_id(void) {
 *         return (SCB->CPUID & 0xF0000000) ? 1 : 0;  // 或读 HSEM
 *     }
 *
 *   GD32 (双核 Cortex-M):
 *     void hal_cpu_secondary_startup(void) {
 *         SYS_CFG->CTL |= SYS_CFG_CTL_CPU1_BOOT;
 *     }
 *
 *   RISC-V 双核:
 *     void hal_cpu_secondary_startup(void) {
 *         // 发 IPI 或写复位控制寄存器
 *     }
 * ════════════════════════════════════════════════════════════════════ */

#if CONFIG_CPU_CORES > 1

/*
 * hal_cpu_secondary_startup — 启动副核心 (必须实现)
 *
 * 释放副核 (Core 1) 的硬件复位，让其从
 * hal_cpu_baremetal_entry 开始执行。
 *
 * 不同 MCU 的做法不同，常见方式:
 *   - ARM: 写 RCC/HSEM 类寄存器释放复位
 *   - RISC-V: 操作复位控制器或发核间中断
 */
void hal_cpu_secondary_startup(void)
{
    /* TODO: 根据具体 MCU 实现 */
}

/*
 * hal_cpu_baremetal_entry — 副核心裸机入口 (可选覆盖)
 *
 * Core 1 被启动后执行的函数。默认死循环，
 * 用户可在自己的板级文件中覆盖此函数。
 *
 * 典型使用模式:
 *
 *   void hal_cpu_baremetal_entry(void)
 *   {
 *       // Core 1 初始化 (时钟、GPIO、DMA 等)
 *       while (1)
 *       {
 *           // Core 1 裸机主循环
 *       }
 *   }
 */
void hal_cpu_baremetal_entry(void)
{
    while (1)
    {
        /* 用户覆盖此函数实现 Core 1 裸机逻辑 */
    }
}

/*
 * hal_cpu_get_id — 获取当前 CPU 核心 ID (可选覆盖)
 *
 * 返回 0 (Core 0) 或 1 (Core 1)。
 *
 * ARM 双核典型做法:
 *   方式一: 读 MPIDR 寄存器
 *     uint32_t mpidr;
 *     __asm__ volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
 *     return mpidr & 1;
 *
 *   方式二: 读厂商自定义寄存器
 *     return (HSEM->CID & HSEM_CID_MASK);
 *
 * RISC-V:
 *   uint32_t hartid;
 *   __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));
 *   return hartid;
 */
int hal_cpu_get_id(void)
{
    return 0;  /* 默认为 Core 0 */
}

#endif /* CONFIG_CPU_CORES > 1 */
