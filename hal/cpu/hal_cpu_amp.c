/*
 * hal_cpu_amp.c — AMP (Asymmetric Multi-Processing) 弱符号默认实现
 *
 * 当 CONFIG_CPU_CORES > 1 时编译。
 * 三个函数均为弱符号，板级可覆盖:
 *   hal_cpu_baremetal_entry    — 副核入口 (默认死循环)
 *   hal_cpu_secondary_startup  — 启动副核 (默认空操作)
 *   hal_cpu_get_id             — 获取当前核 ID (默认返回 0)
 *
 * 板级应根据具体 MCU 覆盖 hal_cpu_secondary_startup()
 * 和 hal_cpu_get_id()，并可选覆盖 hal_cpu_baremetal_entry()。
 */

#include "hal_cpu.h"
#include "compiler_compat_poison.h"

/*
 * 副核心裸机入口 — 默认死循环
 * 用户应在板级覆盖此函数，实现 Core 1 的主循环逻辑。
 */
__attribute__((weak))
void hal_cpu_baremetal_entry(void)
{
    while (1)
    {
        /* 用户覆盖此函数实现 Core 1 裸机任务 */
    }
}

/*
 * 启动副核心 — 默认空操作
 * 板级必须覆盖此函数，根据具体 MCU 释放副核复位:
 *   - STM32H7xx:  RCC->GCR 的 BOOT_C2 位
 *   - GD32:       SYS_CFG 寄存器
 *   - RISC-V:     复位控制寄存器 / IPI
 */
__attribute__((weak))
void hal_cpu_secondary_startup(void)
{
    /* 板级必须实现具体 MCU 的副核启动序列 */
}

/*
 * 获取当前 CPU 核心 ID — 默认返回 0
 * 板级在双核芯片上应覆盖此函数:
 *   ARM Cortex-M: 读 MPIDR 寄存器 (0xE000EF5C)
 *   RISC-V:       读 mhartid CSR
 *   其他:         厂商自定义寄存器
 */
__attribute__((weak))
int hal_cpu_get_id(void)
{
    return 0;  /* 默认为 Core 0 */
}
