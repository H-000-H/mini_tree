#ifndef HAL_PWM_FAST_H
#define HAL_PWM_FAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── PWM Fast Path 运行时占空比直写 ──
 *
 * 本文件仅声明 API 接口，不提供通用实现。
 * 原因：PWM 定时器寄存器布局因芯片厂商而异（STM32 TIMx、NXP TPM、
 * Microchip CCP 等均不兼容），无法像 GPIO BSRR 那样统一偏移。
 *
 * 适用场景：
 *   电机 FOC 20kHz+ 控制环、高频 LED 调光等需要在每个控制周期
 *   更新占空比的硬实时场合。此时应绕过 VFS ioctl + 函数指针，
 *   直接写定时器 CCR 寄存器。
 *
 * 实现责任：
 *   由 hal_if/soc/<chip_name>/ 层根据具体芯片手册提供。
 *   下方声明的函数签名作为约定，各 SoC 实现应提供同名函数
 *   （static inline 或 real function 均可）。
 *
 * 使用方式（以 STM32 为例）:
 *   #include "hal_pwm_fast.h"
 *   // STM32: TIM2 的 CCR1 偏移 0x34, CCR2 偏移 0x38, ...
 *   #define TIM2_BASE  0x40000000UL
 *   hal_pwm_fast_set_duty(TIM2_BASE, 1, 500);
 *
 * 安全注意:
 *   调用前需确认定时器已在初始化阶段正确配置（预分频、ARR、
 *   CCER 输出使能、ARPE 影子寄存器模式）。
 *   本函数不执行任何状态检查，无效地址导致硬 fault。
 */

/* 更新占空比
 *
 * tim_base: 定时器外设基址（如 STM32 TIM2 = 0x40000000）
 * channel:  通道号（通常 1-4）
 * duty:     占空比计数值（具体范围由 ARR 决定）
 */
static inline void hal_pwm_fast_set_duty(uint32_t tim_base, int channel, uint32_t duty)
{
    (void)tim_base;
    (void)channel;
    (void)duty;
    /* 平台实现应在此处写定时器 CCR 寄存器 */
}

/* 更新周期（频率）
 *
 * 少数场景需要在运行时改变 PWM 频率（如变频电机启动）。
 * 大多数场景频率固定，仅需 set_duty。
 */
static inline void hal_pwm_fast_set_period(uint32_t tim_base, uint32_t period)
{
    (void)tim_base;
    (void)period;
    /* 平台实现应在此处写 ARR/PRA 寄存器 */
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWM_FAST_H */
