/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file memory.c
 * @brief malloc/free model memory manager (free list by default; with
 *        CONFIG_OPEN_SLAB small allocations go to slab; the global heap is
 *        provided by the linker script and taken over dynamically)
 * @author H-000-H
 */

#include "memory.h"

#include "err.h"
#include "mem_heap.h"
#include "redef.h"

/* -------------------------------------------------------------------------- */
/* Alignment granularity and minimum block                                    */
/* -------------------------------------------------------------------------- */
#define MINI_OS_MEMORY_ALIGN_SIZE 8u /**< data-area alignment granularity */
#define MINI_OS_MEMORY_MIN_BLOCK 16u /**< minimum free-list data block; do not split when the remainder falls below this */

/* Free-list block header size: the header is embedded in pool memory and the
 * data area follows it, aligned to 8 bytes. */
#define MINI_OS_MEMORY_HDR_SIZE MINI_OS_MEMORY_ALIGN_UP(sizeof(mini_os_buffer_freelist_config_t), MINI_OS_MEMORY_ALIGN_SIZE)

/* Header state magic: basis for double-free / invalid-pointer detection */
#define MINI_OS_MEMORY_MAGIC_ALLOC 0xA5A5A5A5u /**< block is allocated */
#define MINI_OS_MEMORY_MAGIC_FREE 0x5A5A5A5Au  /**< block is on the free list */

/**
 * @brief Insert a block at the head of the free list
 * @param[in] pool pool descriptor
 * @param[in] blk free block to insert
 * @note Also maintains the O(1) free_size accounting.
 * @note Invariant: every block on the free list has
 *       magic == MINI_OS_MEMORY_MAGIC_FREE (enforced here).
 */
static void mini_os_memory_freelist_push(struct mini_os_memory* pool, mini_os_buffer_freelist_config_t* blk)
{
    blk->magic = MINI_OS_MEMORY_MAGIC_FREE;
    pool->free_size += MINI_OS_MEMORY_HDR_SIZE + blk->size;
    mini_os_list_head(&blk->node, &pool->free_list);
}

/**
 * @brief Coalesce address-adjacent free blocks into blk, then insert it once
 * @param[in] pool pool descriptor
 * @param[in] blk block being returned to the free list
 * @note Strategy: walk the free list, absorb every block adjacent to blk
 *       (removing it from the list), then insert blk once at the end.
 *       blk is not on the list during the traversal, so it can never
 *       self-link. Large lists make this O(n); external fragmentation is
 *       lower than merging only the immediate neighbors.
 */
static void mini_os_memory_freelist_merge(struct mini_os_memory* pool, mini_os_buffer_freelist_config_t* blk)
{
    mini_os_list_t* node = pool->free_list.next;

    while (node != &pool->free_list)
    {
        mini_os_list_t*                   next_node = node->next;
        mini_os_buffer_freelist_config_t* it = mini_os_container_of(node, mini_os_buffer_freelist_config_t, node);
        mini_os_uint8_t*                  it_end = (mini_os_uint8_t*)it + MINI_OS_MEMORY_HDR_SIZE + it->size;
        mini_os_uint8_t*                  blk_end = (mini_os_uint8_t*)blk + MINI_OS_MEMORY_HDR_SIZE + blk->size;

        if (it_end == (mini_os_uint8_t*)blk)
        {
            /* it immediately precedes blk: it absorbs blk backwards */
            pool->free_size -= MINI_OS_MEMORY_HDR_SIZE + it->size; /* absorbed block leaves the list */
            it->size += MINI_OS_MEMORY_HDR_SIZE + blk->size;
            mini_os_list_remove(&it->node);
            blk = it;
        }
        else if (blk_end == (mini_os_uint8_t*)it)
        {
            /* blk immediately precedes it: blk absorbs it forwards */
            pool->free_size -= MINI_OS_MEMORY_HDR_SIZE + it->size; /* absorbed block leaves the list */
            blk->size += MINI_OS_MEMORY_HDR_SIZE + it->size;
            mini_os_list_remove(&it->node);
        }
        node = next_node;
    }
    mini_os_memory_freelist_push(pool, blk);
}

/**
 * @brief First-fit allocation: find a free block whose data area >= size
 * @param[in] pool pool descriptor
 * @param[in] size requested data bytes (rounded up to MINI_OS_MEMORY_ALIGN_SIZE)
 * @return the chosen block, removed from the list and marked allocated;
 *         MINI_OS_NULL when no block fits
 * @note If the remainder is large enough it is split off and stays on the
 *       free list; the returned block's magic becomes MINI_OS_MEMORY_MAGIC_ALLOC.
 */
static mini_os_buffer_freelist_config_t* mini_os_memory_freelist_alloc(struct mini_os_memory* pool, mini_os_size_t size)
{
    mini_os_list_t* node;

    size = MINI_OS_MEMORY_ALIGN_UP(size, MINI_OS_MEMORY_ALIGN_SIZE);
    for (node = pool->free_list.next; node != &pool->free_list; node = node->next)
    {
        mini_os_buffer_freelist_config_t* it = mini_os_container_of(node, mini_os_buffer_freelist_config_t, node);

        if (it->size < size)
            continue;
        {
            mini_os_size_t remain = it->size - size;

            pool->free_size -= MINI_OS_MEMORY_HDR_SIZE + it->size; /* whole block leaves the list (remainder re-added by push) */
            mini_os_list_remove(&it->node);
            if (remain >= MINI_OS_MEMORY_HDR_SIZE + MINI_OS_MEMORY_MIN_BLOCK)
            {
                mini_os_buffer_freelist_config_t* split = (mini_os_buffer_freelist_config_t*)((mini_os_uint8_t*)it + MINI_OS_MEMORY_HDR_SIZE + size);
                split->size = remain - MINI_OS_MEMORY_HDR_SIZE;
                mini_os_list_init(&split->node);
                mini_os_memory_freelist_push(pool, split); /* remainder stays on the list (push sets FREE) */
                it->size = size;
            }
            it->magic = MINI_OS_MEMORY_MAGIC_ALLOC; /* leaves the free list -> allocated */
            return it;
        }
    }
    return MINI_OS_NULL;
}

/**
 * @brief Return a block to the free list
 * @param[in] pool pool descriptor
 * @param[in] ptr data pointer of the block to free
 * @note The header sits before the data pointer; size/magic are preserved in
 *       the header while allocated.
 * @note Double-free detection: the magic must be MINI_OS_MEMORY_MAGIC_ALLOC,
 *       otherwise the block is already free (double free) or the pointer is
 *       invalid and the free is rejected.
 */
static void mini_os_memory_freelist_free(struct mini_os_memory* pool, void* ptr)
{
    mini_os_buffer_freelist_config_t* blk = (mini_os_buffer_freelist_config_t*)((mini_os_uint8_t*)ptr - MINI_OS_MEMORY_HDR_SIZE);

    if (blk->magic != MINI_OS_MEMORY_MAGIC_ALLOC)
        return; /* double free or invalid pointer: header is not in the allocated state, reject */
    if ((blk->size & (MINI_OS_MEMORY_ALIGN_SIZE - 1)) != 0)
        return; /* corrupted header: size not aligned */
    blk->magic = MINI_OS_MEMORY_MAGIC_FREE;
    mini_os_list_init(&blk->node);
    mini_os_memory_freelist_merge(pool, blk);
}

/* -------------------------------------------------------------------------- */
/* Segment table                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Register a segment, keeping the table sorted by length ascending
 * @param[in] pool pool descriptor
 * @param[in] base segment base
 * @param[in] len segment bytes
 * @return 0 on success; -1 when the table is full
 */
static mini_os_int32_t mini_os_memory_seg_add(struct mini_os_memory* pool, mini_os_uint8_t* base, mini_os_size_t len)
{
    mini_os_uint32_t insert_index;
    mini_os_uint32_t shift_index;

    if (pool->seg_count >= MINI_OS_MEMORY_MAX_SEGS)
        return -1;
    for (insert_index = 0; insert_index < pool->seg_count; insert_index++)
        if (len < pool->segs[insert_index].len)
            break;
    for (shift_index = pool->seg_count; shift_index > insert_index; shift_index--)
        pool->segs[shift_index] = pool->segs[shift_index - 1];
    pool->segs[insert_index].base = base;
    pool->segs[insert_index].len = len;
    pool->seg_count++;
    return 0;
}

/**
 * @brief Check whether a pointer lies within any registered segment
 * @param[in] pool pool descriptor
 * @param[in] ptr pointer to check
 * @return MINI_OS_TRUE when ptr falls inside a segment (with room for a header
 *         before it); MINI_OS_FALSE otherwise
 */
static mini_os_bool_t mini_os_memory_ptr_in_segments(const struct mini_os_memory* pool, const void* ptr)
{
    mini_os_uint32_t i;

    for (i = 0; i < pool->seg_count; i++)
    {
        const mini_os_uint8_t* base = pool->segs[i].base;
        const mini_os_uint8_t* end = base + pool->segs[i].len;

        if ((const mini_os_uint8_t*)ptr >= base + MINI_OS_MEMORY_HDR_SIZE && (const mini_os_uint8_t*)ptr <= end)
            return MINI_OS_TRUE;
    }
    return MINI_OS_FALSE;
}

/* -------------------------------------------------------------------------- */
/* Slab size classes (shared by CONFIG_OPEN_SLAB / CONFIG_MINI_OS_SLAB_STATIC) */
/* One page (MINI_OS_SLAB_PAGE_SIZE) per class: class sizes are               */
/* 16/32/64/128/256 [, 512], page i serves class (i %% class count) so the    */
/* class sequence repeats every count pages. Free slots embed a next pointer; */
/* when the matching class is exhausted the caller falls back to the free list.*/
/* -------------------------------------------------------------------------- */
#if defined(CONFIG_OPEN_SLAB) || defined(CONFIG_MINI_OS_SLAB_STATIC)

/**
 * @brief Map a request size to its size class index
 * @param[in] size requested bytes
 * @return class index 0..CLASS_COUNT-1; CLASS_COUNT when size exceeds the max
 *         class (caller goes straight to the free list)
 */
static mini_os_uint32_t mini_os_slab_class_of(mini_os_size_t size)
{
    mini_os_uint32_t cls = 0;

    while (cls + 1u < (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT && size > ((mini_os_size_t)MINI_OS_SLAB_MINI_BYTES << cls))
        cls++;
    if (size > ((mini_os_size_t)MINI_OS_SLAB_MINI_BYTES << cls))
        return (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT; /* over the max class */
    return cls;
}

/**
 * @brief Cut a slab zone into pages, each page into slots of its size class,
 *        and link every slot as free
 * @param[in] base zone base
 * @param[in] zone_size zone bytes (a multiple of MINI_OS_SLAB_PAGE_SIZE)
 * @param[out] free_list per-class free slot lists (CLASS_COUNT sentinels)
 */
static void mini_os_slab_zone_link(mini_os_uint8_t* base, mini_os_size_t zone_size, mini_os_single_list_t* free_list)
{
    mini_os_size_t   seg_total = zone_size / MINI_OS_SLAB_PAGE_SIZE;
    mini_os_size_t   i;
    mini_os_uint32_t cls;

    for (i = 0; i < seg_total; i++)
    {
        mini_os_size_t slot_size = (mini_os_size_t)MINI_OS_SLAB_MINI_BYTES << (i % MINI_OS_SLAB_CLASS_COUNT);
        mini_os_size_t slot_total = MINI_OS_SLAB_PAGE_SIZE / slot_size;
        mini_os_size_t j;

        cls = (mini_os_uint32_t)(i % MINI_OS_SLAB_CLASS_COUNT);
        for (j = 0; j < slot_total; j++)
        {
            mini_os_single_list_t* slot = (mini_os_single_list_t*)(base + i * MINI_OS_SLAB_PAGE_SIZE + j * slot_size);

            mini_os_single_list_init(slot);
            mini_os_single_list_push_heap(&free_list[cls], slot);
        }
    }
}

/**
 * @brief Take one slot from the given size class
 * @param[in] free_list per-class free slot lists
 * @param[in] cls size class index
 * @return slot pointer on success; MINI_OS_NULL when the class is exhausted
 *         (caller falls back to the free list)
 * @note the slot is unlinked first: once it is occupied the next-pointer area
 *       yields to user data and the pointer is dropped
 */
static void* mini_os_slab_class_alloc(mini_os_single_list_t* free_list, mini_os_uint32_t cls)
{
    mini_os_single_list_t* slot;

    if (mini_os_single_list_is_empty(&free_list[cls]) == MINI_OS_TRUE)
        return MINI_OS_NULL;
    slot = free_list[cls].next;
    mini_os_single_list_remove(slot, &free_list[cls]);
    return (void*)slot;
}

/**
 * @brief Return a slot to its size class (the class is derived from the page
 *        the pointer lives in, so the caller needs no allocation metadata)
 * @return 1 = freed; 0 = pointer not inside the zone;
 *         -1 = inside the zone but not aligned to a slot boundary (rejected)
 */
static mini_os_int32_t mini_os_slab_zone_free(const mini_os_uint8_t* base, mini_os_size_t zone_size, mini_os_single_list_t* free_list, void* ptr)
{
    const mini_os_uint8_t* p = (const mini_os_uint8_t*)ptr;
    mini_os_size_t         offset;
    mini_os_size_t         slot_size;
    mini_os_uint32_t       cls;

    if (zone_size == 0 || p < base || p >= base + zone_size)
        return 0;
    offset = (mini_os_size_t)(p - base);
    cls = (mini_os_uint32_t)((offset / MINI_OS_SLAB_PAGE_SIZE) % MINI_OS_SLAB_CLASS_COUNT);
    slot_size = (mini_os_size_t)MINI_OS_SLAB_MINI_BYTES << cls;
    if ((offset % slot_size) != 0)
        return -1; /* not aligned to a slot boundary: invalid pointer */
    {
        mini_os_single_list_t* slot = (mini_os_single_list_t*)ptr;

        mini_os_single_list_init(slot);
        mini_os_single_list_push_heap(&free_list[cls], slot);
    }
    return 1;
}

#endif /* CONFIG_OPEN_SLAB || CONFIG_MINI_OS_SLAB_STATIC */

/* -------------------------------------------------------------------------- */
/* Slab small-object allocation (CONFIG_OPEN_SLAB)                             */
/* A fixed zone is carved once from the pool head at init and never returned;  */
/* the zone is cut into size-class pages (see the shared helpers above). When  */
/* the matching class is exhausted slab_alloc returns MINI_OS_NULL and the     */
/* caller falls back to the free list.                                        */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_OPEN_SLAB

/**
 * @brief Carve the slab zone once at init and build the free slot list
 * @param[in] pool pool descriptor
 * @param[in] base pool memory base
 * @param[in] len pool memory bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_NOMEM when the pool is too small
 * @note Page count = min(MINI_OS_SLAB_PAGE_MAX, len/PROPORTION / PAGE_SIZE).
 *       When len < PAGE_SIZE * PROPORTION (8 KB with the default config) not
 *       even 1 page fits and initialization fails.
 */
static mini_os_err_t mini_os_memory_slab_zone_setup(struct mini_os_memory* pool, mini_os_uint8_t* base, mini_os_size_t len)
{
    mini_os_size_t   pages = MINI_OS_SLAB_PAGE_MAX;
    mini_os_uint32_t cls;

    if (pages * MINI_OS_SLAB_PAGE_SIZE > len / MINI_OS_SLAB_PROPORTION)
        pages = (len / MINI_OS_SLAB_PROPORTION) / MINI_OS_SLAB_PAGE_SIZE;
    if (pages == 0)
    {
        return MINI_OS_ERR_NOMEM; /* pool smaller than page size * proportion; slab configured but 0
                                     pages fit */
    }
    pool->slab_base = base;
    pool->slab_size = pages * MINI_OS_SLAB_PAGE_SIZE;
    pool->slab_page_count = pages;
    for (cls = 0; cls < (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT; cls++)
        mini_os_single_list_init(&pool->slab_free[cls]);
    /* Each page serves one size class (page i -> class i %% count), cut and linked as free */
    mini_os_slab_zone_link(base, pool->slab_size, pool->slab_free);
    return MINI_OS_OK;
}

/**
 * @brief Take one object slot from the size class matching size
 * @param[in] pool pool descriptor
 * @param[in] size requested bytes (rounded up to a size class)
 * @return slot pointer on success; MINI_OS_NULL when the matching class is
 *         empty or size exceeds the max class (caller falls back to the free list)
 */
static void* mini_os_memory_slab_alloc(struct mini_os_memory* pool, mini_os_size_t size)
{
    mini_os_uint32_t cls = mini_os_slab_class_of(size);

    if (cls >= (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT)
        return MINI_OS_NULL; /* over the max class, caller goes to the free list */
    return mini_os_slab_class_alloc(pool->slab_free, cls);
}

/**
 * @brief Return an object slot to its size class
 * @param[in] pool pool descriptor
 * @param[in] ptr pointer to free
 * @return 1 = freed; 0 = pointer not inside the slab zone; -1 = inside the
 *         zone but not aligned to a slot boundary (rejected)
 */
static mini_os_int32_t mini_os_memory_slab_free(struct mini_os_memory* pool, void* ptr) { return mini_os_slab_zone_free(pool->slab_base, pool->slab_size, pool->slab_free, ptr); }

#endif /* CONFIG_OPEN_SLAB */

/* -------------------------------------------------------------------------- */
/* Pool API                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize a memory pool (the whole segment is merged into the free list)
 * @param[in] pool pool descriptor (storage provided by the caller)
 * @param[in] config pool configuration (name/static_mem/static_len)
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL invalid arguments or a pool
 *         smaller than one header plus one minimum block; MINI_OS_ERR_NOSPC
 *         segment table error; MINI_OS_ERR_NOMEM with CONFIG_OPEN_SLAB when not
 *         even one slab page fits
 * @details the debug name is copied bounded and NUL-terminated, the counters and
 *          the free list are reset, then the pool is split: with
 *          CONFIG_OPEN_SLAB the slab zone is carved once from the pool head and
 *          never returned, and the remainder is registered as the first segment
 *          and pushed as a single free block
 */
mini_os_err_t mini_os_memory_init(mini_os_memory_t* pool, const mini_os_memory_config_t* config)
{
    mini_os_buffer_freelist_config_t* whole;
    mini_os_size_t                    name_len;
#ifdef CONFIG_OPEN_SLAB
    mini_os_uint32_t cls;
#endif

    if (pool == MINI_OS_NULL || config == MINI_OS_NULL || config->static_mem == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    if (config->static_len < MINI_OS_MEMORY_HDR_SIZE + MINI_OS_MEMORY_MIN_BLOCK)
        return MINI_OS_ERR_INVAL;

    /* Copy the debug name (bounded, always NUL-terminated) */
    name_len = 0;
    if (config->name != MINI_OS_NULL)
    {
        while (name_len < (mini_os_size_t)(MINI_OS_MEMORY_NAME_LEN - 1) && config->name[name_len] != '\0')
        {
            pool->name[name_len] = config->name[name_len];
            name_len++;
        }
    }
    pool->name[name_len] = '\0';

    pool->pool_base = (mini_os_uint8_t*)config->static_mem;
    pool->pool_size = config->static_len;
    pool->total_size = config->static_len;
    pool->free_size = 0;
    mini_os_list_init(&pool->free_list);
    pool->seg_count = 0;
    MINI_OS_ATOMIC_STORE(&pool->used_count, 0, MINI_OS_SEQ_CST);
    MINI_OS_ATOMIC_STORE(&pool->peak, 0, MINI_OS_SEQ_CST);
#ifdef CONFIG_OPEN_SLAB
    for (cls = 0; cls < (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT; cls++)
        mini_os_single_list_init(&pool->slab_free[cls]);
    pool->slab_base = MINI_OS_NULL;
    pool->slab_size = 0;
    pool->slab_page_count = 0;
#endif

    /* slab zone first, then the remainder as one free block */
    {
        mini_os_uint8_t* free_base = pool->pool_base;
        mini_os_size_t   free_len = config->static_len;

#ifdef CONFIG_OPEN_SLAB
        if (mini_os_memory_slab_zone_setup(pool, free_base, free_len) != MINI_OS_OK)
        {
            return MINI_OS_ERR_NOMEM; /* pool too small to carve 1 slab page (threshold = page size
                                       * proportion) */
        }
        free_base += pool->slab_size;
        free_len -= pool->slab_size;
#endif
        /* Register the initial segment; the whole segment becomes a single free block */
        if (mini_os_memory_seg_add(pool, free_base, free_len) != 0)
            return MINI_OS_ERR_NOSPC; /* cannot happen: the table was empty */
        whole = (mini_os_buffer_freelist_config_t*)free_base;
        whole->size = free_len - MINI_OS_MEMORY_HDR_SIZE;
        mini_os_list_init(&whole->node);
        mini_os_memory_freelist_push(pool, whole);
    }
    return MINI_OS_OK;
}

/**
 * @brief De-initialize a memory pool (clears the descriptor, the memory goes back
 *        to the caller)
 * @param[in] pool pool descriptor
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when pool is MINI_OS_NULL
 * @note every outstanding pointer of this pool becomes invalid: the free list and
 *       the segment table are dropped without walking the allocated blocks
 */
mini_os_err_t mini_os_memory_deinit(mini_os_memory_t* pool)
{
#ifdef CONFIG_OPEN_SLAB
    mini_os_uint32_t cls;
#endif

    if (pool == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    mini_os_list_init(&pool->free_list);
    pool->seg_count = 0;
    pool->pool_base = MINI_OS_NULL;
    pool->pool_size = 0;
    pool->total_size = 0;
    pool->free_size = 0;
    MINI_OS_ATOMIC_STORE(&pool->used_count, 0, MINI_OS_SEQ_CST);
    MINI_OS_ATOMIC_STORE(&pool->peak, 0, MINI_OS_SEQ_CST);
#ifdef CONFIG_OPEN_SLAB
    for (cls = 0; cls < (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT; cls++)
        mini_os_single_list_init(&pool->slab_free[cls]);
    pool->slab_base = MINI_OS_NULL;
    pool->slab_size = 0;
    pool->slab_page_count = 0;
#endif
    return MINI_OS_OK;
}

/**
 * @brief Bookkeeping for a successful allocation
 * @param[in] pool pool descriptor
 * @param[in] blk the newly allocated block
 * @note The caller must hold the pool lock (interrupts masked).
 */
static void mini_os_memory_count_alloc(struct mini_os_memory* pool)
{
    mini_os_uint32_t used = MINI_OS_ATOMIC_LOAD(&pool->used_count, MINI_OS_SEQ_CST) + 1;

    MINI_OS_ATOMIC_STORE(&pool->used_count, used, MINI_OS_SEQ_CST);
    if (used > MINI_OS_ATOMIC_LOAD(&pool->peak, MINI_OS_SEQ_CST))
        MINI_OS_ATOMIC_STORE(&pool->peak, used, MINI_OS_SEQ_CST);
}

/**
 * @brief Allocate a block of memory from the pool (malloc semantics)
 * @param[in] pool pool descriptor
 * @param[in] size requested bytes (rounded up to MINI_OS_MEMORY_ALIGN_SIZE)
 * @return data pointer (8-byte aligned) on success; MINI_OS_NULL on invalid
 *         arguments or when no block fits
 * @details the whole allocation runs with interrupts masked (the ISR path uses
 *          the same code), and the used/peak counters are updated only when a
 *          block was really handed out
 * @note with CONFIG_OPEN_SLAB a small request goes to the matching size class
 *       first and falls back to the free list when that class is exhausted or
 *       the size is over the max class
 */
void* mini_os_memory_alloc(mini_os_memory_t* pool, mini_os_size_t size)
{
    void*         ptr = MINI_OS_NULL;
    mini_os_irq_t lock_state;

    if (pool == MINI_OS_NULL || size == 0)
        return MINI_OS_NULL;
    lock_state = mini_os_irq_save();
#ifdef CONFIG_OPEN_SLAB
    /* slab class first, free list below as the fallback */
    if (size <= MINI_OS_SLAB_MAX_BYTES)
        ptr = mini_os_memory_slab_alloc(pool, size);
#endif
    if (ptr == MINI_OS_NULL)
    {
        mini_os_buffer_freelist_config_t* blk = mini_os_memory_freelist_alloc(pool, size);

        if (blk != MINI_OS_NULL)
            ptr = (mini_os_uint8_t*)blk + MINI_OS_MEMORY_HDR_SIZE;
    }
    if (ptr != MINI_OS_NULL)
        mini_os_memory_count_alloc(pool);
    mini_os_irq_restore(lock_state);
    return ptr;
}

/**
 * @brief Return a block to the pool (free semantics, adjacent free blocks are
 *        coalesced)
 * @param[in] pool pool descriptor
 * @param[in] ptr pointer returned by mini_os_memory_alloc() for this pool
 * @return MINI_OS_OK when the block was released; MINI_OS_ERR_INVAL when
 *         pool/ptr is MINI_OS_NULL or the pointer is invalid / not owned by this
 *         pool
 * @details with CONFIG_OPEN_SLAB the pointer is offered to the slab zone first:
 *          a hit (1) frees it, -1 means it is inside the zone but misaligned and
 *          is rejected, 0 means it belongs to the free list, which is only tried
 *          when the pointer lies inside a registered segment
 * @note the used counter is only decremented when something was really freed, so
 *       a rejected free cannot drive it below zero
 */
mini_os_err_t mini_os_memory_free(mini_os_memory_t* pool, void* ptr)
{
    mini_os_irq_t   lock_state;
    mini_os_int32_t slab_ret = 0;
    mini_os_bool_t  freed = MINI_OS_FALSE;

    if (pool == MINI_OS_NULL || ptr == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    lock_state = mini_os_irq_save();
#ifdef CONFIG_OPEN_SLAB
    slab_ret = mini_os_memory_slab_free(pool, ptr);
    if (slab_ret > 0)
        freed = MINI_OS_TRUE;
#endif
    /* slab_ret == 0: not part of any slab page, try the free list; == -1: inside a page but
     * invalid, reject */
    if (freed == MINI_OS_FALSE && slab_ret == 0 && mini_os_memory_ptr_in_segments(pool, ptr) == MINI_OS_TRUE)
    {
        mini_os_memory_freelist_free(pool, ptr);
        freed = MINI_OS_TRUE;
    }
    if (freed == MINI_OS_TRUE)
    {
        mini_os_uint32_t used = MINI_OS_ATOMIC_LOAD(&pool->used_count, MINI_OS_SEQ_CST);

        if (used > 0)
            MINI_OS_ATOMIC_STORE(&pool->used_count, used - 1, MINI_OS_SEQ_CST);
    }
    mini_os_irq_restore(lock_state);
    return (freed == MINI_OS_TRUE) ? MINI_OS_OK : MINI_OS_ERR_INVAL;
}

/**
 * @brief Append a memory segment to the pool (runtime expansion)
 * @param[in] pool pool descriptor
 * @param[in] mem segment base (must not overlap an existing segment)
 * @param[in] len segment bytes
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL on invalid arguments or a
 *         segment too small to hold a header plus one block;
 *         MINI_OS_ERR_NOSPC when the segment table is full
 * @note the segment is registered, pushed as one free block and coalesced with
 *       adjacent free blocks; existing blocks are never moved
 */
mini_os_err_t mini_os_memory_expand(mini_os_memory_t* pool, void* mem, mini_os_size_t len)
{
    mini_os_buffer_freelist_config_t* seg;
    mini_os_irq_t                     lock_state;

    if (pool == MINI_OS_NULL || mem == MINI_OS_NULL || len < MINI_OS_MEMORY_HDR_SIZE + MINI_OS_MEMORY_MIN_BLOCK)
        return MINI_OS_ERR_INVAL;
    lock_state = mini_os_irq_save();
    if (mini_os_memory_seg_add(pool, (mini_os_uint8_t*)mem, len) != 0)
    {
        mini_os_irq_restore(lock_state);
        return MINI_OS_ERR_NOSPC; /* segment table full */
    }
    seg = (mini_os_buffer_freelist_config_t*)mem;
    seg->size = len - MINI_OS_MEMORY_HDR_SIZE;
    mini_os_list_init(&seg->node);
    mini_os_memory_freelist_merge(pool, seg); /* coalesce with adjacent free blocks */
    pool->total_size += len;
    mini_os_irq_restore(lock_state);
    return MINI_OS_OK;
}

/**
 * @brief Allocate a block from ISR context (equivalent to mini_os_memory_alloc)
 * @param[in] pool pool descriptor
 * @param[in] size requested bytes (rounded up to MINI_OS_MEMORY_ALIGN_SIZE)
 * @return data pointer on success; MINI_OS_NULL on invalid arguments or out of
 *         memory
 * @note the critical section masks interrupts and the native heap is never
 *       touched, so the ISR path is identical to the thread path
 */
void* mini_os_memory_alloc_isr(mini_os_memory_t* pool, mini_os_size_t size) { return mini_os_memory_alloc(pool, size); }

/**
 * @brief Return a block to the pool from ISR context (equivalent to
 *        mini_os_memory_free)
 * @param[in] pool pool descriptor
 * @param[in] ptr pointer returned by mini_os_memory_alloc() for this pool
 * @return MINI_OS_OK when the block was released; MINI_OS_ERR_INVAL on invalid
 *         arguments or an invalid pointer
 */
mini_os_err_t mini_os_memory_free_isr(mini_os_memory_t* pool, void* ptr) { return mini_os_memory_free(pool, ptr); }

/* -------------------------------------------------------------------------- */
/* Statistics / diagnostics                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Query the total pool size (sum of all segments)
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 * @return total bytes registered on this pool
 */
mini_os_size_t mini_os_memory_size(const mini_os_memory_t* pool)
{
    if (pool == MINI_OS_NULL)
        return 0;
    return pool->total_size;
}

/**
 * @brief Query the remaining free bytes of the pool (O(1))
 * @param[in] pool pool descriptor; MINI_OS_NULL or an uninitialized pool returns 0
 * @return free bytes currently on the free list
 * @note free_size is maintained by the alloc/free paths, so no free-list walk is
 *       needed; with CONFIG_OPEN_SLAB the pages carved for slab are not counted
 */
mini_os_size_t mini_os_memory_free_space(const mini_os_memory_t* pool)
{
    mini_os_size_t total;
    mini_os_irq_t  lock_state;

    if (pool == MINI_OS_NULL || pool->pool_base == MINI_OS_NULL)
        return 0;
    /* free_size is maintained by the alloc/free paths; no free-list walk needed */
    lock_state = mini_os_irq_save();
    total = pool->free_size;
    mini_os_irq_restore(lock_state);
    return total;
}

/**
 * @brief Query the current number of allocated blocks
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 * @return blocks currently handed out by this pool
 */
mini_os_uint32_t mini_os_memory_used(const mini_os_memory_t* pool)
{
    if (pool == MINI_OS_NULL)
        return 0;
    return MINI_OS_ATOMIC_LOAD(&pool->used_count, MINI_OS_SEQ_CST);
}

/**
 * @brief Query the historical peak number of allocated blocks
 * @param[in] pool pool descriptor; MINI_OS_NULL returns 0
 * @return highest value the allocated block counter ever reached
 */
mini_os_uint32_t mini_os_memory_peak(const mini_os_memory_t* pool)
{
    if (pool == MINI_OS_NULL)
        return 0;
    return MINI_OS_ATOMIC_LOAD(&pool->peak, MINI_OS_SEQ_CST);
}

/**
 * @brief Reset the peak counter to the current number of allocated blocks
 * @param[in] pool pool descriptor
 * @return MINI_OS_OK on success; MINI_OS_ERR_INVAL when pool is MINI_OS_NULL
 */
mini_os_err_t mini_os_memory_reset_peak(mini_os_memory_t* pool)
{
    if (pool == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    MINI_OS_ATOMIC_STORE(&pool->peak, MINI_OS_ATOMIC_LOAD(&pool->used_count, MINI_OS_SEQ_CST), MINI_OS_SEQ_CST);
    return MINI_OS_OK;
}

/* -------------------------------------------------------------------------- */
/* Global heap: dynamically takes over the linker-script heap zone             */
/* (mem_heap.h / mini-os-heap.ld)                                              */
/* -------------------------------------------------------------------------- */

static mini_os_memory_t s_mini_os_heap_pool;                  /**< global heap pool descriptor */
static mini_os_bool_t   s_mini_os_heap_ready = MINI_OS_FALSE; /**< heap ready flag */

/* -------------------------------------------------------------------------- */
/* Static slab zone (CONFIG_MINI_OS_SLAB_STATIC): an independent static array, */
/* never carved out of the heap; the heap keeps 100% of its RAM. When the      */
/* static slots are exhausted the request falls back to the free list.         */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_MINI_OS_SLAB_STATIC

/** Static slab zone storage (RAM, owned by the allocator) */
static mini_os_uint8_t       s_mini_os_slab_static_mem[MINI_OS_SLAB_STATIC_SIZE] MINI_OS_ALIGN(8);
static mini_os_single_list_t s_mini_os_slab_static_free[MINI_OS_SLAB_CLASS_COUNT]; /**< free slot list per size class
                                                                                      (sentinels) */
static mini_os_bool_t s_mini_os_slab_static_ready = MINI_OS_FALSE;                 /**< static zone ready flag */

/**
 * @brief Cut the static zone into size-class pages and link every slot as
 *        free (called by the startup constructor)
 */
static void mini_os_slab_static_zone_setup(void)
{
    mini_os_uint32_t cls;

    for (cls = 0; cls < (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT; cls++)
        mini_os_single_list_init(&s_mini_os_slab_static_free[cls]);
    /* Each page serves one size class (page i -> class i %% count) */
    mini_os_slab_zone_link(s_mini_os_slab_static_mem, MINI_OS_SLAB_STATIC_SIZE, s_mini_os_slab_static_free);
    s_mini_os_slab_static_ready = MINI_OS_TRUE;
}

/**
 * @brief Take one slot from the size class matching size
 * @return slot pointer on success; MINI_OS_NULL when the matching class is
 *         empty or size exceeds the max class (caller falls back to the heap)
 */
static void* mini_os_slab_static_alloc(mini_os_size_t size)
{
    mini_os_uint32_t cls = mini_os_slab_class_of(size);

    if (cls >= (mini_os_uint32_t)MINI_OS_SLAB_CLASS_COUNT)
        return MINI_OS_NULL; /* over the max class, caller goes to the heap */
    return mini_os_slab_class_alloc(s_mini_os_slab_static_free, cls);
}

/**
 * @brief Return a slot to its size class
 * @param[in] ptr pointer to free
 * @return 1 = freed; 0 = pointer not inside the static zone;
 *         -1 = inside the zone but not aligned to a slot boundary (rejected)
 */
static mini_os_int32_t mini_os_slab_static_free(void* ptr) { return mini_os_slab_zone_free(s_mini_os_slab_static_mem, MINI_OS_SLAB_STATIC_SIZE, s_mini_os_slab_static_free, ptr); }

#endif /* CONFIG_MINI_OS_SLAB_STATIC */

/**
 * @brief Validate the slab occupation ratio (only meaningful with CONFIG_OPEN_SLAB;
 *        invoked by the startup constructor; exceeding the limit is a configuration error)
 * @return MINI_OS_OK configuration valid; MINI_OS_ERR_NOMEM the slab ceiling
 *         exceeds 1/MINI_OS_SLAB_PROPORTION of the heap
 */
#ifdef CONFIG_OPEN_SLAB
MINI_OS_CONSTRUCTOR(101) mini_os_err_t mini_os_heap_validate(void)
{
    /* page size * proportion must be less than heap size and page limited by init function*/
    if ((mini_os_size_t)MINI_OS_HEAP_SIZE < ((mini_os_size_t)MINI_OS_SLAB_PAGE_SIZE * MINI_OS_SLAB_PROPORTION))
        return MINI_OS_ERR_NOMEM;
    return MINI_OS_OK;
}

/**
 * @brief Halt on a slab configuration failure (constructor, runs before main)
 */
MINI_OS_CONSTRUCTOR(101) static void mini_os_slab_validate_ctor(void)
{
    if (mini_os_heap_validate() != MINI_OS_OK)
    {
        for (;;)
        {
        }
    }
}
#endif /* CONFIG_OPEN_SLAB */

/* Heap auto-init constructor priority (value 102 wrapped in a named macro), runs before main */
#define MINI_OS_MEMORY_PRESTRUCTOR 102

/**
 * @brief Lazily take over the linker-script heap zone (idempotent, public)
 * @return MINI_OS_OK when the heap is ready; MINI_OS_ERR_NOMEM when the heap
 *         zone is absent (linker symbols collapsed to 0) or init failed
 * @details Merges the linker-script heap zone into the free list, i.e. the
 *          global-heap variant of mini_os_memory_init; also cuts the static
 *          slab zone once when CONFIG_MINI_OS_SLAB_STATIC is enabled
 *          (independent of the heap).
 * @note The startup constructor (below) calls this before main. Environments
 *       that do not iterate .init_array (bare-metal startup) can call it
 *       lazily before the first allocation — the ready flag makes repeated
 *       calls no-ops. Not ISR-safe: the free list is unprotected, same as
 *       libc malloc; call from thread/main context only.
 */
mini_os_err_t mini_os_heap_ensure_init(void)
{
    mini_os_memory_config_t cfg;

    if (s_mini_os_heap_ready)
        return MINI_OS_OK;
#ifdef CONFIG_MINI_OS_SLAB_STATIC
    if (!s_mini_os_slab_static_ready)
        mini_os_slab_static_zone_setup(); /* static zone ready before the heap, no heap dependency */
#endif
    if ((__mini_os_heap_end - __mini_os_heap_start) <= 0)
        return MINI_OS_ERR_NOMEM; /* linker script anomaly / host environment: heap stays not ready */
    cfg.name = "heap";
    cfg.static_mem = (void*)__mini_os_heap_start;
    cfg.static_len = MINI_OS_HEAP_SIZE;
    if (mini_os_memory_init(&s_mini_os_heap_pool, &cfg) != MINI_OS_OK)
        return MINI_OS_ERR_NOMEM;
    s_mini_os_heap_ready = MINI_OS_TRUE;
    return MINI_OS_OK;
}

/**
 * @brief Heap initialization (auto-run constructor before main)
 */
MINI_OS_CONSTRUCTOR(MINI_OS_MEMORY_PRESTRUCTOR) static void mini_os_heap_init_ctor(void)
{
    (void)mini_os_heap_ensure_init();
}

/**
 * @brief Allocate memory from the global heap
 * @param[in] size requested bytes (0 returns MINI_OS_NULL)
 * @return data pointer (8-byte aligned) on success; MINI_OS_NULL when the heap
 *         is not ready or out of memory
 * @details with CONFIG_MINI_OS_SLAB_STATIC a small request goes to the
 *          independent static slab zone first (ready before the heap and not
 *          carved out of it); when the matching class is exhausted the request
 *          falls back to the heap
 * @note with CONFIG_OPEN_SLAB the small classes live in pages carved from the
 *       heap itself, so they are served inside mini_os_memory_alloc()
 */
void* mini_os_malloc(mini_os_size_t size)
{
#ifdef CONFIG_MINI_OS_SLAB_STATIC
    void* ptr;
#endif

    if (size == 0)
        return MINI_OS_NULL;
#ifdef CONFIG_MINI_OS_SLAB_STATIC
    if (size <= MINI_OS_SLAB_MAX_BYTES && s_mini_os_slab_static_ready == MINI_OS_TRUE)
    {
        ptr = mini_os_slab_static_alloc(size);
        if (ptr != MINI_OS_NULL)
            return ptr;
        /* matching class exhausted: fall back to the heap when it exists */
    }
#endif
    if (s_mini_os_heap_ready != MINI_OS_TRUE)
        return MINI_OS_NULL;
    return mini_os_memory_alloc(&s_mini_os_heap_pool, size);
}

/**
 * @brief Return memory to the global heap (adjacent free blocks are coalesced)
 * @param[in] ptr pointer returned by mini_os_malloc()
 * @return MINI_OS_OK on success; MINI_OS_ERR_DEFER while the heap is not ready
 *         (retry later); MINI_OS_ERR_INVAL on an invalid or misaligned pointer
 * @details with CONFIG_MINI_OS_SLAB_STATIC the pointer is offered to the static
 *          zone first: 1 means it belonged there, -1 means it is inside the zone
 *          but not slot aligned, and only otherwise does the heap see it
 * @note freeing a NULL pointer of a static-slab build is rejected early, before
 *       the heap readiness check
 */
mini_os_err_t mini_os_free(void* ptr)
{
#ifdef CONFIG_MINI_OS_SLAB_STATIC
    mini_os_int32_t result;

    if (ptr == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
    result = mini_os_slab_static_free(ptr);
    if (result == 1)
        return MINI_OS_OK; /* pointer belonged to the static slab zone */
    if (result < 0)
        return MINI_OS_ERR_INVAL; /* inside the static zone but misaligned */
#endif
    if (s_mini_os_heap_ready != MINI_OS_TRUE)
        return MINI_OS_ERR_DEFER; /* heap not ready, retry later */
    return mini_os_memory_free(&s_mini_os_heap_pool, ptr);
}

/**
 * @brief Allocate zeroed memory from the global heap (count * size bytes)
 * @param[in] count number of elements (0 returns MINI_OS_NULL)
 * @param[in] size bytes per element (0 returns MINI_OS_NULL)
 * @return zeroed data pointer (8-byte aligned) on success; MINI_OS_NULL when the
 *         heap is not ready, out of memory, or on multiplication overflow
 * @note the multiplication is guarded before the allocation, so a wrapping
 *       count * size can never request a short buffer
 */
void* mini_os_calloc(mini_os_size_t count, mini_os_size_t size)
{
    mini_os_size_t total;
    void*          ptr;

    if (count == 0 || size == 0)
        return MINI_OS_NULL;
    /* Multiplication overflow guard */
    if (count > ((mini_os_size_t)-1) / size)
        return MINI_OS_NULL;
    total = count * size;
    ptr = mini_os_malloc(total);
    if (ptr != MINI_OS_NULL)
        MINI_OS_MEMSET(ptr, 0, total);
    return ptr;
}

/**
 * @brief Query the remaining free bytes of the global heap (O(1))
 * @return free bytes; 0 when the heap is not ready
 */
mini_os_size_t mini_os_heap_free_space(void)
{
    if (s_mini_os_heap_ready != MINI_OS_TRUE)
        return 0;
    return mini_os_memory_free_space(&s_mini_os_heap_pool);
}
