#ifndef HAL_CPU_H
#define HAL_CPU_H

/* ── CPU 紧急停止 (IEC 61508 §7.4.3.4 / ISO 26262 第6部分) ──
 *
 * 单核模式: 仅 portDISABLE_INTERRUPTS() 关当前核心中断
 * 双核模式: portDISABLE_INTERRUPTS() + 跨核暂停 (挂起对端核心)
 *
 * 平台实现位于 soc_port_xxx/src/hal_cpu.c
 * 由 sdkconfig / CONFIG_FREERTOS_NUMBER_OF_CORES 宏自动选择路径
 */
#ifdef __cplusplus
extern "C" {
#endif

void hal_cpu_emergency_stop_all_cores(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_H */