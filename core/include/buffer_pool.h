#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
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
 *   static bp_config_t cfg = {
 *       .name = "audio",
 *       .buf_size = 256,
 *       .buf_count = 16,
 *       .align = BP_ALIGN_DMA,
 *   };
 *   bp_t* pool = bp_create(&cfg);
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

typedef struct
{
    const char* name;        /* 调试标识名 */
    size_t      buf_size;    /* 每个 buffer 的数据区大小 */
    uint32_t    buf_count;   /* buffer 数量 (≤ BP_MAX_BUFS) */
    bp_align_t  align;       /* 对齐要求 */
    bool        use_static;  /* true=使用外部静态内存, false=内部 osal_calloc */
    void*       static_mem;  /* use_static 时, 指向外部内存基址 */
    size_t      static_len;  /* static_mem 总大小 */
} bp_config_t;

typedef struct bp_pool bp_t;

/* ── 生命周期 ── */
bp_t* bp_create(const bp_config_t* config);
void  bp_destroy(bp_t* pool);

/* ── 分配/释放 ── */
void* bp_alloc(bp_t* pool);
void  bp_free(bp_t* pool, void* buf);

/* ISR 安全版本 (实际与普通版本相同, 原子操作本身 ISR 安全) */
void* bp_alloc_isr(bp_t* pool);
void  bp_free_isr(bp_t* pool, void* buf);

/* ── 统计诊断 ── */
uint32_t bp_used(const bp_t* pool);
uint32_t bp_peak(const bp_t* pool);
void     bp_reset_peak(bp_t* pool);

#ifdef __cplusplus
}
#endif
