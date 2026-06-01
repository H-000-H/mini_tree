#ifndef HAL_CPU_H
#define HAL_CPU_H

#ifdef __cplusplus
extern "C" {
#endif

/* CPU 紧急停止 (IEC 61508 §7.4.3.4 / ISO 26262 第6部分)
 *
 * 单核模式: 仅关当前核心中断
 * 双核模式: 关中断 + 跨核暂停 (挂起对端核心)
 */
void hal_cpu_emergency_stop_all_cores(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_H */
