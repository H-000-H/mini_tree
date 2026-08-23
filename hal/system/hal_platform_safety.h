/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_platform_safety.h
 *@brief hal platform safety 头文件
 *@author H-000-H
 *@details
 *   平台安全 HAL — 硬件闭锁与 NMI 紧急标记
 *   critical_hardware_lock: 冻结调度器前强停外设并点亮故障 LED/蜂鸣器
 *   nmi_emergency_stamp: IRAM 内执行, 写持久崩溃标记, 禁用 printf/mutex/Flash
 */

#ifndef HAL_PLATFORM_SAFETY_H
#define HAL_PLATFORM_SAFETY_H

#ifdef __cplusplus
extern "C"
{
#endif

    /*平台级安全硬件闭锁*/
    /*===========================================================================================================================================================*/
    /* 平台级安全硬件闭锁
     *
     * 由 enter_safe_state() 在冻结调度器之前调用.
     * 平台实现需完成:
     *   - 强制停止所有活跃外设 (PWM, I2S, SPI, DMA 等)
     *   - 点亮故障指示灯 (Fault LED)
     *   - 启动蜂鸣器报警 (2Hz 方波)
     *
     * @return MINI_OK 成功; 负的 VFS_ERR_* (如 MINI_ERR_IO) 表示某步失败.
     * 调用方处于安全停机路径, 通常忽略返回值 (COMPAT_IGNORE_RESULT).
     */
    int hal_platform_critical_hardware_lock(void);

    /* 强制停止所有 PWM 输出 (安全停机路径; STM32F407 板无独立 PWM 引擎时为 no-op)
     * @return MINI_OK 成功; 负的 VFS_ERR_* (如 MINI_ERR_IO) 表示停止失败. */
    int hal_pwm_force_stop_all(void);
    /*===========================================================================================================================================================*/

    /*NMI 紧急标记*/
    /*===========================================================================================================================================================*/
    /* 平台级 NMI 紧急标记 (掉电保护)
     *
     * 由 BOD NMI handler 调用, 必须在 IRAM 中执行.
     * 平台实现需完成:
     *   - 写入持久崩溃标记 (如 RTC 寄存器)
     *   - 强制点亮故障 LED (GPIO 寄存器直写)
     *
     * 严禁: printf / mutex / FreeRTOS API / Flash 访问
     */
    void hal_platform_nmi_emergency_stamp(void);
    /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_PLATFORM_SAFETY_H */
