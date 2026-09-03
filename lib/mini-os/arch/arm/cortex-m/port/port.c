/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file port.c
 * @brief Cortex-M port C side: SVC callback storage and dispatch.
 *        The context-switch assembly lives in port.S (uppercase, preprocessed).
 * @author H-000-H
 */
#include "port.h"

#include "err.h"
#include "mini_config.h"
#include "redef.h"

/** @brief Installed SVC callback (shared with port.S, which loads it directly) */
svc_call_back g_svc_cb = NULL;
/** @brief Argument handed back to the SVC callback (shared with port.S) */
void* g_svc_arg = NULL;

/** @brief Cortex-M CPUID register (SCB) and its part-number field layout */
#define MINI_OS_SCB_CPUID 0xE000ED00U    /**< CPUID Base Register */
#define MINI_OS_CPUID_PARTNO_SHIFT 4     /**< part number field start bit */
#define MINI_OS_CPUID_PARTNO_MASK 0xFFFU /**< part number field mask (bits[15:4]) */
#define MINI_OS_CPUID_PARTNO_M0 0xC20U   /**< Cortex-M0 part number */
#define MINI_OS_CPUID_PARTNO_M0P 0xC60U  /**< Cortex-M0+ part number (ARMv6-M compatible) */
#define MINI_OS_CPUID_PARTNO_M3 0xC23U   /**< Cortex-M3 part number */
#define MINI_OS_CPUID_PARTNO_M4 0xC24U   /**< Cortex-M4 part number */
#define MINI_OS_CPUID_PARTNO_M7 0xC27U   /**< Cortex-M7 part number */

#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
#define MINI_OS_CPACR 0xE000ED88U           /**< Coprocessor Access Control Register */
#define MINI_OS_CPACR_FPU_MASK (0xFU << 20) /**< CP10/CP11 full access bits */
/**
 * @brief Enable the FPU (CP10/CP11 full access) before any thread can run
 * @note constructor, runs before main; required for the FPU context switch
 */
MINI_OS_CONSTRUCTOR(MINI_OS_FPU_ENABLE_CONSTRUCTOR)
static void mini_os_fpu_enable_ctor(void)
{
    volatile mini_os_uint32_t* cpacr = (volatile mini_os_uint32_t*)MINI_OS_CPACR;

    *cpacr |= MINI_OS_CPACR_FPU_MASK;
    __asm__ volatile("dsb\n"
                     "isb" ::
                         : "memory");
}
#endif /* MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU */

/**
 * @brief Probe the running CPU against the architecture the kernel was built for
 * @return MINI_OS_OK if SCB_CPUID part number matches MINI_OS_ARCH;
 *         MINI_OS_ERR_NOTSUPP otherwise (wrong -DMINI_OS_ARCH or stale lib)
 * @note Cortex-M CPUID (0xE000ED00) bits[15:4] hold the part number:
 *       M0 0xC20 / M0+ 0xC60 / M3 0xC23 / M4 0xC24 / M7 0xC27. The port
 *       assembly is core-specific, so a mismatch would corrupt contexts;
 *       the startup constructor halts on it before any thread runs.
 */
mini_os_err_t mini_os_cpu_probe(void)
{
    volatile mini_os_uint32_t* cpuid = (volatile mini_os_uint32_t*)MINI_OS_SCB_CPUID;
    mini_os_uint32_t           partno = (*cpuid >> MINI_OS_CPUID_PARTNO_SHIFT) & MINI_OS_CPUID_PARTNO_MASK;

#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    if (partno != MINI_OS_CPUID_PARTNO_M0 && partno != MINI_OS_CPUID_PARTNO_M0P) /* ARMv6-M twins */
#elif MINI_OS_ARCH == MINI_OS_ARCH_M3
    if (partno != MINI_OS_CPUID_PARTNO_M3)
#elif MINI_OS_ARCH == MINI_OS_ARCH_M4
    if (partno != MINI_OS_CPUID_PARTNO_M4)
#elif MINI_OS_ARCH == MINI_OS_ARCH_M7
    if (partno != MINI_OS_CPUID_PARTNO_M7)
#else
#error "mini-os: unsupported MINI_OS_ARCH value in mini_os_cpu_probe"
#endif
        return MINI_OS_ERR_NOTSUPP;
    return MINI_OS_OK;
}

/**
 * @brief Halt on a CPU/architecture mismatch (constructor, runs before main)
 * @note fail fast: with the wrong core the context-switch assembly corrupts
 *       the stack, far better to stop here than to debug a corrupted PendSV
 */
MINI_OS_CONSTRUCTOR(MINI_OS_CPU_PROBE_CONSTRUCTOR)
static void mini_os_cpu_probe_ctor(void)
{
    if (mini_os_cpu_probe() != MINI_OS_OK)
    {
        for (;;)
        {
        }
    }
}

#if MINI_OS_STACK_OVERFLOW_CHECK
/* Linker-provided system stack boundary (mini-os-heap.ld):
 * heap is [__mini_os_heap_start, __mini_os_heap_end), the system (MSP) stack
 * is [__mini_os_heap_end, _estack) and grows downwards; the word AT
 * __mini_os_heap_end is the first casualty of a stack overflow. */
extern mini_os_uint32_t __mini_os_heap_end;

/**
 * @brief Plant the overflow sentinel at the system stack's low boundary
 * @note constructor, runs after the CPU probe and before any thread exists
 */
MINI_OS_CONSTRUCTOR(MINI_OS_STACK_SENTINEL_CONSTRUCTOR)
static void mini_os_stack_sentinel_ctor(void) { __mini_os_heap_end = MINI_OS_STACK_MAGIC; }

/**
 * @brief Check the stack sentinel; halt on overflow (fail fast)
 * @note called from the idle thread loop; the system stack ran into the heap
 *       when the magic word no longer reads MINI_OS_STACK_MAGIC
 */
void mini_os_stack_overflow_check(void)
{
    if (__mini_os_heap_end != MINI_OS_STACK_MAGIC)
    {
        for (;;)
        {
        }
    }
}
#endif /* MINI_OS_STACK_OVERFLOW_CHECK */

/**
 * @brief Install the SVC callback the port assembly dispatches to
 * @param[in] cb callback invoked with the stacked exception frame
 *              (MINI_OS_NULL disables the dispatch)
 * @param[in] arg opaque argument handed back to the callback
 * @note the two globals are read directly by port.S, hence they are not static
 */
void mini_os_svc_set_callback(svc_call_back cb, void* arg)
{
    g_svc_cb = cb;
    g_svc_arg = arg;
}

/**
 * @brief Extract the 8-bit immediate of the SVC instruction that was executed
 * @param[in] frame stacked exception frame (r0-r3, r12, LR, PC, xPSR)
 * @return the SVC number (imm8)
 * @details the stacked PC points at the instruction after the SVC, and a Thumb
 *          SVC is 2 bytes wide: [pc-2] is the 0xDF opcode, [pc-1] is the imm8
 */
mini_os_uint8_t mini_os_svc_get_num(mini_os_uint32_t* frame)
{
    mini_os_uint32_t pc = frame[6];                       /* stacked PC: address after the SVC instruction */
    mini_os_uint8_t  num = *((mini_os_uint8_t*)(pc - 1)); /* Thumb SVC is 2 bytes: [pc-2]=0xDF opcode, [pc-1]=imm8 */
    return num;
}

/**
 * @brief Dispatch an SVC to a callback with its frame and argument
 * @param[in] frame stacked exception frame of the SVC
 * @param[in] cb callback to invoke (MINI_OS_NULL is ignored)
 * @param[in] arg opaque argument handed to the callback
 */
void mini_os_svc_dispatch(mini_os_uint32_t* frame, svc_call_back cb, void* arg)
{
    if (cb != NULL)
        cb(frame, arg);
}
