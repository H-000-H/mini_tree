/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file buffer_pool.h
 *@brief buffer pool 头文件
 *@author H-000-H
 *@details
 *   Buffer Pool — 预分配定长缓冲区池接口
 *   替代动态 malloc 消除碎片; 适用于 EventBus 零拷贝、DMA、驱动 I/O 队列
 *   O(1) 分配/释放 (位图+CLZ), ISR 安全 (原子位操作), 内置峰值追踪
 */

#pragma once

#include "compiler_compat.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define BP_MAX_BUFS 32 /* 单池最大缓冲区数 (受 uint32_t 位图限制) */

    typedef enum
    {
        BP_ALIGN_NONE = 0, /* 自然对齐, 无填充 */
        BP_ALIGN_DMA = 1, /* 32 字节对齐 (DMA 通用) */
        BP_ALIGN_CACHE = 2, /* 缓存行对齐 (由工具链定义) */
    } bp_align_t;

    struct bp_config
    {
        const char* name; /**< 调试标识名 */
        size_t buf_size; /**< 每个 buffer 的数据区大小 */
        uint32_t buf_count; /**< buffer 数量 (≤ BP_MAX_BUFS) */
        bp_align_t align; /**< 对齐要求 */
        bool use_static; /**< true=使用外部静态内存, false=内部 osal_calloc */
        void* static_mem; /**< use_static 时, 指向外部内存基址 */
        size_t static_len; /**< static_mem 总大小 */
    };

    struct bp_pool;

    /* ── 生命周期 ── */
    /**
     * @brief 创建定长缓冲区池 (初始化时一次性分配, 运行时零 malloc)
     * @param[in] config 池配置 (name/buf_size/buf_count/align/静态内存)
     * @return 池对象指针; 配置非法或资源不足返回 NULL
     */
    struct bp_pool* bp_create(const struct bp_config* config) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 销毁缓冲区池并释放资源
     * @param[in] pool 池对象指针 (可为 NULL)
     */
    void bp_destroy(struct bp_pool* pool);

    /* ── 分配/释放 ── */
    /**
     * @brief 从池中分配一个缓冲区 (O(1) 位图+CLZ, ISR 安全)
     * @param[in] pool 池对象指针
     * @return 缓冲区指针; 池满返回 NULL
     */
    void* bp_alloc(struct bp_pool* pool) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 归还缓冲区到池
     * @param[in] pool 池对象指针
     * @param[in] buf 待释放缓冲区指针 (须由本池 bp_alloc 分配)
     */
    void bp_free(struct bp_pool* pool, void* buf);

    /* ISR 安全版本 (实际与普通版本相同, 原子操作本身 ISR 安全) */
    /**
     * @brief ISR 上下文分配缓冲区 (同 bp_alloc, 原子操作天然 ISR 安全)
     * @param[in] pool 池对象指针
     * @return 缓冲区指针; 池满返回 NULL
     */
    void* bp_alloc_isr(struct bp_pool* pool) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief ISR 上下文归还缓冲区 (同 bp_free)
     * @param[in] pool 池对象指针
     * @param[in] buf 待释放缓冲区指针
     */
    void bp_free_isr(struct bp_pool* pool, void* buf);

    /* ── 统计诊断 ── */
    /**
     * @brief 查询当前已分配缓冲区数
     * @param[in] pool 池对象指针
     * @return 已分配数量
     */
    uint32_t bp_used(const struct bp_pool* pool);
    /**
     * @brief 查询历史峰值占用 (调试/认证用)
     * @param[in] pool 池对象指针
     * @return 峰值占用数量
     */
    uint32_t bp_peak(const struct bp_pool* pool);
    /**
     * @brief 重置峰值计数器
     * @param[in] pool 池对象指针
     */
    void bp_reset_peak(struct bp_pool* pool);

#ifdef __cplusplus
}
#endif
