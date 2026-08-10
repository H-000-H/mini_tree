#ifndef RTCONFIG_H
#define RTCONFIG_H

/* ═══════════════════════════════════════════════════════════════════
 * mini_tree RT-Thread Kernel Configuration
 *
 * Minimal configuration for OSAL backend use only.
 * The host project can override this file by adding its own
 * rtconfig.h to the include path (same pattern as FreeRTOSConfig.h).
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Nano 模式 (跳过 POSIX 系统头, 纯内核) ── */
#define RT_USING_NANO

/* ── Object / Thread ── */
#define RT_NAME_MAX              12
#define RT_ALIGN_SIZE            8
#define RT_THREAD_PRIORITY_32
#define RT_THREAD_PRIORITY_MAX   32
#define RT_TICK_PER_SECOND       1000
#define RT_CPUS_NR               1

/* ── Hooks ── */
#define RT_USING_HOOK
#define RT_USING_IDLE_HOOK
#define RT_IDLE_HOOK_LIST_SIZE   4
#define IDLE_THREAD_STACK_SIZE   256

/* ── IPC (用于 OSAL mutex) ── */
#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_MESSAGEQUEUE

/* ── Memory ── */
#define RT_USING_HEAP
#define RT_USING_SMALL_MEM
#define RT_USING_SMALL_MEM_AS_HEAP

/* ── Console ── */
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE       128

/* ── Atomic / FFS ──
 * M0/M0+ (ARMv6-M) 无 LDREX/STREX 指令, 原子操作退回软件实现 (关中断),
 * 由 rtatomic.h 的 rt_soft_atomic_* 内联提供, 无需 libcpu atomic_arm.c.
 * RT_USING_CPU_FFS 表示使用 CPU 硬件 FFS (ARM 有 CLZ 指令, 见 cpuport.c);
 * RISC-V 无硬件 FFS 指令, 不定义以启用 kservice.c 的软件 __rt_ffs. */
#if !defined(__ARM_ARCH_6M__)
#define RT_USING_HW_ATOMIC
#endif
#if !defined(__riscv)
#define RT_USING_CPU_FFS
#endif

/* ── Debug ── */
#define RT_VER_NUM               0x50300
#define RT_BACKTRACE_LEVEL_MAX_NR   32

#endif /* RTCONFIG_H */
