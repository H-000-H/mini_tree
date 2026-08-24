/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file buffer_pool.h
 *@brief buffer pool 头文件
 *@author H-000-H
 *@details
 *   Buffer Pool — 空闲链表 + 双指针块分配器 (独立模块, 仅依赖 C 标准库)
 *   从静态池按调用方指定大小切块 (空闲链表, 双向), 每块内部用双指针
 *   (head/tail) 管理内容。原子原语内部自包装, 不依赖任何体系头文件。
 *   两种模式 (编译期宏, 由宿主构建注入):
 *     - BUFFER_POOL_STATIC_THEN_DYNAMIC (默认): 静态池优先, 用尽回退动态
 *     - BUFFER_POOL_DYNAMIC_ONLY: 静态池屏蔽, 全部走动态分配
 *   门控宏 CONFIG_BUFFER_POOL (未定义时默认开启编译)。
 */

#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ENODATA
#define ENODATA 61 /**< 部分 libc (newlib 等) 缺少 ENODATA, 补齐 Linux 取值 */
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define BUFF_POOL_OK 0 /**< 成功 */
#define BUFF_POOL_ERR_INVAL (-EINVAL) /**< 入参非法 (句柄/指针为 NULL、大小为 0、池段过小) */
#define BUFF_POOL_ERR_NOMEM (-ENOMEM) /**< 内存不足 (calloc 失败 / 静态池无法满足) */
#define BUFF_POOL_ERR_NOSPC (-ENOSPC) /**< 无剩余空间 (块写满 / 扩容段表已满) */
#define BUFF_POOL_ERR_EMPTY (-ENODATA) /**< 块内无可读数据 */

    /* -------------------------------------------------------------------------- */
    /* Buffer Pool — 空闲链表 + 双指针块分配器 */
    /* 空闲链表(双向)管理"从内存池分配多少": 静态池被切成大小不一的块, */
    /* 每块头部内嵌块元数据, 分配/释放维护链表, 相邻块自动合并. */
    /* 双指针管理"每块内容怎么用": 每个分配出来的小内存池内部有 head(写) */
    /* 与 tail(读) 双指针, 构成环形读写缓冲, 用满时写入被截断. */
    /* 编译期模式: */
    /* - BUFFER_POOL_STATIC_THEN_DYNAMIC (默认): 静态池优先, 用尽回退动态 */
    /* - BUFFER_POOL_DYNAMIC_ONLY: 静态池屏蔽, 全部走动态分配 */
    /* 用法: */
    /* static uint8_t s_pool_mem[4096] _Alignas(8); */
    /* static struct buffer_pool_config cfg = */
    /* { */
    /* .name = "net", */
    /* .use_static = true, */
    /* .static_mem = s_pool_mem, */
    /* .static_len = sizeof(s_pool_mem), */
    /* }; */
    /* buffer_pool_t* pool = NULL; */
    /* buffer_block_t* blk = NULL; */
    /* buffer_pool_create(&cfg, &pool); */
    /* buffer_pool_alloc(pool, 1024, &blk); */
    /* size_t n = 0; */
    /* buffer_block_write(blk, data, len, &n); // 双指针写, n=实际写入 */
    /* buffer_block_read(blk, dst, sizeof dst, &n); // 双指针读 */
    /* buffer_pool_free(pool, blk); */
    /* -------------------------------------------------------------------------- */

    /* 原子类型 (本模块自定; 原子操作由实现文件内部自包装 BUFF_POOL_*) */
    typedef volatile uint32_t buff_pool_atomic_uint_t;

    /* 已分配块: 自带双指针 (head/tail) 的环形读写缓冲 (head/tail 原子) */
    typedef struct buffer_block
    {
        struct buffer_pool* pool; /**< 归属池 */
        uint8_t* raw; /**< 池内原地址或 calloc 原始指针 (释放用) */
        uint8_t* data; /**< 数据区起始 (raw 之后) */
        size_t capacity; /**< 数据区容量 (字节) */
        buff_pool_atomic_uint_t head; /**< 双指针: 写指针 */
        buff_pool_atomic_uint_t tail; /**< 双指针: 读指针 */
        bool from_static; /**< true=来自静态池, false=动态分配 */
    } buffer_block_t;

    typedef struct buffer_pool_config
    {
        const char* name; /**< 调试标识名 */
        bool use_static; /**< 是否启用静态池 (全动态模式下被忽略) */
        void* static_mem; /**< 静态池内存基址; NULL 时按默认池大小内部分配 */
        size_t static_len; /**< 静态池字节数; 0 时取默认池大小 (4096) */
    } buffer_pool_config_t;

    typedef struct buffer_pool buffer_pool_t;

    /* -------------------------------------------------------------------------- */
    /* 生命周期 */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 创建 buffer pool (空闲链表 + 双指针块分配器)
     * @param[in] config 池配置 (name/use_static/static_mem/static_len)
     * @param[out] p_pool 回传池对象指针 (仅成功时写入)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 配置非法; BUFF_POOL_ERR_NOMEM 资源不足
     */
    int buffer_pool_create(const buffer_pool_config_t* config, buffer_pool_t** p_pool);
    /**
     * @brief 销毁 buffer pool 并释放资源
     * @param[in] pool 池对象指针 (不得为 NULL)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法
     */
    int buffer_pool_destroy(buffer_pool_t* pool);

    /* -------------------------------------------------------------------------- */
    /* 分配/释放 */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 从池分配一块指定大小的缓冲 (静态优先, 用尽回退动态; 全动态模式直接动态)
     * @param[in] pool 池对象指针
     * @param[in] size 请求的数据区字节数 (调用方指定, 不写死)
     * @param[out] p_block 回传块对象指针 (仅成功时写入)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法; BUFF_POOL_ERR_NOMEM 分配失败
     */
    int buffer_pool_alloc(buffer_pool_t* pool, size_t size, buffer_block_t** p_block);
    /**
     * @brief 仅从静态池分配 (绝不回退动态)
     * @param[in] pool 池对象指针
     * @param[in] size 请求的数据区字节数
     * @param[out] p_block 回传块对象指针 (仅成功时写入)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法; BUFF_POOL_ERR_NOMEM 静态池无法满足
     * (调用方可改用 buffer_pool_expand 扩容)
     * @note 与 buffer_pool_alloc 不同, 本函数绝不走 calloc。
     */
    int buffer_pool_alloc_static(buffer_pool_t* pool, size_t size, buffer_block_t** p_block);
    /**
     * @brief 向静态池追加一段内存 (运行期扩容)
     * @param[in] pool 池对象指针
     * @param[in] mem 新段基址 (不得与池内存/任何块重叠; 调用方保证该内存独占)
     * @param[in] len 新段字节数 (>= 块头 + 最小块)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法; BUFF_POOL_ERR_NOSPC 段表已满
     * @note 已有块永不移动, 无指针失效风险; 新段并入空闲链表,
     *       与相邻空闲块自动合并。绝不 realloc/resize 原内存。
     */
    int buffer_pool_expand(buffer_pool_t* pool, void* mem, size_t len);
    /**
     * @brief 归还块到池 (静态块插回空闲链表并合并相邻, 动态块 free)
     * @param[in] pool 池对象指针
     * @param[in] block 待释放块 (须由本池 buffer_pool_alloc 分配)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法
     */
    int buffer_pool_free(buffer_pool_t* pool, buffer_block_t* block);

    /* ISR 安全版本 (仅限静态池; ISR 上下文严禁触碰原生堆,                */
    /* libc malloc/free 不是 ISR 安全的。临界区用关中断实现,               */
    /* 绝不用 CAS 自旋锁, 不会在被抢占线程持锁时死锁)                    */
    /**
     * @brief ISR 上下文分配块 (仅静态池, 无动态回退)
     * @param[in] pool 池对象指针
     * @param[in] size 请求字节数
     * @param[out] p_block 回传块对象指针 (仅成功时写入)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法/架构不支持;
     *         BUFF_POOL_ERR_NOMEM 静态池无法满足 (与 buffer_pool_alloc 不同,
     *         绝不回退 calloc)
     */
    int buffer_pool_alloc_isr(buffer_pool_t* pool, size_t size, buffer_block_t** p_block);
    /**
     * @brief ISR 上下文归还块 (仅静态块)
     * @param[in] pool 池对象指针
     * @param[in] block 待释放块 (必须是静态块)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法/动态块/
     *         架构不支持。动态块 (from_static == false) 的 free() 非 ISR 安全,
     *         须在线程上下文用 buffer_pool_free 释放
     */
    int buffer_pool_free_isr(buffer_pool_t* pool, buffer_block_t* block);

    /* -------------------------------------------------------------------------- */
    /* 块内容管理 (双指针, 环形读写; 实际字节数走指针, return 只表成败) */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 向块写入数据 (head 指针推进, 环形回绕; 满则截断)
     * @param[in] block 块对象
     * @param[in] src 源数据
     * @param[in] len 请求写入字节数
     * @param[out] actual 可选: 实际写入字节数 (≤ len), 可传 NULL
     * @return BUFF_POOL_OK 成功 (空间不足时截断); BUFF_POOL_ERR_INVAL 入参非法; BUFF_POOL_ERR_NOSPC
     * 块已满 (*actual = 0)
     * @note 空间不足时按实际剩余空间截断写入, 仍返回 BUFF_POOL_OK;
     *       用 *actual 判断是否写满。
     */
    int buffer_block_write(buffer_block_t* block, const void* src, size_t len, size_t* actual);
    /**
     * @brief 从块读取数据 (tail 指针推进, 环形回绕; 空则截断为 0)
     * @param[in] block 块对象
     * @param[out] dst 目的缓冲区
     * @param[in] len 请求读取字节数
     * @param[out] actual 可选: 实际读取字节数 (≤ len), 可传 NULL
     * @return BUFF_POOL_OK 成功 (数据不足时截断); BUFF_POOL_ERR_INVAL 入参非法; BUFF_POOL_ERR_EMPTY
     * 块内无数据 (*actual = 0)
     * @note 数据不足时按实际可读字节数截断读取, 仍返回 BUFF_POOL_OK;
     *       用 *actual 判断是否读到期望长度。
     */
    int buffer_block_read(buffer_block_t* block, void* dst, size_t len, size_t* actual);
    /**
     * @brief 查询块内已写入但未读取的字节数
     * @param[in] block 块对象
     * @param[out] p_used 回传已占用字节数
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法 (*p_used = 0)
     */
    int buffer_block_used(const buffer_block_t* block, size_t* p_used);
    /**
     * @brief 查询块剩余可写字节数
     * @param[in] block 块对象
     * @param[out] p_space 回传剩余空间 (字节)
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法 (*p_space = 0)
     */
    int buffer_block_space(const buffer_block_t* block, size_t* p_space);
    /**
     * @brief 重置块读写双指针 (清空内容)
     * @param[in] block 块对象
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法
     */
    int buffer_block_reset(buffer_block_t* block);

    /* -------------------------------------------------------------------------- */
    /* 统计诊断 */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 查询池子总大小 (静态池字节数)
     * @param[in] pool 池对象指针
     * @param[out] p_size 回传池子总字节数; 未启用静态池 (全动态模式) 回传 0
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法 (*p_size = 0)
     * @note 与 config 的关系: use_static && static_len>0 → static_len;
     *       use_static && static_len==0 → 默认池大小;
     *       全动态模式 → 0 (静态池屏蔽)
     */
    int buffer_pool_size(const buffer_pool_t* pool, size_t* p_size);
    /**
     * @brief 查询静态池当前剩余可切字节数 (遍历空闲链表求和)
     * @param[in] pool 池对象指针
     * @param[out] p_free 回传剩余字节数; 未启用静态池回传 0
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法 (*p_free = 0)
     * @note 含各空闲块数据区 + 可复用的块头 (拆分后余块重新入链),
     *       与实际最大单块大小相近但不保证连续。
     */
    int buffer_pool_free_space(const buffer_pool_t* pool, size_t* p_free);
    /**
     * @brief 查询当前已分配块数
     * @param[in] pool 池对象指针
     * @param[out] p_used 回传已分配块数
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法 (*p_used = 0)
     */
    int buffer_pool_used(const buffer_pool_t* pool, uint32_t* p_used);
    /**
     * @brief 查询历史峰值占用 (调试/认证用)
     * @param[in] pool 池对象指针
     * @param[out] p_peak 回传峰值占用块数
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法 (*p_peak = 0)
     */
    int buffer_pool_peak(const buffer_pool_t* pool, uint32_t* p_peak);
    /**
     * @brief 重置峰值计数器
     * @param[in] pool 池对象指针
     * @return BUFF_POOL_OK 成功; BUFF_POOL_ERR_INVAL 入参非法
     */
    int buffer_pool_reset_peak(buffer_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* BUFFER_POOL_H */
