/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Buffer Pool — 预分配定长缓冲区池接口
 *
 * 替代动态 malloc 消除碎片; 适用于 EventBus 零拷贝、DMA、驱动 I/O 队列
 * O(1) 分配/释放 (位图+CLZ), ISR 安全 (原子位操作), 内置峰值追踪
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" 
{
#endif

/* ── Buffer Pool — 预分配定长缓冲区池 ──
 *
 * 替代动态内存分配 (malloc/rt_malloc), 消除碎片.
 * 适用于 EventBus 零拷贝传参、DMA 缓冲区、驱动 I/O 队列等场景.
 *
 * 特性:
 *   - 初始化时一次性分配, 运行时无 malloc
 *   - O(1) 分配/释放 (位图 + CLZ)
 *   - ISR 安全 (原子位操作, 无锁)
 *   - 支持 DMA 对齐
 *   - 内置峰值追踪 (调试/认证用)
 *
 * 用法:
 *   static struct bp_config cfg =
 {
 *       .name = "audio",
 *       .buf_size = 256,
 *       .buf_count = 16,
 *       .align = BP_ALIGN_DMA,
 *   };
 *   struct bp_pool* pool = bp_create(&cfg);
 *   void* buf = bp_alloc(pool);
 *   // ... 使用 buf ...
 *   bp_free(pool, buf);
 */

#define BP_MAX_BUFS  32  /* 单池最大缓冲区数 (受 uint32_t 位图限制) */

typedef enum
{
    BP_ALIGN_NONE  = 0,  /* 自然对齐, 无填充 */
    BP_ALIGN_DMA   = 1,  /* 32 字节对齐 (DMA 通用) */
    BP_ALIGN_CACHE = 2,  /* 缓存行对齐 (由工具链定义) */
} bp_align_t;

struct bp_config
{
    const char* name;        /* 调试标识名 */
    size_t      buf_size;    /* 每个 buffer 的数据区大小 */
    uint32_t    buf_count;   /* buffer 数量 (≤ BP_MAX_BUFS) */
    bp_align_t  align;       /* 对齐要求 */
    bool        use_static;  /* true=使用外部静态内存, false=内部 osal_calloc */
    void*       static_mem;  /* use_static 时, 指向外部内存基址 */
    size_t      static_len;  /* static_mem 总大小 */
};

struct bp_pool;

/* ── 生命周期 ── */
struct bp_pool* bp_create(const struct bp_config* config) COMPAT_WARN_UNUSED_RESULT;
void  bp_destroy(struct bp_pool* pool);

/* ── 分配/释放 ── */
void* bp_alloc(struct bp_pool* pool) COMPAT_WARN_UNUSED_RESULT;
void  bp_free(struct bp_pool* pool, void* buf);

/* ISR 安全版本 (实际与普通版本相同, 原子操作本身 ISR 安全) */
void* bp_alloc_isr(struct bp_pool* pool) COMPAT_WARN_UNUSED_RESULT;
void  bp_free_isr(struct bp_pool* pool, void* buf);

/* ── 统计诊断 ── */
uint32_t bp_used(const struct bp_pool* pool);
uint32_t bp_peak(const struct bp_pool* pool);
void     bp_reset_peak(struct bp_pool* pool);

#ifdef __cplusplus
}
#endif


