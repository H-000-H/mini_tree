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

/* ── AMP (Asymmetric Multi-Processing) ──
 *
 * 在 CONFIG_CPU_CORES > 1 的双核芯片上有效。
 * Core 0 运行 RTOS，Core 1 运行裸机代码。
 */

/** 启动副核心 (板级必须实现)
 *
 *  根据不同 MCU 释放副核复位:
 *    STM32H7:  置位 RCC->GCR 的 BOOT_C2 位
 *    GD32:     写 SYS_CFG 寄存器释放 CPU1
 *    RISC-V:   发 IPI 或操作复位控制寄存器
 */
void hal_cpu_secondary_startup(void);

/** 副核心裸机入口 (weak, 板级可覆盖)
 *
 *  默认实现为死循环。用户覆盖此函数来实现
 *  Core 1 的 bare-metal 主循环:
 *
 *    void hal_cpu_baremetal_entry(void)
 *    {
 *        // 初始化 Core 1 外设
 *        while (1)
 *        {
 *            // Core 1 轮询/中断处理
 *        }
 *    }
 */
void hal_cpu_baremetal_entry(void);

/** 获取当前 CPU 核心 ID
 *
 *  @return 0 = Core 0 (RTOS), 1 = Core 1 (bare-metal)
 *
 *  ARM Cortex-M 双核通常通过读取
 *  MPIDR 寄存器 (0xE000EF5C) 或
 *  厂商自定义寄存器实现。
 */
int hal_cpu_get_id(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_H */
