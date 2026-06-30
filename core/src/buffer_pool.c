/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Buffer Pool — 预分配定长缓冲区池实现
 *
 * 位图 + 原子 CAS 实现 O(1) 无锁分配/释放, ISR 安全
 * ARMv6-M 无 LDREX/STREX 退化到关中断原子; 内存 32 字节对齐保证 DMA 安全
 */
#include "buffer_pool.h"
#include "osal.h"
#include "compiler_compat.h"
#include "compiler_compat_poison.h"

/* ── 内部常量 ── */
#define BP_FREE_ALL  0xFFFFFFFFu

/* ═══════════════════════════════════════════════════════════════
 *  原子操作抽象层
 * ═══════════════════════════════════════════════════════════════
 *  ARMv6-M (Cortex-M0/M0+) / ARMv8-M Baseline (Cortex-M23) 匮乏
 *  LDREX/STREX 指令, 使用关中断实现原子操作.
 *  其它架构 (ARMv7-M+, RISC-V) 直接使用 GCC __atomic 内置函数.
 */
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)

__attribute__((always_inline))
static inline uint32_t bp_critical_enter(void)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, PRIMASK\n\tcpsid i" : "=r"(primask) :: "memory");
    return primask;
}

__attribute__((always_inline))
static inline void bp_critical_exit(uint32_t primask)
{
    __asm__ volatile("msr PRIMASK, %0" :: "r"(primask) : "memory");
}

#define BP_CAS(addr, exp, des) ({ \
    uint32_t _p = bp_critical_enter(); \
    bool _m = (*(addr) == *(exp));      \
    if (_m)  *(addr) = (des);          \
    else    *(exp) = *(addr);          \
    bp_critical_exit(_p);              \
    _m;                                \
})

#define BP_ADD_FETCH(addr, val) ({ \
    uint32_t _p = bp_critical_enter(); \
    *(addr) += (val);                   \
    uint32_t _r = *(addr);              \
    bp_critical_exit(_p);              \
    _r;                                \
})

#define BP_SUB_FETCH(addr, val) ({ \
    uint32_t _p = bp_critical_enter(); \
    *(addr) -= (val);                   \
    uint32_t _r = *(addr);              \
    bp_critical_exit(_p);              \
    _r;                                \
})

#define BP_OR(addr, val) do { \
    uint32_t _p = bp_critical_enter(); \
    *(addr) |= (val);                   \
    bp_critical_exit(_p);              \
} while (0)

#define BP_LOAD(addr) (*(addr))
#define BP_STORE(addr, val) (*(addr) = (val))

#else
/* ARMv7-M+, RISC-V: 硬件原子指令 */
#define BP_CAS(addr, exp, des)     __atomic_compare_exchange_n(addr, exp, des, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)
#define BP_ADD_FETCH(addr, val)    __atomic_add_fetch(addr, val, __ATOMIC_RELAXED)
#define BP_SUB_FETCH(addr, val)    __atomic_sub_fetch(addr, val, __ATOMIC_RELAXED)
#define BP_OR(addr, val)           (void)__atomic_fetch_or(addr, val, __ATOMIC_RELEASE)
#define BP_LOAD(addr)              __atomic_load_n(addr, __ATOMIC_RELAXED)
#define BP_STORE(addr, val)        __atomic_store_n(addr, val, __ATOMIC_RELAXED)
#endif

/* ── 缓存行大小 (GCC 内置) ── */
#ifndef BP_CACHE_LINE
#if defined(__GNUC__) && defined(__CHAR_BIT__) && defined(__SIZEOF_POINTER__)
#define BP_CACHE_LINE  (__CHAR_BIT__ * __SIZEOF_POINTER__)
#else
#define BP_CACHE_LINE  32
#endif
#endif

#define BP_IS_POW2(n)  ((n) & ((n) - 1))
#define BP_ALIGN_UP(n, a)  (((size_t)(n) + (size_t)(a) - 1) & ~((size_t)(a) - 1))

/* 根据对齐要求计算实际 buf_size */
static inline size_t align_buf_size(size_t size, bp_align_t align)
{
    size_t alignment = 1;
    switch (align)
    {
    case BP_ALIGN_DMA:   alignment = 32; break;
    case BP_ALIGN_CACHE: alignment = (size_t)BP_CACHE_LINE; break;
    default:             alignment = 1;  break;
    }
    return (alignment <= 1) ? size : BP_ALIGN_UP(size, alignment);
}

/* ── Pool 控制块 ── */
struct bp_pool
{
    const char* name;
    uint8_t*    pool_mem;       /* 缓冲区内存基址 (32 字节对齐, DMA 安全) */
    void*       pool_mem_raw;   /* 原始分配地址 (os_free 用) */
    size_t      buf_size;       /* 对齐后的 buf 大小 */
    uint32_t    buf_count;      /* buffer 数量 */
    uint32_t    free_mask;      /* 位图: 1=空闲, 0=已分配 */
    uint32_t    used;           /* 当前已分配数 */
    uint32_t    peak;           /* 峰值已分配数 */
    uint8_t     owned : 1;      /* pool_mem 由本池管理, 需释放 */
};

_Static_assert(BP_MAX_BUFS <= sizeof(uint32_t) * 8,
               "BP_MAX_BUFS exceeds free_mask width");

/* ── 位图操作 ── */

/* 原子查找并分配一个空闲位, 返回位索引; BP_MAX_BUFS 表示失败 */
static uint32_t bitmap_alloc(volatile uint32_t* mask)
{
    uint32_t old, new_mask;
    int bit;

    do
    {
        old = *mask;
        if (old == 0) return BP_MAX_BUFS;
        bit = COMPAT_CTZ(old);  /* 找最低位 1 → 第一个空闲 */
        new_mask = old & ~(1u << bit);
    } while (!BP_CAS(mask, &old, new_mask));
    return (uint32_t)bit;
}

/* 原子释放一个位 */
static void bitmap_free(volatile uint32_t* mask, uint32_t bit)
{
    BP_OR(mask, 1u << bit);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  公共接口
 * ═══════════════════════════════════════════════════════════════════════ */

struct bp_pool* bp_create(const struct bp_config* config)
{
    if (!config || !config->name || config->buf_count == 0 ||
        config->buf_count > BP_MAX_BUFS)
    {
        return NULL;
    }

    size_t real_bs = align_buf_size(config->buf_size, config->align);
    size_t total   = real_bs * config->buf_count;

    /* 分配控制块 */
    struct bp_pool* pool = (struct bp_pool*)osal_calloc(1, sizeof(struct bp_pool));
    if (!pool) return NULL;

    /* 分配或引用缓冲区内存 */
    if (config->use_static && config->static_mem)
    {
        if (config->static_len < total)
        {
            osal_free(pool);
            return NULL;
        }
        __builtin_memset(config->static_mem, 0, config->static_len);
        pool->pool_mem     = (uint8_t*)config->static_mem;
        pool->pool_mem_raw = NULL;
        pool->owned        = 0;
    }
    else
    {
        /* 多分配 31 字节, 向上对齐到 32 字节保证 DMA 安全 */
        void* raw = osal_calloc(1, total + 31);
        if (!raw)
        {
            osal_free(pool);
            return NULL;
        }
        pool->pool_mem    = (uint8_t*)BP_ALIGN_UP((uintptr_t)raw, 32);
        pool->pool_mem_raw = raw;
        pool->owned = 1;
    }

    pool->name      = config->name;
    pool->buf_size  = real_bs;
    pool->buf_count = config->buf_count;
    pool->free_mask = (config->buf_count == 32) ? BP_FREE_ALL
                                                : ((1u << config->buf_count) - 1);
    pool->used      = 0;
    pool->peak      = 0;

    return pool;
}

void* bp_alloc(struct bp_pool* pool)
{
    if (!pool) return NULL;

    uint32_t idx = bitmap_alloc(&pool->free_mask);
    if (idx >= pool->buf_count) return NULL;

    uint32_t u = BP_ADD_FETCH(&pool->used, 1);
    /* 更新峰值 (无锁 CAS) */
    uint32_t p;
    do
    {
        p = pool->peak;
        if (u <= p) break;
    } while (!BP_CAS(&pool->peak, &p, u));

    return pool->pool_mem + idx * pool->buf_size;
}

void* bp_alloc_isr(struct bp_pool* pool)
{
    return bp_alloc(pool);
}

void bp_free(struct bp_pool* pool, void* buf)
{
    if (!pool || !buf) return;

    uintptr_t base = (uintptr_t)pool->pool_mem;
    uintptr_t off  = (uintptr_t)buf - base;
    uint32_t  idx  = (uint32_t)(off / pool->buf_size);

    /* 越界检查: 防止野指针破坏池 */
    if (off >= pool->buf_size * pool->buf_count || off % pool->buf_size != 0)
    {
        return;
    }

    bitmap_free(&pool->free_mask, idx);
    BP_SUB_FETCH(&pool->used, 1);
}

void bp_free_isr(struct bp_pool* pool, void* buf)
{
    bp_free(pool, buf);
}

uint32_t bp_used(const struct bp_pool* pool)
{
    return pool ? BP_LOAD(&((struct bp_pool*)pool)->used) : 0;
}

uint32_t bp_peak(const struct bp_pool* pool)
{
    return pool ? BP_LOAD(&((struct bp_pool*)pool)->peak) : 0;
}

void bp_reset_peak(struct bp_pool* pool)
{
    if (!pool) return;
    uint32_t cur = BP_LOAD(&pool->used);
    BP_STORE(&pool->peak, cur);
}

void bp_destroy(struct bp_pool* pool)
{
    if (!pool) return;
    if (pool->owned)
    {
        osal_free(pool->pool_mem_raw);
    }
    osal_free(pool);
}

