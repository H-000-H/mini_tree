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

/* ── Atomic / FFS ── */
#define RT_USING_HW_ATOMIC
#define RT_USING_CPU_FFS

/* ── Debug ── */
#define RT_VER_NUM               0x50300
#define RT_BACKTRACE_LEVEL_MAX_NR   32

#endif /* RTCONFIG_H */
