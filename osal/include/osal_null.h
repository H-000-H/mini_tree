#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* ── 裸机移植辅助接口 ──
 *
 * 使用 osal_null 后端时, 在中断入口/出口调用以下函数:
 *
 *   void SysTick_Handler(void)
 *   {
 *       osal_null_isr_enter();
 *       osal_null_systick_handler();
 *       // ... 其他中断处理 ...
 *       osal_null_isr_exit();
 *   }
 *
 *   void UART_IRQHandler(void)
 *   {
 *       osal_null_isr_enter();
 *       // ... 处理 ...
 *       osal_null_isr_exit();
 *   }
 *
 * 原子后端在 menuconfig → OSAL Backend → Bare-metal Atomic Backend 配置.
 */

/* 在 ISR 入口调用 (设置 osal_in_isr() 为 true) */
void osal_null_isr_enter(void);

/* 在 ISR 出口调用 */
void osal_null_isr_exit(void);

/* 系统滴答 (1 ms), 应在 SysTick_Handler 中调用 */
void osal_null_systick_handler(void);

#ifdef __cplusplus
}
#endif

