/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file port.h
 * @brief export symbols for port layer
 * @author H-000-H
 */
#ifndef PORT_H
#define PORT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "redef.h"
typedef void (*svc_call_back)(mini_os_uint32_t* frame, void* arg);

/* SVC helpers */
void mini_os_svc_set_callback(svc_call_back cb, void* arg);

mini_os_uint8_t mini_os_svc_get_num(mini_os_uint32_t* frame);

void mini_os_svc_dispatch(mini_os_uint32_t* frame, svc_call_back cb, void* arg);

/* Assembly entry points (naked, defined in port.c) */
void pendsv_handler(void);

void svc_handler(void);

void mini_os_start_first_thread(void);

void mini_os_yield_trigger(void);

/**
 * @brief Data + instruction synchronization barrier (dsb + isb)
 * @note call after a PendSV-related register write that must be observed
 *       before the code continues; implemented in port.S
 */
void mini_os_barrier(void);

/**
 * @brief Enter sleep until the next interrupt (wfi)
 * @note used by the idle thread loop; implemented in port.S
 */
void mini_os_wfi(void);

/**
 * @brief Spin-loop wait hint (yield instruction, NOP-equivalent on single-core
 *        Cortex-M; pays off on future multicore / other-arch ports)
 * @note used by the atomic spinlock backoff loop; implemented in port.S
 */
void mini_os_pause(void);

mini_os_err_t mini_os_nvic_set_priority(mini_os_uint32_t irq, mini_os_uint32_t priority);

void mini_os_psp_set(mini_os_uint32_t psp);

void mini_os_set_control(mini_os_uint32_t control);

/**
 * @brief Probe the running CPU against the compiled-for architecture
 * @return MINI_OS_OK on match; MINI_OS_ERR_NOTSUPP on CPU/arch mismatch
 * @note also runs as a fail-fast startup constructor (priority 101)
 */
mini_os_err_t mini_os_cpu_probe(void);

#if MINI_OS_STACK_OVERFLOW_CHECK
/**
 * @brief Check the system stack sentinel (see mini-os-heap.ld boundary);
 *        halts (fail fast) when the system stack overflowed into the heap
 * @note call from the idle thread loop; enabled via
 *       CONFIG_MINI_OS_STACK_OVERFLOW_CHECK, sentinel planted at startup
 */
void mini_os_stack_overflow_check(void);
#endif /* MINI_OS_STACK_OVERFLOW_CHECK */

#if MINI_OS_ARCH == MINI_OS_ARCH_M7
/* MINI_OS_CACHE_FLUSH / MINI_OS_CACHE_INVALIDATE / MINI_OS_CACHE_LINESIZE
 * come from mini_config.h (M7 branch) */

/**
 * @brief Enable / disable the instruction cache (invalidates before enabling)
 */
void mini_os_icache_enable(void);

void mini_os_icache_disable(void);

/**
 * @brief Enable / disable the data cache (clean + invalidate before enabling)
 */
void mini_os_dcache_enable(void);

void mini_os_dcache_disable(void);

/**
 * @brief Invalidate the instruction cache lines covering [addr, addr + size)
 * @param[in] addr any address of the target range
 * @param[in] size range length in bytes
 * @note needed after writing code to RAM at runtime (loader / OTA)
 */
void mini_os_icache_invalidate_by_addr(void* addr, mini_os_uint32_t size);

/**
 * @brief Data cache maintenance on [addr, addr + size)
 * @param[in] addr any address of the target range
 * @param[in] size range length in bytes
 * @param[in] ops bitmask of MINI_OS_CACHE_FLUSH / MINI_OS_CACHE_INVALIDATE
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when no op is selected
 * @note DMA buffers: FLUSH before handing the buffer to the DMA, INVALIDATE
 *       after the DMA wrote it; the range is rounded out to cache lines.
 *       INVALIDATE alone drops dirty lines without writing them back (Zephyr
 *       rejects whole-cache invalidate for the same reason): only use it on
 *       ranges the CPU has not written, e.g. right after a DMA writeback.
 */
mini_os_err_t mini_os_dcache_ops(void* addr, mini_os_uint32_t size, mini_os_uint32_t ops);
#endif /* MINI_OS_ARCH == MINI_OS_ARCH_M7 */

#ifdef __cplusplus
}
#endif
#endif /* PORT_H */
