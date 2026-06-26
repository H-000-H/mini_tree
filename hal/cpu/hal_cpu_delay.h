#ifndef HAL_CPU_DELAY_H
#define HAL_CPU_DELAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*硬实时延时 API*/
/*===========================================================================================================================================================*/
/* ── 硬实时微秒延时 ──
 *
 * 基于 CPU 硬件周期计数器，不受 OS tick 和任务调度影响。
 * 适用于软件模拟协议（1-Wire / DHT11 / WS2812 / 软件 I2C）
 * 及需要 μs 级阻塞定时的场景。
 *
 * 平台实现:
 *   STM32 → CMSIS DWT 周期计数器 (CoreDebug / DWT)
 *   CH32  → WCH Delay_Init / Delay_Us / Delay_Ms (SysTick)
 *
 *   hal_delay_init();
 *   hal_delay_ms(10);       // 阻塞 10ms
 *   hal_delay_us(10);       // 阻塞 10μs
 *   hal_delay_cycles(24);   // 阻塞 24 个 CPU 周期
 */

void hal_delay_init(void);
void hal_delay_us(uint32_t us);
void hal_delay_ms(uint32_t ms);
void hal_delay_cycles(uint32_t cycles);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_DELAY_H */
