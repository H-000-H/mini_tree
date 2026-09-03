/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file mem_heap.h
 * @brief Memory heap definition and link lds script only control heap size
 * @author H-000-H
 */
#ifndef MEM_HEAP_H
#define MEM_HEAP_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include "mini_config.h"
#include "redef.h"
extern char __mini_os_heap_start[];
extern char __mini_os_heap_end[];
#define MINI_OS_HEAP_SIZE ((mini_os_size_t)(__mini_os_heap_end - __mini_os_heap_start))
/**
 * @brief Slab page size
 * @note
 *  - Slab page size must be a power of 2 and not exceed 64KB (default 2KB)
 *  - Slab zone is carved out of the pool once at init: pages = min(MINI_OS_SLAB_PAGE_MAX,
 * heap/MINI_OS_SLAB_PROPORTION / page size)
 *  - normal it's take up 1/4 or 1/5 of the heap and peer slab buffer in page mini default 16
 */
#ifdef CONFIG_MINI_OS_SLAB_PAGE_SIZE
#define MINI_OS_SLAB_PAGE_SIZE CONFIG_MINI_OS_SLAB_PAGE_SIZE
#else
#define MINI_OS_SLAB_PAGE_SIZE 2048
#endif
#if defined(CONFIG_MINI_OS_SLAB_PAGE_MAX_SIZE)
#define MINI_OS_SLAB_PAGE_MAX_SIZE CONFIG_MINI_OS_SLAB_PAGE_MAX_SIZE
#else
#define MINI_OS_SLAB_PAGE_MAX_SIZE (1 << 16)
#endif

#ifdef CONFIG_MINI_OS_SLAB_PAGE_MAX
#define MINI_OS_SLAB_PAGE_MAX CONFIG_MINI_OS_SLAB_PAGE_MAX
#else
#define MINI_OS_SLAB_PAGE_MAX 4
#endif

#ifdef CONFIG_MINI_OS_SLAB_PROPORTION
#define MINI_OS_SLAB_PROPORTION CONFIG_MINI_OS_SLAB_PROPORTION
#else
#define MINI_OS_SLAB_PROPORTION 4
#endif

#ifdef CONFIG_MINI_OS_SLAB_MINI_BYTES
#define MINI_OS_SLAB_MINI_BYTES CONFIG_MINI_OS_SLAB_MINI_BYTES
#else
#define MINI_OS_SLAB_MINI_BYTES 16
#endif

/**
 * @brief Slab size classes: one page (MINI_OS_SLAB_PAGE_SIZE) per class
 * @note
 *  - class sizes: 16, 32, 64, 128, 256 [, 512 with CONFIG_MINI_OS_SLAB_512]
 *  - page i serves class (i %% class count), so the class sequence repeats
 *    every count pages (1 page -> only 16B, 2 pages -> 16B+32B, ...)
 *  - requests larger than the max class go straight to the free list
 */
#ifdef CONFIG_MINI_OS_SLAB_512
#define MINI_OS_SLAB_CLASS_COUNT 6
#else
#define MINI_OS_SLAB_CLASS_COUNT 5
#endif
#define MINI_OS_SLAB_MAX_BYTES (MINI_OS_SLAB_MINI_BYTES << (MINI_OS_SLAB_CLASS_COUNT - 1))

MINI_OS_ASSERT(MINI_OS_SLAB_MINI_BYTES >= 16, "MINI_OS_SLAB_MINI_BYTES must not be less than 16");
MINI_OS_ASSERT(((MINI_OS_SLAB_PAGE_SIZE) & ((MINI_OS_SLAB_PAGE_SIZE)-1)) == 0, "MINI_OS_SLAB_PAGE_SIZE must be a power of 2");
MINI_OS_ASSERT((MINI_OS_SLAB_PAGE_SIZE) <= (1u << 16), "MINI_OS_SLAB_PAGE_SIZE must not exceed 64KB");
MINI_OS_ASSERT((MINI_OS_SLAB_PAGE_SIZE) >= (MINI_OS_SLAB_MAX_BYTES), "MINI_OS_SLAB_PAGE_SIZE must not be smaller than the max slab class");
#if defined(CONFIG_MINI_OS_SLAB_512) && defined(CONFIG_OPEN_SLAB)
MINI_OS_ASSERT((MINI_OS_SLAB_PAGE_MAX) >= (MINI_OS_SLAB_CLASS_COUNT), "CONFIG_MINI_OS_SLAB_512 needs at least 6 pages (raise CONFIG_MINI_OS_SLAB_PAGE_MAX)");
#endif

/**
 * @brief Static slab zone size (CONFIG_MINI_OS_SLAB_STATIC only)
 * @note
 *  - the slab zone is an independent static array, NOT carved out of the heap;
 *    the heap keeps 100% of its RAM for the free list
 *  - must be a multiple of MINI_OS_SLAB_MINI_BYTES and not exceed 64KB
 *  - PAGE_SIZE / PAGE_MAX / PROPORTION do not apply to the static zone
 */
#ifdef CONFIG_MINI_OS_SLAB_STATIC
#ifdef CONFIG_MINI_OS_SLAB_STATIC_SIZE
#define MINI_OS_SLAB_STATIC_SIZE CONFIG_MINI_OS_SLAB_STATIC_SIZE
#else
#define MINI_OS_SLAB_STATIC_SIZE 1024
#endif
MINI_OS_ASSERT((MINI_OS_SLAB_STATIC_SIZE % MINI_OS_SLAB_PAGE_SIZE) == 0, "MINI_OS_SLAB_STATIC_SIZE must be a multiple of MINI_OS_SLAB_PAGE_SIZE (one "
                                                                         "size class per page)");
MINI_OS_ASSERT((MINI_OS_SLAB_STATIC_SIZE) <= (1u << 16), "MINI_OS_SLAB_STATIC_SIZE must not exceed 64KB");
#ifdef CONFIG_MINI_OS_SLAB_512
MINI_OS_ASSERT((MINI_OS_SLAB_STATIC_SIZE) >= (MINI_OS_SLAB_CLASS_COUNT * MINI_OS_SLAB_PAGE_SIZE), "CONFIG_MINI_OS_SLAB_512 needs at least 6 static pages");
#endif
#endif /* CONFIG_MINI_OS_SLAB_STATIC */

/**
 * @brief check slab total take up much memory
 * @note  if slab has taken up 1/MINI_OS_SLAB_PROPORTION of the heap, return MINI_OS_ERR_NOMEM
 * @return MINI_OS_OK if slab total take up much memory, MINI_OS_ERR_NOMEM otherwise
 */
#ifdef CONFIG_OPEN_SLAB
mini_os_err_t mini_os_heap_validate(void);
#endif

#if defined(__cplusplus)
}
#endif
#endif
