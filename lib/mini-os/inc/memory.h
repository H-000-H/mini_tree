/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file memory.h
 * @brief Memory heap definitions and API (malloc/free model)
 * @author H-000-H
 * @details
 *  - malloc/free model: alloc returns a raw pointer, free takes only the
 *    pointer; the block contents are managed by the caller
 *  - three mutually exclusive builds (chosen by config at compile time):
 *      1. default: everything goes through the free list
 *         (first-fit, splitting, adjacent coalescing)
 *      2. CONFIG_OPEN_SLAB: small allocations (<= MINI_OS_SLAB_MAX_BYTES)
 *         go to size-class slab pages carved once from the pool head at init
 *         (class sizes 16/32/64/128/256 [, 512], one page per class,
 *         page i serves class i %% class count), falling back to the free
 *         list when the matching class is exhausted
 *      3. CONFIG_MINI_OS_SLAB_STATIC: same size classes over an independent
 *         static array (never touches the heap; the heap keeps all its RAM),
 *         falling back to the free list when the matching class is exhausted
 *  - free blocks / free slab objects embed list pointers; once occupied the
 *    pointer area is reused for user data and rewritten on free
 *  - the global heap memory comes from the linker script
 *    (mem_heap.h / mini-os-heap.ld)
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#if defined(__cplusplus)
extern "C"
{
#endif
#include "list.h"
#include "redef.h"

/*---------------------------------------------------------------------------------------------------------*/
/*                                 slab build selection (mutually exclusive) */
/*---------------------------------------------------------------------------------------------------------*/
#if defined(CONFIG_OPEN_SLAB) && defined(CONFIG_MINI_OS_SLAB_STATIC)
#error "CONFIG_OPEN_SLAB and CONFIG_MINI_OS_SLAB_STATIC are mutually exclusive"
#endif
#if defined(CONFIG_OPEN_SLAB) || defined(CONFIG_MINI_OS_SLAB_STATIC)
#include "mem_heap.h" /* slab size-class config (MINI_OS_SLAB_CLASS_COUNT etc.) */
#endif

/*---------------------------------------------------------------------------------------------------------*/
/*                                       memory pool macros */
/*---------------------------------------------------------------------------------------------------------*/
#ifdef CONFIG_MINI_OS_MEMORY_NAME_LEN
#define MINI_OS_MEMORY_NAME_LEN CONFIG_MINI_OS_MEMORY_NAME_LEN /**< max pool debug name length (incl. NUL) */
#else
#define MINI_OS_MEMORY_NAME_LEN 16
#endif

#ifdef CONFIG_MINI_OS_MEMORY_MAX_SEGS
#define MINI_OS_MEMORY_MAX_SEGS CONFIG_MINI_OS_MEMORY_MAX_SEGS /**< max registered memory segments per pool */
#else
#define MINI_OS_MEMORY_MAX_SEGS 4
#endif
// clang-format off
/*---------------------------------------------------------------------------------------------------------*/
/*                                       freelist block header */
/*---------------------------------------------------------------------------------------------------------*/
typedef struct mini_os_buffer_freelist_config mini_os_buffer_freelist_config_t;
/**
 * @brief Free-list block header (embedded in pool memory, i.e. the prefix of an allocated
 * block)
 * @note While free: size + magic + node are valid and the block is linked on the
 *       pool free list (magic == MINI_OS_MEMORY_MAGIC_FREE).
 *       While allocated: size + magic remain valid (size restores the block on
 *       free, magic validates the state), the node area is void.
 *       User data starts at the block base + aligned header size (see memory.c).
 */
struct mini_os_buffer_freelist_config
{
    mini_os_uint32_t size;      /**< data bytes of this block */
    mini_os_list_t   node;      /**< free-list node (doubly linked) */
    mini_os_uint32_t magic;     /**< state magic: allocated MINI_OS_MEMORY_MAGIC_ALLOC / free
                                        MINI_OS_MEMORY_MAGIC_FREE */
};

/*---------------------------------------------------------------------------------------------------------*/
/*                                       memory pool descriptor */
/*---------------------------------------------------------------------------------------------------------*/
/**
 * @brief Registered pool segment (kept sorted by length ascending)
 */
struct mini_os_memory_seg
{
    mini_os_uint8_t* base; /**< segment base */
    mini_os_size_t   len;  /**< segment bytes */
};

/**
 * @brief Memory pool descriptor (malloc/free model)
 */
struct mini_os_memory
{
    char             name[MINI_OS_MEMORY_NAME_LEN];             /**< debug name */
    mini_os_uint8_t* pool_base;                                 /**< first segment base */
    mini_os_size_t   pool_size;                                 /**< first segment bytes */
    mini_os_size_t   total_size;                                /**< total bytes of all segments */
    mini_os_size_t   free_size;                                 /**< currently free bytes (incl. free headers; maintained by
                                                                alloc/free for O(1) queries) */
    mini_os_list_t            free_list;                        /**< free-list head (sentinel) */
    struct mini_os_memory_seg segs[MINI_OS_MEMORY_MAX_SEGS];    /**< segment table */
    mini_os_uint32_t          seg_count;                        /**< number of registered segments */
    mini_os_atomic_uint32_t   used_count;                       /**< currently allocated block count */
    mini_os_atomic_uint32_t   peak;                             /**< historical peak allocated block count */
#ifdef CONFIG_OPEN_SLAB
    mini_os_uint8_t* slab_base;                                 /**< slab zone base (carved once at init, never returned;
                                                                  meaningless when slab_size == 0) */
    mini_os_size_t        slab_size;                            /**< slab zone total bytes (pages * MINI_OS_SLAB_PAGE_SIZE) */
    mini_os_single_list_t slab_free[MINI_OS_SLAB_CLASS_COUNT];  /**< free slot list per size class (sentinels) */
    mini_os_uint32_t      slab_page_count;                      /**< slab pages carved out (statistics) */
#endif
};
typedef struct mini_os_memory mini_os_memory_t;

/**
 * @brief Memory pool configuration
 */
typedef struct mini_os_memory_config
{
    const char* name;                                           /**< debug name (may be MINI_OS_NULL) */
    void*       static_mem;                                     /**< pool memory base (8-byte aligned; caller guarantees exclusive
                                                                     ownership) */
    mini_os_size_t static_len;                                  /**< pool memory bytes */
} mini_os_memory_config_t;
// clang-format on
/*---------------------------------------------------------------------------------------------------------*/
/*                            memory pool API (malloc/free model, unified errno) */
/*---------------------------------------------------------------------------------------------------------*/
/**
 * @brief Initialize a memory pool (the whole segment is merged into the free list)
 * @param[in] pool pool descriptor (storage provided by the caller)
 * @param[in] config pool configuration (name/static_mem/static_len)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL invalid arguments / pool segment too small;
 *         MINI_OS_ERR_NOSPC segment table error;
 *         MINI_OS_ERR_NOMEM with CONFIG_OPEN_SLAB when the pool is smaller than
 *         page size * proportion (8 KB by default) so not even 1 slab page fits
 */
mini_os_err_t mini_os_memory_init(mini_os_memory_t* pool, const mini_os_memory_config_t* config);

/**
 * @brief De-initialize a memory pool (only clears the descriptor; memory goes back to the
 * caller)
 * @param[in] pool pool descriptor
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when pool is MINI_OS_NULL
 */
mini_os_err_t mini_os_memory_deinit(mini_os_memory_t* pool);

/**
 * @brief Allocate a block of memory from the pool (malloc semantics)
 * @param[in] pool pool descriptor
 * @param[in] size requested bytes (internally rounded up to 8-byte alignment)
 * @return data pointer (8-byte aligned) on success; MINI_OS_NULL on invalid
 *         arguments or out of memory
 * @note With CONFIG_OPEN_SLAB, requests <= MINI_OS_SLAB_MAX_BYTES are
 *       rounded up to a size class and served from the matching slab page;
 *       when that class is exhausted the request falls back to the free list.
 *       CONFIG_MINI_OS_SLAB_STATIC does not apply to pool API, only to the
 *       global malloc/free entry.
 */
void* mini_os_memory_alloc(mini_os_memory_t* pool, mini_os_size_t size);

/**
 * @brief Return memory to the pool (free semantics, adjacent free blocks are coalesced)
 * @param[in] pool pool descriptor
 * @param[in] ptr pointer returned by mini_os_memory_alloc for this pool
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when pool/ptr is MINI_OS_NULL
 *         or the pointer is invalid / not owned by this pool
 * @note A double free is detected via the header magic and rejected, so the
 *       free list is never corrupted.
 */
mini_os_err_t mini_os_memory_free(mini_os_memory_t* pool, void* ptr);

/* ISR-safe variants: the critical section masks interrupts, this module never */
/* touches the native heap; the ISR path is identical to the thread path.      */
/**
 * @brief Allocate memory from ISR context (equivalent to mini_os_memory_alloc)
 */
void* mini_os_memory_alloc_isr(mini_os_memory_t* pool, mini_os_size_t size);

/**
 * @brief Return memory to the pool from ISR context (equivalent to mini_os_memory_free)
 */
mini_os_err_t mini_os_memory_free_isr(mini_os_memory_t* pool, void* ptr);

/**
 * @brief Append a memory segment to the pool (runtime expansion)
 * @param[in] pool pool descriptor
 * @param[in] mem segment base (must not overlap existing segments or any allocated block)
 * @param[in] len segment bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL invalid arguments / segment too small;
 *         MINI_OS_ERR_NOSPC segment table full
 * @note The new segment is merged into the free list and coalesced with adjacent
 *       free blocks; existing blocks are never moved.
 */
mini_os_err_t mini_os_memory_expand(mini_os_memory_t* pool, void* mem, mini_os_size_t len);

/**
 * @brief Query the total pool size (sum of all segments)
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 */
mini_os_size_t mini_os_memory_size(const mini_os_memory_t* pool);

/**
 * @brief Query the remaining free-list bytes (O(1), maintained by alloc/free)
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 * @note With CONFIG_OPEN_SLAB, pages already carved for slab are not on the
 *       free list and are not counted.
 */
mini_os_size_t mini_os_memory_free_space(const mini_os_memory_t* pool);

/**
 * @brief Query the current number of allocated blocks
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 */
mini_os_uint32_t mini_os_memory_used(const mini_os_memory_t* pool);

/**
 * @brief Query the historical peak number of allocated blocks
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 */
mini_os_uint32_t mini_os_memory_peak(const mini_os_memory_t* pool);

/**
 * @brief Reset the peak counter to the current number of allocated blocks
 * @param[in] pool pool descriptor
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when pool is MINI_OS_NULL
 */
mini_os_err_t mini_os_memory_reset_peak(mini_os_memory_t* pool);

/*---------------------------------------------------------------------------------------------------------*/
/*                                  global heap API (linker-script heap zone) */
/*---------------------------------------------------------------------------------------------------------*/
/**
 * @brief Allocate memory from the global heap (the heap zone comes from the
 *        linker script and is initialized automatically at startup)
 * @param[in] size requested bytes (0 returns MINI_OS_NULL)
 * @return data pointer (8-byte aligned) on success; MINI_OS_NULL when the heap
 *         is not ready or out of memory
 * @note With CONFIG_OPEN_SLAB, requests <= MINI_OS_SLAB_MAX_BYTES go to the
 *       heap-carved size-class slab pages first. With
 *       CONFIG_MINI_OS_SLAB_STATIC, such requests go to the independent
 *       static slab pages first (initialized at startup, independent of heap
 *       readiness); when the matching class is exhausted they fall back to
 *       the heap.
 */
void* mini_os_malloc(mini_os_size_t size);

/**
 * @brief Return memory to the global heap (adjacent free blocks are coalesced)
 * @param[in] ptr pointer returned by mini_os_malloc
 * @return MINI_OS_OK on success; MINI_OS_ERR_DEFER heap not ready;
 *         MINI_OS_ERR_INVAL invalid pointer
 */
mini_os_err_t mini_os_free(void* ptr);

/**
 * @brief Allocate zeroed memory from the global heap (count * size bytes)
 * @param[in] count number of elements
 * @param[in] size bytes per element (either 0 returns MINI_OS_NULL;
 *                multiplication overflow returns MINI_OS_NULL)
 * @return zeroed data pointer (8-byte aligned) on success; MINI_OS_NULL when
 *         the heap is not ready, out of memory, or on overflow
 */
void* mini_os_calloc(mini_os_size_t count, mini_os_size_t size);

/**
 * @brief Query the remaining free bytes of the global heap (O(1))
 * @return free bytes; 0 when the heap is not ready
 */
mini_os_size_t mini_os_heap_free_space(void);

/**
 * @brief Lazily take over the linker-script heap zone (idempotent)
 * @return MINI_OS_OK when the heap is ready; MINI_OS_ERR_NOMEM when the heap
 *         zone is absent (linker symbols collapsed to 0) or init failed
 * @note Normally invoked by the startup constructor before main; environments
 *       without .init_array traversal (bare-metal) call it explicitly or
 *       lazily before the first allocation. Not ISR-safe.
 */
mini_os_err_t mini_os_heap_ensure_init(void);

#if defined(__cplusplus)
}
#endif
#endif /* MEMORY_H */
