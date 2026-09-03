/*
 * SPDX-License-Identifier: Apache-2.0
 * @file buffer_pool.c
 * @brief 缓冲池实现: 空闲链表 + 双指针块分配器
 */

#include "buffer_pool.h"

#include <errno.h>  /* EINVAL */
#include <stdlib.h> /* calloc/free */
#include <string.h>
#define CONFIG_BUFFER_POOL 1 /* 独立构建时强制启用 */
#ifdef CONFIG_BUFFER_POOL

#ifndef CONFIG_BUFFER_POOL_SIZE
#define CONFIG_BUFFER_POOL_SIZE 4096u /* 未外部注入时的默认静态池字节数 */
#endif

#if defined(CONFIG_BUFFER_POOL_DYNAMIC_ONLY)
#define BUFFER_POOL_DYNAMIC_ONLY 1
#else
#define BUFFER_POOL_DYNAMIC_ONLY 0
#endif

/* -------------------------------------------------------------------------- */
/* 原子操作 / 临界区封装 (本模块自包装, 不依赖体系头文件)                       */
/* ARMv6-M (Cortex-M0/M0+) 与 ARMv8-M Baseline (Cortex-M23) 缺少             */
/* LDREX/STREX, 以关中断代替; 其余架构走 GCC __atomic 内建函数。               */
/* -------------------------------------------------------------------------- */
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

/* 无 LDREX/STREX, CAS 以关中断临界区封装 */
static inline int BUFF_POOL_ATOMIC_CAS(volatile uint32_t* p, uint32_t* expected, uint32_t desired)
{
    uint32_t primask = BUFF_POOL_CRITICAL_ENTER();
    int      ok = (*p == *expected);
    if (ok)
        *p = desired;
    else
        *expected = *p;
    BUFF_POOL_CRITICAL_EXIT(primask);
    return ok;
}

#else /* ARMv7-M+ / RISC-V / 其余架构: 硬件原子操作 */

#define BUFF_POOL_ATOMIC_LOAD(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_ADD_FETCH(p, v) __atomic_add_fetch((p), (v), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_SUB_FETCH(p, v) __atomic_sub_fetch((p), (v), __ATOMIC_RELAXED)
#define BUFF_POOL_ATOMIC_CAS(p, e, d) __atomic_compare_exchange_n((p), (e), (d), 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)

/* -------------------------------------------------------------------------- */
/* ISR 临界区原语 (ARMv7-M+: PRIMASK; RISC-V: mie)                            */
/* 仅 _isr 接口使用: ISR 入口绝不在线程持有的池锁上自旋 (否则死锁)。          */
/* RTOS 后端 (FreeRTOS/RT-Thread) 移植时应改用各自的 from-ISR 关中断原语      */
/* (portSET_INTERRUPT_MASK_FROM_ISR 等), 以兼容中断嵌套。                     */
/* -------------------------------------------------------------------------- */

#if defined(__riscv) || defined(__riscv_xlen)

static inline uint32_t BUFF_POOL_ISR_MASK_ENTER(void)
{
    uint32_t mie;
    __asm__ volatile("csrrc %0, mie, %1" : "=r"(mie) : "r"(1u << 3) : "memory"); /* 清 MIE */
    return mie;
}

static inline void BUFF_POOL_ISR_MASK_EXIT(uint32_t prev)
{
    uint32_t restore = prev & (1u << 3);
    if (restore != 0u)
        __asm__ volatile("csrs mie, %0" ::"r"(restore) : "memory");
}

#elif defined(__ARM_ARCH) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_MAIN__)

static inline uint32_t BUFF_POOL_ISR_MASK_ENTER(void)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, PRIMASK\n\tcpsid i" : "=r"(primask)::"memory");
    return primask;
}

static inline void BUFF_POOL_ISR_MASK_EXIT(uint32_t prev) { __asm__ volatile("msr PRIMASK, %0" ::"r"(prev) : "memory"); }

#endif

#endif

/* -------------------------------------------------------------------------- */
/* 对齐与最小块 */
/* -------------------------------------------------------------------------- */
#define BUFF_POOL_ALIGN_SIZE 8u /* 块与数据的对齐粒度 */
#define BUFF_POOL_MIN_BLOCK 16u /* 拆分后余量低于此值则不再拆分 */

#define BUFF_POOL_ALIGN_UP(x, a) (((x) + ((a) - 1u)) & ~((a) - 1u))

/* -------------------------------------------------------------------------- */
/* 空闲块头 (内嵌于池内存; 分配时与已分配块元数据共用头部区域)                */
/* -------------------------------------------------------------------------- */
struct buff_pool_free_block
{
    struct buff_pool_free_block* prev;
    struct buff_pool_free_block* next;
    size_t                       size; /**< 本空闲块的数据区字节数 */
};

/* 统一头部大小: 空闲时为 free_block, 已分配时为 buffer_block_t */
#define BUFF_POOL_HEAD_SIZE                                                                                                                          \
    (BUFF_POOL_ALIGN_UP(                                                                                                                             \
        (sizeof(struct buff_pool_free_block) > sizeof(buffer_block_t) ? sizeof(struct buff_pool_free_block) : sizeof(buffer_block_t)),               \
        BUFF_POOL_ALIGN_SIZE))

#define BUFF_POOL_MAX_SEGS 4u /* 池段最大数量 (初始段 + 扩容段) */

/* 单个池段 (初始池或追加的扩容段) */
struct buff_pool_seg
{
    uint8_t* base;
    size_t   len; /**< 段字节数 */
};

struct buffer_pool
{
    const char*                  name;
    buff_pool_atomic_uint_t      lock;                     /**< 池锁 (自封装的 test-and-set / 关中断) */
    bool                         use_static;               /**< 是否启用静态池 (静态优先模式下有效) */
    uint8_t*                     pool_base;                /**< 初始静态池基址 */
    size_t                       pool_size;                /**< 初始静态池字节数 */
    bool                         pool_owned;               /**< true = 池内存由本池分配 (销毁时释放) */
    struct buff_pool_free_block* free_list;                /**< 空闲链表头 */
    size_t                       total_size;               /**< 所有段的累计大小 */
    struct buff_pool_seg         segs[BUFF_POOL_MAX_SEGS]; /**< 段表, 按长度升序排列 */
    uint32_t                     seg_count;                /**< 已注册的段数量 */
    buff_pool_atomic_uint_t      used_count;               /**< 当前已分配的块数 */
    buff_pool_atomic_uint_t      peak;                     /**< 峰值使用量 */
};

/* 注册一个段, 保持段表按长度升序排列。
 * 成功返回 0, 段表已满返回 -1。 */
static int buff_pool_seg_add(struct buffer_pool* pool, uint8_t* base, size_t len)
{
    uint32_t insert_index, shift_index;
    if (pool->seg_count >= BUFF_POOL_MAX_SEGS)
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

/* -------------------------------------------------------------------------- */
/* 池临界区 (锁) */
/* -------------------------------------------------------------------------- */

static void buff_pool_lock_enter(buffer_pool_t* pool, uint32_t* state)
{
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    /* 关中断即临界区 (单核安全) */
    (void)pool;
    *state = BUFF_POOL_CRITICAL_ENTER();
#else
    uint32_t expected = 0u;
    (void)state;
    while (!BUFF_POOL_ATOMIC_CAS(&pool->lock, &expected, 1u))
        expected = 0u; /* 自旋等待 */
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

/* -------------------------------------------------------------------------- */
/* 空闲链表操作 (调用方须持有池锁) */
/* -------------------------------------------------------------------------- */

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

/* 将块归还链表, 与地址相邻的空闲块合并。
 * 策略: 将相邻空闲块吸收进 blk (并从链表移除), 最后统一插入 blk。
 * 遍历期间 blk 不在链表中, 不会出现自链接。 */
static void buff_pool_freelist_merge(struct buffer_pool* pool, struct buff_pool_free_block* blk)
{
    struct buff_pool_free_block* it = pool->free_list;
    blk->prev = NULL;
    blk->next = NULL;
    while (it != NULL)
    {
        struct buff_pool_free_block* next = it->next;
        uint8_t*                     it_end = (uint8_t*)it + BUFF_POOL_HEAD_SIZE + it->size;
        uint8_t*                     blk_end = (uint8_t*)blk + BUFF_POOL_HEAD_SIZE + blk->size;

        if (it_end == (uint8_t*)blk) /* it 紧邻 blk 前方 */
        {
            it->size += BUFF_POOL_HEAD_SIZE + blk->size;
            buff_pool_freelist_remove(pool, it);
            blk = it; /* blk 向前扩展覆盖 it; it 已脱离链表 */
        }
        else if (blk_end == (uint8_t*)it) /* blk 紧邻 it 前方 */
        {
            blk->size += BUFF_POOL_HEAD_SIZE + it->size;
            buff_pool_freelist_remove(pool, it);
        }
        it = next;
    }
    buff_pool_freelist_push(pool, blk);
}

/* 首次适配: 查找数据区 >= size 的空闲块; 拆分并保留余量。
 * 若 seg != NULL, 仅考虑位于该段内的空闲块 (地址范围检查)。
 * 选中的块从链表移除并交给调用方; 拆分余量留在原位置。 */
static struct buff_pool_free_block* buff_pool_freelist_alloc(struct buffer_pool* pool, size_t size, const struct buff_pool_seg* seg)
{
    struct buff_pool_free_block* it = pool->free_list;
    uint8_t*                     s_base = seg ? seg->base : NULL;
    uint8_t*                     s_end = seg ? seg->base + seg->len : NULL;
    size = BUFF_POOL_ALIGN_UP(size, BUFF_POOL_ALIGN_SIZE);
    while (it != NULL)
    {
        struct buff_pool_free_block* next = it->next;
        if (seg != NULL)
        {
            if ((uint8_t*)it < s_base || (uint8_t*)it >= s_end)
            {
                it = next; /* 块不在本段内 */
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
                buff_pool_freelist_push(pool, split); /* 余量留在链表 */
                it->size = size;                      /* 缩小选中块 (已脱离链表) */
            }
            else
                buff_pool_freelist_remove(pool, it);
            return it;
        }
        it = next;
    }
    return NULL;
}

/* 单个段内最大的空闲块 (调用方持有锁) */
static size_t buff_pool_seg_max_free(const struct buffer_pool* pool, const struct buff_pool_seg* seg)
{
    const struct buff_pool_free_block* it = pool->free_list;
    uint8_t*                           s_base = seg->base;
    uint8_t*                           s_end = seg->base + seg->len;
    size_t                             maxf = 0u;
    while (it != NULL)
    {
        if ((uint8_t*)it >= s_base && (uint8_t*)it < s_end && it->size > maxf)
            maxf = it->size;
        it = it->next;
    }
    return maxf;
}

/* 所有段中最大的空闲块 */
static size_t buff_pool_total_max_free(const struct buffer_pool* pool)
{
    size_t   maxf = 0u;
    uint32_t seg_index;
    for (seg_index = 0; seg_index < pool->seg_count; seg_index++)
    {
        size_t seg_max_free = buff_pool_seg_max_free(pool, &pool->segs[seg_index]);
        if (seg_max_free > maxf)
            maxf = seg_max_free;
    }
    return maxf;
}

/* 从静态池中切块。
 * 按段从小到大依次尝试 (段表按长度升序):
 * 先耗尽最小段, 不能满足时再向更大的段请求。
 * 无段可满足时返回 NULL。 */
static buffer_block_t* buff_pool_alloc_static_impl(struct buffer_pool* pool, size_t size)
{
    uint32_t                     seg_index;
    struct buff_pool_free_block* free_node;
    buffer_block_t*              blk;
    if (pool->pool_base == NULL || pool->seg_count == 0u)
        return NULL;
    for (seg_index = 0; seg_index < pool->seg_count; seg_index++)
    {
        struct buff_pool_seg* seg = &pool->segs[seg_index];
        if (buff_pool_seg_max_free(pool, seg) < size)
            continue; /* 本段无法满足; 尝试更大的段 */
        free_node = buff_pool_freelist_alloc(pool, size, seg);
        if (free_node == NULL)
            continue; /* 碎片化; 尝试下一段 */
        {
            size_t cap = free_node->size;
            blk = (buffer_block_t*)free_node;
            blk->raw = (uint8_t*)free_node;
            blk->data = (uint8_t*)free_node + BUFF_POOL_HEAD_SIZE;
            blk->capacity = cap;
            BUFF_POOL_ATOMIC_STORE(&blk->head, 0u);
            BUFF_POOL_ATOMIC_STORE(&blk->tail, 0u);
            blk->from_static = true;
        }
        return blk;
    }
    return NULL;
}

/* 动态分配: 块元数据与数据区一起分配 */
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

/* -------------------------------------------------------------------------- */
/* 公开 API */
/* -------------------------------------------------------------------------- */
int buffer_pool_create(const buffer_pool_config_t* config, buffer_pool_t** p_pool)
{
    buffer_pool_t* pool;
    if (config == NULL || p_pool == NULL)
        return BUFF_POOL_ERR_INVAL;
    pool = (buffer_pool_t*)calloc(1u, sizeof(*pool));
    if (pool == NULL)
        return BUFF_POOL_ERR_NOMEM;
    pool->name = config->name;
    BUFF_POOL_ATOMIC_STORE(&pool->lock, 0u);

#if BUFFER_POOL_DYNAMIC_ONLY
    /* 纯动态模式: 屏蔽静态池 */
    pool->use_static = false;
#else
    /* 静态优先模式: 启用静态池 (调用方提供或内部分配) */
    if (config->use_static)
    {
        size_t total = config->static_len;
        if (total == 0u)
            total = CONFIG_BUFFER_POOL_SIZE;
        if (total < BUFF_POOL_HEAD_SIZE + BUFF_POOL_MIN_BLOCK)
        {
            free(pool);
            return BUFF_POOL_ERR_INVAL;
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
                return BUFF_POOL_ERR_NOMEM;
            }
            pool->pool_owned = true;
        }
        pool->pool_size = total;
        pool->total_size = total;
        /* 注册初始段 */
        if (buff_pool_seg_add(pool, pool->pool_base, total) != 0)
        {
            if (pool->pool_owned)
                free(pool->pool_base);
            free(pool);
            return BUFF_POOL_ERR_INVAL;
        }
        /* 整个池作为一个空闲块 */
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
    *p_pool = pool;
    return BUFF_POOL_OK;
}

int buffer_pool_destroy(buffer_pool_t* pool)
{
    if (pool == NULL)
        return BUFF_POOL_ERR_INVAL;
    if (pool->pool_owned && pool->pool_base != NULL)
        free(pool->pool_base);
    free(pool);
    return BUFF_POOL_OK;
}

/* 分配成功后的记账 (调用方持有锁) */
static void buff_pool_count_alloc(buffer_pool_t* pool, buffer_block_t* blk)
{
    uint32_t used = BUFF_POOL_ATOMIC_ADD_FETCH(&pool->used_count, 1u);
    uint32_t prev_peak;
    blk->pool = pool;
    do
    {
        prev_peak = BUFF_POOL_ATOMIC_LOAD(&pool->peak);
        if (used <= prev_peak)
            break;
    } while (!BUFF_POOL_ATOMIC_CAS(&pool->peak, &prev_peak, used));
}

int buffer_pool_alloc_static(buffer_pool_t* pool, size_t size, buffer_block_t** p_block)
{
    buffer_block_t* blk;
    uint32_t        lock_state;
    if (pool == NULL || size == 0u || p_block == NULL)
        return BUFF_POOL_ERR_INVAL;
    buff_pool_lock_enter(pool, &lock_state);
    blk = buff_pool_alloc_static_impl(pool, size);
    if (blk != NULL)
        buff_pool_count_alloc(pool, blk);
    buff_pool_lock_exit(pool, lock_state);
    if (blk == NULL)
        return BUFF_POOL_ERR_NOMEM;
    *p_block = blk;
    return BUFF_POOL_OK;
}

int buffer_pool_alloc(buffer_pool_t* pool, size_t size, buffer_block_t** p_block)
{
    buffer_block_t* blk;
    uint32_t        lock_state;
    if (pool == NULL || size == 0u || p_block == NULL)
        return BUFF_POOL_ERR_INVAL;
    buff_pool_lock_enter(pool, &lock_state);
    /* 阈值: 若请求超过任意段中最大连续空闲, 静态池无法满足 — 直接走动态。 */
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
    if (blk == NULL)
        return BUFF_POOL_ERR_NOMEM;
    *p_block = blk;
    return BUFF_POOL_OK;
}

int buffer_pool_expand(buffer_pool_t* pool, void* mem, size_t len)
{
    struct buff_pool_free_block* seg;
    uint32_t                     lock_state;
    if (pool == NULL || mem == NULL || len < BUFF_POOL_HEAD_SIZE + BUFF_POOL_MIN_BLOCK)
        return BUFF_POOL_ERR_INVAL;
    buff_pool_lock_enter(pool, &lock_state);
    if (buff_pool_seg_add(pool, (uint8_t*)mem, len) != 0) /* 段表已满 */
    {
        buff_pool_lock_exit(pool, lock_state);
        return BUFF_POOL_ERR_NOSPC;
    }
    seg = (struct buff_pool_free_block*)mem;
    seg->prev = NULL;
    seg->next = NULL;
    seg->size = len - BUFF_POOL_HEAD_SIZE;
    buff_pool_freelist_merge(pool, seg); /* 与相邻空闲块合并 */
    pool->total_size += len;
    buff_pool_lock_exit(pool, lock_state);
    return BUFF_POOL_OK;
}

int buffer_pool_free(buffer_pool_t* pool, buffer_block_t* block)
{
    uint32_t lock_state;
    if (pool == NULL || block == NULL)
        return BUFF_POOL_ERR_INVAL;
    buff_pool_lock_enter(pool, &lock_state);
    if (block->from_static)
    {
        /* 分配时 buffer_block_t.data 覆盖了 free_block.size 的偏移,
         * 归还空闲链表前需从 block->capacity 恢复 size。 */
        struct buff_pool_free_block* free_node = (struct buff_pool_free_block*)block->raw;
        free_node->size = block->capacity;
        buff_pool_freelist_merge(pool, free_node);
    }
    else
    {
        free(block->raw);
    }
    if (BUFF_POOL_ATOMIC_LOAD(&pool->used_count) > 0u)
        BUFF_POOL_ATOMIC_SUB_FETCH(&pool->used_count, 1u);
    buff_pool_lock_exit(pool, lock_state);
    return BUFF_POOL_OK;
}

/* -------------------------------------------------------------------------- */
/* ISR 安全接口                                                               */
/* ISR 上下文严禁触碰原生堆: libc malloc/free 不是 ISR 安全的。               */
/* 静态块可进; 需要回退动态分配 (calloc) 或释放动态块 (free) 时直接拒绝返回。 */
/* -------------------------------------------------------------------------- */

int buffer_pool_alloc_isr(buffer_pool_t* pool, size_t size, buffer_block_t** p_block)
{
    buffer_block_t* blk;
    uint32_t        lock_state;
    (void)lock_state; /* 未知架构构建下未使用 */
    if (pool == NULL || size == 0u || p_block == NULL)
        return BUFF_POOL_ERR_INVAL;
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    /* 线程路径本就关中断, 直接复用同一临界区 */
    buff_pool_lock_enter(pool, &lock_state);
#elif defined(BUFF_POOL_ISR_MASK_ENTER)
    /* ARMv7-M+ / RISC-V: 关中断代替 CAS 自旋锁 —
     * ISR 里在被抢占线程持有的锁上自旋会死锁 */
    lock_state = BUFF_POOL_ISR_MASK_ENTER();
#else
    /* 未知架构: 无已验证的 ISR 安全锁原语, 拒绝 */
    return BUFF_POOL_ERR_INVAL;
#endif
    /* 仅静态池 — ISR 中绝不回退 calloc */
    blk = buff_pool_alloc_static_impl(pool, size);
    if (blk != NULL)
        buff_pool_count_alloc(pool, blk);
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    buff_pool_lock_exit(pool, lock_state);
#elif defined(BUFF_POOL_ISR_MASK_ENTER)
    BUFF_POOL_ISR_MASK_EXIT(lock_state);
#endif
    if (blk == NULL)
        return BUFF_POOL_ERR_NOMEM; /* 静态池无法满足; 不像 alloc 那样回退动态 */
    *p_block = blk;
    return BUFF_POOL_OK;
}

int buffer_pool_free_isr(buffer_pool_t* pool, buffer_block_t* block)
{
    uint32_t lock_state;
    (void)lock_state; /* 未知架构构建下未使用 */
    if (pool == NULL || block == NULL)
        return BUFF_POOL_ERR_INVAL;
    if (!block->from_static)
        return BUFF_POOL_ERR_INVAL;
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    buff_pool_lock_enter(pool, &lock_state);
#elif defined(BUFF_POOL_ISR_MASK_ENTER)
    lock_state = BUFF_POOL_ISR_MASK_ENTER();
#else
    return BUFF_POOL_ERR_INVAL; /* 未知架构: 无已验证的 ISR 安全锁原语 */
#endif
    /* 分配时 buffer_block_t.data 覆盖了 free_block.size 偏移, 归还前从 capacity 恢复 */
    {
        struct buff_pool_free_block* free_node = (struct buff_pool_free_block*)block->raw;
        free_node->size = block->capacity;
        buff_pool_freelist_merge(pool, free_node);
    }
    if (BUFF_POOL_ATOMIC_LOAD(&pool->used_count) > 0u)
        BUFF_POOL_ATOMIC_SUB_FETCH(&pool->used_count, 1u);
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    buff_pool_lock_exit(pool, lock_state);
#elif defined(BUFF_POOL_ISR_MASK_ENTER)
    BUFF_POOL_ISR_MASK_EXIT(lock_state);
#endif
    return BUFF_POOL_OK;
}

/* -------------------------------------------------------------------------- */
/* 块内容管理 (双指针, 环形读写; head/tail 原子操作, SPSC 语义)              */
/* -------------------------------------------------------------------------- */

int buffer_block_used(const buffer_block_t* block, size_t* p_used)
{
    uint32_t head;
    uint32_t tail;
    if (block == NULL || p_used == NULL)
        return BUFF_POOL_ERR_INVAL;
    if (block->capacity == 0u)
    {
        *p_used = 0u;
        return BUFF_POOL_OK;
    }
    head = BUFF_POOL_ATOMIC_LOAD(&block->head);
    tail = BUFF_POOL_ATOMIC_LOAD(&block->tail);
    *p_used = (size_t)((head + (uint32_t)block->capacity - tail) % (uint32_t)block->capacity);
    return BUFF_POOL_OK;
}

int buffer_block_space(const buffer_block_t* block, size_t* p_space)
{
    size_t used = 0u;
    int    ret;
    if (block == NULL || p_space == NULL)
        return BUFF_POOL_ERR_INVAL;
    ret = buffer_block_used(block, &used);
    if (ret != BUFF_POOL_OK)
        return ret;
    *p_space = block->capacity - used;
    return BUFF_POOL_OK;
}

int buffer_block_write(buffer_block_t* block, const void* src, size_t len, size_t* actual)
{
    const uint8_t* src_bytes = (const uint8_t*)src;
    size_t         space = 0u;
    size_t         first;
    uint32_t       head;
    int            ret;
    if (actual != NULL)
        *actual = 0u;
    if (block == NULL || src == NULL || len == 0u)
        return BUFF_POOL_ERR_INVAL;
    ret = buffer_block_space(block, &space);
    if (ret != BUFF_POOL_OK)
        return ret;
    if (space == 0u)
        return BUFF_POOL_ERR_NOSPC; /* 块已满, 未写入 */
    if (len > space)
        len = space;
    head = BUFF_POOL_ATOMIC_LOAD(&block->head);
    first = block->capacity - (size_t)head;
    if (first > len)
        first = len;
    memcpy(&block->data[head], src_bytes, first);
    if (len > first)
        memcpy(block->data, &src_bytes[first], len - first);
    BUFF_POOL_ATOMIC_STORE(&block->head, (head + (uint32_t)len) % (uint32_t)block->capacity);
    if (actual != NULL)
        *actual = len;
    return BUFF_POOL_OK;
}

int buffer_block_read(buffer_block_t* block, void* dst, size_t len, size_t* actual)
{
    uint8_t* dst_bytes = (uint8_t*)dst;
    size_t   used = 0u;
    size_t   first;
    uint32_t tail;
    int      ret;
    if (actual != NULL)
        *actual = 0u;
    if (block == NULL || dst == NULL || len == 0u)
        return BUFF_POOL_ERR_INVAL;
    ret = buffer_block_used(block, &used);
    if (ret != BUFF_POOL_OK)
        return ret;
    if (used == 0u)
        return BUFF_POOL_ERR_EMPTY; /* 块为空, 无可读数据 */
    if (len > used)
        len = used;
    tail = BUFF_POOL_ATOMIC_LOAD(&block->tail);
    first = block->capacity - (size_t)tail;
    if (first > len)
        first = len;
    memcpy(dst_bytes, &block->data[tail], first);
    if (len > first)
        memcpy(&dst_bytes[first], block->data, len - first);
    BUFF_POOL_ATOMIC_STORE(&block->tail, (tail + (uint32_t)len) % (uint32_t)block->capacity);
    if (actual != NULL)
        *actual = len;
    return BUFF_POOL_OK;
}

int buffer_block_reset(buffer_block_t* block)
{
    if (block == NULL)
        return BUFF_POOL_ERR_INVAL;
    BUFF_POOL_ATOMIC_STORE(&block->head, 0u);
    BUFF_POOL_ATOMIC_STORE(&block->tail, 0u);
    return BUFF_POOL_OK;
}

/* -------------------------------------------------------------------------- */
/* 统计 / 诊断 */
/* -------------------------------------------------------------------------- */

int buffer_pool_size(const buffer_pool_t* pool, size_t* p_size)
{
    if (pool == NULL || p_size == NULL)
        return BUFF_POOL_ERR_INVAL;
    *p_size = pool->total_size;
    return BUFF_POOL_OK;
}

int buffer_pool_free_space(const buffer_pool_t* pool, size_t* p_free)
{
    const struct buff_pool_free_block* it;
    size_t                             total = 0u;
    uint32_t                           lock_state;
    if (pool == NULL || p_free == NULL)
        return BUFF_POOL_ERR_INVAL;
    if (pool->pool_base == NULL)
    {
        *p_free = 0u;
        return BUFF_POOL_OK;
    }
    /* 持锁遍历空闲链表, 避免并发修改 */
    buff_pool_lock_enter((buffer_pool_t*)pool, &lock_state);
    for (it = pool->free_list; it != NULL; it = it->next)
        total += BUFF_POOL_HEAD_SIZE + it->size;
    buff_pool_lock_exit((buffer_pool_t*)pool, lock_state);
    *p_free = total;
    return BUFF_POOL_OK;
}

int buffer_pool_used(const buffer_pool_t* pool, uint32_t* p_used)
{
    if (pool == NULL || p_used == NULL)
        return BUFF_POOL_ERR_INVAL;
    *p_used = BUFF_POOL_ATOMIC_LOAD(&pool->used_count);
    return BUFF_POOL_OK;
}

int buffer_pool_peak(const buffer_pool_t* pool, uint32_t* p_peak)
{
    if (pool == NULL || p_peak == NULL)
        return BUFF_POOL_ERR_INVAL;
    *p_peak = BUFF_POOL_ATOMIC_LOAD(&pool->peak);
    return BUFF_POOL_OK;
}

int buffer_pool_reset_peak(buffer_pool_t* pool)
{
    if (pool == NULL)
        return BUFF_POOL_ERR_INVAL;
    BUFF_POOL_ATOMIC_STORE(&pool->peak, BUFF_POOL_ATOMIC_LOAD(&pool->used_count));
    return BUFF_POOL_OK;
}

#endif /* CONFIG_BUFFER_POOL */
