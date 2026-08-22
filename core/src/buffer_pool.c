/*
 * SPDX-License-Identifier: Apache-2.0
 * @file buffer_pool.c
 */

#include "buffer_pool.h"

#include <errno.h> /* EINVAL */
#include <stdlib.h> /* calloc/free */
#include <string.h>
#define CONFIG_BUFFER_POOL 1 /* force enable for standalone build */
#ifdef CONFIG_BUFFER_POOL

#ifndef CONFIG_BUFFER_POOL_SIZE
#define CONFIG_BUFFER_POOL_SIZE 4096u /* default static pool bytes when not injected */
#endif

#if defined(CONFIG_BUFFER_POOL_DYNAMIC_ONLY)
#define BUFFER_POOL_DYNAMIC_ONLY 1
#else
#define BUFFER_POOL_DYNAMIC_ONLY 0
#endif

/* ═══════════════════════════════════════════════════════════════
 * Atomic / critical-section wrappers (no architecture-provided atomics)
 * ARMv6-M (Cortex-M0/M0+) and ARMv8-M Baseline (Cortex-M23) lack
 * LDREX/STREX, so interrupts are masked; other architectures use
 * GCC __atomic builtins.
 * ═══════════════════════════════════════════════════════════════ */

#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)

static inline uint32_t BUFF_POOL_CRITICAL_ENTER(void)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, PRIMASK\n\tcpsid i" : "=r"(primask)::"memory");
    return primask;
}

static inline void BUFF_POOL_CRITICAL_EXIT(uint32_t primask) { __asm__ volatile("msr PRIMASK, %0" ::"r"(primask) : "memory"); }

#define BUFF_POOL_ATOMIC_LOAD(p) (*(p))
#define BUFF_POOL_ATOMIC_STORE(p, v) (*(p) = (v))
#define BUFF_POOL_ATOMIC_ADD_FETCH(p, v) (*(p) += (v))
#define BUFF_POOL_ATOMIC_SUB_FETCH(p, v) (*(p) -= (v))

#else /* ARMv7-M+ / RISC-V / others: hardware atomics */

#define BUFF_POOL_ATOMIC_LOAD(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_ADD_FETCH(p, v) __atomic_add_fetch((p), (v), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_SUB_FETCH(p, v) __atomic_sub_fetch((p), (v), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_CAS(p, e, d) __atomic_compare_exchange_n((p), (e), (d), 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)

#endif

/* ── Alignment and minimum block ── */
#define BUFF_POOL_ALIGN_SIZE 8u /* alignment granularity for blocks and data */
#define BUFF_POOL_MIN_BLOCK 16u /* do not split when the remainder is below this */

#define BUFF_POOL_ALIGN_UP(x, a) (((x) + ((a) - 1u)) & ~((a) - 1u))

/* ── Free block header (embedded in pool memory; shares the header area
 *    with the allocated block metadata) ── */
struct buff_pool_free_block
{
    struct buff_pool_free_block* prev;
    struct buff_pool_free_block* next;
    size_t size; /**< data bytes of this free block */
};

/* Unified header size: free_block when free, buffer_block_t when allocated */
#define BUFF_POOL_HEAD_SIZE (BUFF_POOL_ALIGN_UP((sizeof(struct buff_pool_free_block) > sizeof(buffer_block_t) ? sizeof(struct buff_pool_free_block) : sizeof(buffer_block_t)), BUFF_POOL_ALIGN_SIZE))

#define BUFF_POOL_MAX_SEGS 4u /* maximum number of pool segments (initial + expands) */

/* One pool segment (initial pool or an appended expansion segment) */
struct buff_pool_seg
{
    uint8_t* base;
    size_t len; /**< segment bytes */
};

struct buffer_pool
{
    const char* name;
    buff_pool_atomic_uint_t lock; /**< pool lock (self-wrapped test-and-set / irq mask) */
    bool use_static; /**< static pool enabled (static-first mode with a pool) */
    uint8_t* pool_base; /**< initial static pool base */
    size_t pool_size; /**< initial static pool bytes */
    bool pool_owned; /**< true = pool memory allocated by this pool (freed on destroy) */
    struct buff_pool_free_block* free_list; /**< free list head */
    size_t total_size; /**< cumulative size of all segments */
    struct buff_pool_seg segs[BUFF_POOL_MAX_SEGS]; /**< segment table, sorted by len asc */
    uint32_t seg_count; /**< number of registered segments */
    buff_pool_atomic_uint_t used_count; /**< currently allocated blocks */
    buff_pool_atomic_uint_t peak; /**< peak usage */
};

/* Register a segment, keeping the table sorted by length ascending.
 * Returns 0 on success, -1 when the table is full. */
static int buff_pool_seg_add(struct buffer_pool* pool, uint8_t* base, size_t len)
{
    uint32_t i, j;
    if (pool->seg_count >= BUFF_POOL_MAX_SEGS)
        return -1;
    for (i = 0; i < pool->seg_count; i++)
        if (len < pool->segs[i].len)
            break;
    for (j = pool->seg_count; j > i; j--)
        pool->segs[j] = pool->segs[j - 1];
    pool->segs[i].base = base;
    pool->segs[i].len = len;
    pool->seg_count++;
    return 0;
}

/* ── Pool critical section (lock) ── */

static void buff_pool_lock_enter(buffer_pool_t* pool, uint32_t* state)
{
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    /* Masking interrupts is the critical section (single-core safe) */
    (void)pool;
    *state = BUFF_POOL_CRITICAL_ENTER();
#else
    uint32_t expected = 0u;
    (void)state;
    while (!BUFF_POOL_ATOMIC_CAS(&pool->lock, &expected, 1u))
        expected = 0u; /* spin-wait */
#endif
}

static void buff_pool_lock_exit(buffer_pool_t* pool, uint32_t state)
{
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    (void)pool;
    BUFF_POOL_CRITICAL_EXIT(state);
#else
    (void)state;
    BUFF_POOL_ATOMIC_STORE(&pool->lock, 0u);
#endif
}

/* ── Free-list operations (caller must hold the pool lock) ── */

static void buff_pool_freelist_push(struct buffer_pool* pool, struct buff_pool_free_block* blk)
{
    blk->prev = NULL;
    blk->next = pool->free_list;
    if (pool->free_list != NULL)
        pool->free_list->prev = blk;
    pool->free_list = blk;
}

static void buff_pool_freelist_remove(struct buffer_pool* pool, struct buff_pool_free_block* blk)
{
    if (blk->prev != NULL)
        blk->prev->next = blk->next;
    else
        pool->free_list = blk->next;
    if (blk->next != NULL)
        blk->next->prev = blk->prev;
    blk->prev = NULL;
    blk->next = NULL;
}

/* Return a block to the list, coalescing with adjacent free blocks
 * (contiguous addresses).
 * Strategy: absorb any adjacent free block into blk (removing it from the
 * list), then insert blk once at the end. blk is never part of the list
 * during traversal, so no self-linking can occur. */
static void buff_pool_freelist_merge(struct buffer_pool* pool, struct buff_pool_free_block* blk)
{
    struct buff_pool_free_block* it = pool->free_list;
    blk->prev = NULL;
    blk->next = NULL;
    while (it != NULL)
    {
        struct buff_pool_free_block* next = it->next;
        uint8_t* it_end = (uint8_t*)it + BUFF_POOL_HEAD_SIZE + it->size;
        uint8_t* blk_end = (uint8_t*)blk + BUFF_POOL_HEAD_SIZE + blk->size;

        if (it_end == (uint8_t*)blk) /* it immediately precedes blk */
        {
            it->size += BUFF_POOL_HEAD_SIZE + blk->size;
            buff_pool_freelist_remove(pool, it);
            blk = it; /* blk grows backwards to cover it; it is now off-list */
        }
        else if (blk_end == (uint8_t*)it) /* blk immediately precedes it */
        {
            blk->size += BUFF_POOL_HEAD_SIZE + it->size;
            buff_pool_freelist_remove(pool, it);
        }
        it = next;
    }
    buff_pool_freelist_push(pool, blk);
}

/* First-fit: find a free block with data >= size; split and keep the remainder.
 * If seg != NULL, only free blocks located inside that segment are considered
 * (address range check). The chosen block is REMOVED from the list and handed
 * to the caller; the remainder (split) stays in the list where the block was. */
static struct buff_pool_free_block* buff_pool_freelist_alloc(struct buffer_pool* pool, size_t size, const struct buff_pool_seg* seg)
{
    struct buff_pool_free_block* it = pool->free_list;
    uint8_t* s_base = seg ? seg->base : NULL;
    uint8_t* s_end = seg ? seg->base + seg->len : NULL;
    size = BUFF_POOL_ALIGN_UP(size, BUFF_POOL_ALIGN_SIZE);
    while (it != NULL)
    {
        struct buff_pool_free_block* next = it->next;
        if (seg != NULL)
        {
            if ((uint8_t*)it < s_base || (uint8_t*)it >= s_end)
            {
                it = next; /* block lies outside this segment */
                continue;
            }
        }
        if (it->size >= size)
        {
            size_t remain = it->size - size;
            if (remain >= BUFF_POOL_HEAD_SIZE + BUFF_POOL_MIN_BLOCK)
            {
                struct buff_pool_free_block* split = (struct buff_pool_free_block*)((uint8_t*)it + BUFF_POOL_HEAD_SIZE + size);
                buff_pool_freelist_remove(pool, it);
                split->prev = NULL;
                split->next = NULL;
                split->size = remain - BUFF_POOL_HEAD_SIZE;
                buff_pool_freelist_push(pool, split); /* remainder stays in the list */
                it->size = size; /* shrink the chosen block (now off-list) */
            }
            else
            {
                buff_pool_freelist_remove(pool, it);
            }
            return it;
        }
        it = next;
    }
    return NULL;
}

/* Largest single free block inside one segment (caller holds the lock) */
static size_t buff_pool_seg_max_free(const struct buffer_pool* pool, const struct buff_pool_seg* seg)
{
    const struct buff_pool_free_block* it = pool->free_list;
    uint8_t* s_base = seg->base;
    uint8_t* s_end = seg->base + seg->len;
    size_t maxf = 0u;
    while (it != NULL)
    {
        if ((uint8_t*)it >= s_base && (uint8_t*)it < s_end && it->size > maxf)
            maxf = it->size;
        it = it->next;
    }
    return maxf;
}

/* Largest single free block across all segments */
static size_t buff_pool_total_max_free(const struct buffer_pool* pool)
{
    size_t maxf = 0u;
    uint32_t s;
    for (s = 0; s < pool->seg_count; s++)
    {
        size_t m = buff_pool_seg_max_free(pool, &pool->segs[s]);
        if (m > maxf)
            maxf = m;
    }
    return maxf;
}

/* Cut a block from the static pool.
 * Segments are tried from the SMALLEST upwards (table is sorted by len asc):
 * the smallest segment is drained first, larger segments only after it can
 * no longer satisfy the request. Returns NULL when no segment fits. */
static buffer_block_t* buff_pool_alloc_static_impl(struct buffer_pool* pool, size_t size)
{
    uint32_t s;
    struct buff_pool_free_block* f;
    buffer_block_t* blk;
    if (pool->pool_base == NULL || pool->seg_count == 0u)
        return NULL;
    for (s = 0; s < pool->seg_count; s++)
    {
        struct buff_pool_seg* seg = &pool->segs[s];
        if (buff_pool_seg_max_free(pool, seg) < size)
            continue; /* this segment cannot satisfy; move up */
        f = buff_pool_freelist_alloc(pool, size, seg);
        if (f == NULL)
            continue; /* fragmented; try the next segment */
        {
            size_t cap = f->size;
            blk = (buffer_block_t*)f;
            blk->raw = (uint8_t*)f;
            blk->data = (uint8_t*)f + BUFF_POOL_HEAD_SIZE;
            blk->capacity = cap;
            BUFF_POOL_ATOMIC_STORE(&blk->head, 0u);
            BUFF_POOL_ATOMIC_STORE(&blk->tail, 0u);
            blk->from_static = true;
        }
        return blk;
    }
    return NULL;
}

/* Dynamic allocation: block metadata and data area are allocated together */
static buffer_block_t* buff_pool_alloc_dynamic(size_t size)
{
    buffer_block_t* blk;
    size = BUFF_POOL_ALIGN_UP(size, BUFF_POOL_ALIGN_SIZE);
    blk = (buffer_block_t*)calloc(1u, BUFF_POOL_HEAD_SIZE + size);
    if (blk == NULL)
        return NULL;
    blk->raw = (uint8_t*)blk;
    blk->data = (uint8_t*)blk + BUFF_POOL_HEAD_SIZE;
    blk->capacity = size;
    BUFF_POOL_ATOMIC_STORE(&blk->head, 0u);
    BUFF_POOL_ATOMIC_STORE(&blk->tail, 0u);
    blk->from_static = false;
    return blk;
}

/* ═══════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════ */

buffer_pool_t* buffer_pool_create(const buffer_pool_config_t* config)
{
    buffer_pool_t* pool;
    if (config == NULL)
        return NULL;
    pool = (buffer_pool_t*)calloc(1u, sizeof(*pool));
    if (pool == NULL)
        return NULL;
    pool->name = config->name;
    BUFF_POOL_ATOMIC_STORE(&pool->lock, 0u);

#if BUFFER_POOL_DYNAMIC_ONLY
    /* Dynamic-only mode: the static pool is disabled */
    pool->use_static = false;
#else
    /* Static-first mode: enable the static pool (caller-provided or internal) */
    if (config->use_static)
    {
        size_t total = config->static_len;
        if (total == 0u)
            total = CONFIG_BUFFER_POOL_SIZE;
        if (total < BUFF_POOL_HEAD_SIZE + BUFF_POOL_MIN_BLOCK)
        {
            free(pool);
            return NULL;
        }
        if (config->static_mem != NULL)
        {
            pool->pool_base = (uint8_t*)config->static_mem;
            pool->pool_owned = false;
        }
        else
        {
            pool->pool_base = (uint8_t*)calloc(1u, total);
            if (pool->pool_base == NULL)
            {
                free(pool);
                return NULL;
            }
            pool->pool_owned = true;
        }
        pool->pool_size = total;
        pool->total_size = total;
        /* Register the initial segment */
        if (buff_pool_seg_add(pool, pool->pool_base, total) != 0)
        {
            if (pool->pool_owned)
                free(pool->pool_base);
            free(pool);
            return NULL;
        }
        /* The whole pool becomes a single free block */
        {
            struct buff_pool_free_block* whole = (struct buff_pool_free_block*)pool->pool_base;
            whole->prev = NULL;
            whole->next = NULL;
            whole->size = total - BUFF_POOL_HEAD_SIZE;
            pool->free_list = whole;
        }
        pool->use_static = true;
    }
#endif /* BUFFER_POOL_DYNAMIC_ONLY */
    return pool;
}

void buffer_pool_destroy(buffer_pool_t* pool)
{
    if (pool == NULL)
        return;
    if (pool->pool_owned && pool->pool_base != NULL)
        free(pool->pool_base);
    free(pool);
}

/* Bookkeeping for a successful allocation (caller holds the lock) */
static void buff_pool_count_alloc(buffer_pool_t* pool, buffer_block_t* blk)
{
    uint32_t used = BUFF_POOL_ATOMIC_ADD_FETCH(&pool->used_count, 1u);
    uint32_t p;
    blk->pool = pool;
    do
    {
        p = BUFF_POOL_ATOMIC_LOAD(&pool->peak);
        if (used <= p)
            break;
    } while (!BUFF_POOL_ATOMIC_CAS(&pool->peak, &p, used));
}

buffer_block_t* buffer_pool_alloc_static(buffer_pool_t* pool, size_t size)
{
    buffer_block_t* blk;
    uint32_t lock_state;
    if (pool == NULL || size == 0u)
        return NULL;
    buff_pool_lock_enter(pool, &lock_state);
    blk = buff_pool_alloc_static_impl(pool, size);
    if (blk != NULL)
        buff_pool_count_alloc(pool, blk);
    buff_pool_lock_exit(pool, lock_state);
    return blk;
}

buffer_block_t* buffer_pool_alloc(buffer_pool_t* pool, size_t size)
{
    buffer_block_t* blk;
    uint32_t lock_state;
    if (pool == NULL || size == 0u)
        return NULL;
    buff_pool_lock_enter(pool, &lock_state);
    /* Threshold: if the request exceeds the largest single free block in ANY
     * segment, the static pool cannot satisfy it — go straight to dynamic. */
    if (size > buff_pool_total_max_free(pool))
    {
        blk = buff_pool_alloc_dynamic(size);
    }
    else
    {
        blk = buff_pool_alloc_static_impl(pool, size);
        if (blk == NULL)
            blk = buff_pool_alloc_dynamic(size);
    }
    if (blk != NULL)
        buff_pool_count_alloc(pool, blk);
    buff_pool_lock_exit(pool, lock_state);
    return blk;
}

int buffer_pool_expand(buffer_pool_t* pool, void* mem, size_t len)
{
    struct buff_pool_free_block* seg;
    uint32_t lock_state;
    if (pool == NULL || mem == NULL || len < BUFF_POOL_HEAD_SIZE + BUFF_POOL_MIN_BLOCK)
        return -EINVAL;
    buff_pool_lock_enter(pool, &lock_state);
    if (buff_pool_seg_add(pool, (uint8_t*)mem, len) != 0) /* segment table full */
    {
        buff_pool_lock_exit(pool, lock_state);
        return -ENOSPC;
    }
    seg = (struct buff_pool_free_block*)mem;
    seg->prev = NULL;
    seg->next = NULL;
    seg->size = len - BUFF_POOL_HEAD_SIZE;
    buff_pool_freelist_merge(pool, seg); /* coalesces with adjacent free blocks */
    pool->total_size += len;
    buff_pool_lock_exit(pool, lock_state);
    return 0;
}

void buffer_pool_free(buffer_pool_t* pool, buffer_block_t* block)
{
    uint32_t lock_state;
    if (pool == NULL || block == NULL)
        return;
    buff_pool_lock_enter(pool, &lock_state);
    if (block->from_static)
    {
        /* buffer_block_t.data overwrites the free_block.size offset at
         * allocation time, so restore size from block->capacity before
         * returning the block to the free list. */
        struct buff_pool_free_block* f = (struct buff_pool_free_block*)block->raw;
        f->size = block->capacity;
        buff_pool_freelist_merge(pool, f);
    }
    else
    {
        free(block->raw);
    }
    if (BUFF_POOL_ATOMIC_LOAD(&pool->used_count) > 0u)
        BUFF_POOL_ATOMIC_SUB_FETCH(&pool->used_count, 1u);
    buff_pool_lock_exit(pool, lock_state);
}

buffer_block_t* buffer_pool_alloc_isr(buffer_pool_t* pool, size_t size) { return buffer_pool_alloc(pool, size); }

void buffer_pool_free_isr(buffer_pool_t* pool, buffer_block_t* block) { buffer_pool_free(pool, block); }

/* ── Block content management (dual pointers, ring read/write;
 *    SPSC semantics, head/tail atomic) ── */

size_t buffer_block_used(const buffer_block_t* block)
{
    uint32_t head;
    uint32_t tail;
    if (block == NULL || block->capacity == 0u)
        return 0u;
    head = BUFF_POOL_ATOMIC_LOAD(&block->head);
    tail = BUFF_POOL_ATOMIC_LOAD(&block->tail);
    return (size_t)((head + (uint32_t)block->capacity - tail) % (uint32_t)block->capacity);
}

size_t buffer_block_space(const buffer_block_t* block)
{
    if (block == NULL)
        return 0u;
    return block->capacity - buffer_block_used(block);
}

int buffer_block_write(buffer_block_t* block, const void* src, size_t len, size_t* actual)
{
    const uint8_t* s = (const uint8_t*)src;
    size_t space;
    size_t first;
    uint32_t head;
    if (block == NULL || src == NULL || len == 0u)
    {
        if (actual != NULL)
            *actual = 0u;
        return -EINVAL;
    }
    space = buffer_block_space(block);
    if (len > space)
        len = space;
    if (len == 0u)
    {
        if (actual != NULL)
            *actual = 0u;
        return 0; /* block full, truncated to 0, still success */
    }
    head = BUFF_POOL_ATOMIC_LOAD(&block->head);
    first = block->capacity - (size_t)head;
    if (first > len)
        first = len;
    memcpy(&block->data[head], s, first);
    if (len > first)
        memcpy(block->data, &s[first], len - first);
    BUFF_POOL_ATOMIC_STORE(&block->head, (head + (uint32_t)len) % (uint32_t)block->capacity);
    if (actual != NULL)
        *actual = len;
    return 0;
}

int buffer_block_read(buffer_block_t* block, void* dst, size_t len, size_t* actual)
{
    uint8_t* d = (uint8_t*)dst;
    size_t used;
    size_t first;
    uint32_t tail;
    if (block == NULL || dst == NULL || len == 0u)
    {
        if (actual != NULL)
            *actual = 0u;
        return -EINVAL;
    }
    used = buffer_block_used(block);
    if (len > used)
        len = used;
    if (len == 0u)
    {
        if (actual != NULL)
            *actual = 0u;
        return 0; /* empty, truncated to 0, still success */
    }
    tail = BUFF_POOL_ATOMIC_LOAD(&block->tail);
    first = block->capacity - (size_t)tail;
    if (first > len)
        first = len;
    memcpy(d, &block->data[tail], first);
    if (len > first)
        memcpy(&d[first], block->data, len - first);
    BUFF_POOL_ATOMIC_STORE(&block->tail, (tail + (uint32_t)len) % (uint32_t)block->capacity);
    if (actual != NULL)
        *actual = len;
    return 0;
}

void buffer_block_reset(buffer_block_t* block)
{
    if (block != NULL)
    {
        BUFF_POOL_ATOMIC_STORE(&block->head, 0u);
        BUFF_POOL_ATOMIC_STORE(&block->tail, 0u);
    }
}

/* ── Statistics / diagnostics ── */

size_t buffer_pool_size(const buffer_pool_t* pool)
{
    if (pool == NULL)
        return 0u;
    return pool->total_size;
}

size_t buffer_pool_free_space(const buffer_pool_t* pool)
{
    const struct buff_pool_free_block* it;
    size_t total = 0u;
    uint32_t lock_state;
    if (pool == NULL || pool->pool_base == NULL)
        return 0u;
    /* Walk the free list under the lock to avoid concurrent mutation */
    buff_pool_lock_enter((buffer_pool_t*)pool, &lock_state);
    for (it = pool->free_list; it != NULL; it = it->next)
        total += BUFF_POOL_HEAD_SIZE + it->size;
    buff_pool_lock_exit((buffer_pool_t*)pool, lock_state);
    return total;
}

uint32_t buffer_pool_used(const buffer_pool_t* pool)
{
    if (pool == NULL)
        return 0u;
    return BUFF_POOL_ATOMIC_LOAD(&pool->used_count);
}

uint32_t buffer_pool_peak(const buffer_pool_t* pool)
{
    if (pool == NULL)
        return 0u;
    return BUFF_POOL_ATOMIC_LOAD(&pool->peak);
}

void buffer_pool_reset_peak(buffer_pool_t* pool)
{
    if (pool != NULL)
        BUFF_POOL_ATOMIC_STORE(&pool->peak, BUFF_POOL_ATOMIC_LOAD(&pool->used_count));
}

#endif /* CONFIG_BUFFER_POOL */
