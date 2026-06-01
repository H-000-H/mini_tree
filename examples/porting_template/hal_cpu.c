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
