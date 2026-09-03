/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file buffer.h
 *@brief buffer 头文件
 *@author H-000-H
 *@details
 *   @note: 使用编译器原生对齐标签替代硬编码 Padding。
 *   @note: 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 *   字节，这里强制对齐 64 字节。
 *   @warning: 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */

#ifndef BUFFER_H
#define BUFFER_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef ENODATA
#define ENODATA 61 /**< 部分 libc (newlib 等) 缺少 ENODATA, 补齐 Linux 取值 */
#endif

/* -------------------------------------------------------------------------- */
/*  BUFF 家族统一错误码 (自成一系, 直接包装 C 标准 errno, 不依赖 mini_tree 其他文件) */
/* -------------------------------------------------------------------------- */
#define BUFF_OK 0                 /**< 成功 */
#define BUFF_ERR_INVAL (-EINVAL)  /**< 入参非法 (句柄/数据指针为 NULL、容量为 0 或非 2 的幂) */
#define BUFF_ERR_FULL (-ENOSPC)   /**< 缓冲已满, 无法写入 */
#define BUFF_ERR_EMPTY (-ENODATA) /**< 缓冲为空, 无法读取 */

/**
 * @brief 元素类型为 uintptr_t: 既能ADC 16 位采样值, 也能下半部 work 指针
 */
typedef uintptr_t fifo_data_type;

#if defined(__GNUC__) || defined(__clang__)
#define BUFF_ALIGN(x) __attribute__((aligned(x)))
#else
#define BUFF_ALIGN(x)
#endif

/**
 * @brief SPSC 无锁原子操作原语 (全部 buffer 家族统一使用: fifo / fifo_uni / double_buffer)
 * @note acquire/release 内存序: 发布侧 STORE_RELEASE, 观察侧 LOAD_ACQUIRE, 私有侧 LOAD_RELAXED
 * @note 带降级: 非 GCC/Clang 编译器退化为 volatile 普通读写 (牺牲跨核内存序保证, 单核裸机场景可用)
 * @warning 本文件不允许引入 mini_tree 的其他文件, 故不复用 compiler_compat.h 的原子封装。
 */
#if defined(__GNUC__) || defined(__clang__)
#define BUFF_LOAD_ACQUIRE(ptr) __atomic_load_n(&(ptr), __ATOMIC_ACQUIRE)
#define BUFF_LOAD_RELAXED(ptr) __atomic_load_n(&(ptr), __ATOMIC_RELAXED)
#define BUFF_STORE_RELEASE(ptr, val) __atomic_store_n(&(ptr), (val), __ATOMIC_RELEASE)
#else
/* 降级: 非 GCC/Clang 无 __atomic/__typeof__, 退化为普通读写 (需配合 volatile
 * 全局变量或单核场景使用) */
#define BUFF_LOAD_ACQUIRE(ptr) (ptr)
#define BUFF_LOAD_RELAXED(ptr) (ptr)
#define BUFF_STORE_RELEASE(ptr, val) ((ptr) = (val))
#endif

/**
 * @brief 内存拷贝原语 (全部 buffer 家族统一使用, 带降级)
 * @note GCC/Clang 用 __builtin_memcpy (免头文件依赖且利于内联展开), 其它编译器降级标准 memcpy
 */
#if defined(__GNUC__) || defined(__clang__)
#define BUFF_MEM_COPY(dst, src, n) __builtin_memcpy((dst), (src), (n))
#else
#define BUFF_MEM_COPY(dst, src, n) memcpy((dst), (src), (n))
#endif

/* -------------------------------------------------------------------------- */
/* 1. 环形FIFO SPSC无锁缓冲区实现 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 环形FIFO SPSC无锁缓冲区实现
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
struct fifo_spsc
{
    fifo_data_type* buf BUFF_ALIGN(32); /**< 数据缓冲区 (32 字节对齐) */
    uint16_t            size;           /**< 缓冲区容量 */
    uint16_t            mask;           /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */

    BUFF_ALIGN(64) uint16_t w_ptr; /**< 写指针  */
    BUFF_ALIGN(64) uint16_t r_ptr; /**< 读指针  */
};

/**
 * @brief 初始化环形 FIFO SPSC 无锁缓冲区
 * @param[in] handle FIFO 句柄
 * @param[in] buf 数据缓冲区 (由调用方静态分配)
 * @param[in] size 缓冲区容量 (须为 2 的幂, 内部取 mask = size-1)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_init(struct fifo_spsc* handle, fifo_data_type* buf, uint16_t size);
/**
 * @brief 写入单个数据 (SPSC 无锁)
 * @param[in] handle FIFO 句柄
 * @param[in] data 待写入元素
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 缓冲满
 */
int fifo_write_data(struct fifo_spsc* handle, fifo_data_type data);
/**
 * @brief 读取单个数据 (SPSC 无锁)
 * @param[in] handle FIFO 句柄
 * @param[out] p_data 回传读出的元素
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 缓冲空
 */
int fifo_read_data(struct fifo_spsc* handle, fifo_data_type* p_data);

/**
 * @brief 写入块数据
 * @param[in] handle FIFO 句柄
 * @param[in] p_data 源缓冲区
 * @param[in] len 写入元素个数
 * @param[out] p_actual 可选: 实际写入元素个数 (≤ len), 可传 NULL
 * @return BUFF_OK 成功 (空间不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 无可写空间
 * (*p_actual = 0)
 */
int fifo_write_block(struct fifo_spsc* handle, const fifo_data_type* p_data, uint16_t len, uint16_t* p_actual);
/**
 * @brief 读取块数据
 * @param[in] handle FIFO 句柄
 * @param[out] p_data 目标缓冲区
 * @param[in] len 期望读取元素个数
 * @param[out] p_actual 可选: 实际读取元素个数 (≤ len), 可传 NULL
 * @return BUFF_OK 成功 (数据不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 无可读数据
 * (*p_actual = 0)
 */
int fifo_read_block(struct fifo_spsc* handle, fifo_data_type* p_data, uint16_t len, uint16_t* p_actual);
/**
 * @brief 查询 FIFO 是否已满
 * @param[in] handle FIFO 句柄
 * @param[out] p_full 回传结果: 满为 true
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_isfull(struct fifo_spsc* handle, bool* p_full);
/**
 * @brief 查询 FIFO 是否为空
 * @param[in] handle FIFO 句柄
 * @param[out] p_empty 回传结果: 空为 true
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_isempty(struct fifo_spsc* handle, bool* p_empty);
/**
 * @brief 查询 FIFO 当前元素个数
 * @param[in] handle FIFO 句柄
 * @param[out] p_count 回传元素个数
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_get_count(struct fifo_spsc* handle, uint16_t* p_count);

/* -------------------------------------------------------------------------- */
/* 1.1 统一环形FIFO SPSC无锁实现 (元素宽度可指定, 字节流/帧/任意定长项) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 统一环形FIFO SPSC无锁实现 (元素宽度初始化时指定)
 * @note 与 fifo_spsc 同款指针方案与 acquire/release 内存序, 容量以元素计;
 *       item_size=1 即字节流 (TCP/串口), item_size=sizeof(帧) 即帧队列 (USB 网卡)。
 * @note 零拷贝接口: 生产侧 fifo_uni_write_acquire/commit, 消费侧 fifo_uni_read_peek/release。
 * @warning SPSC 严格单生产者单消费者, 多端并发即数据竞争; 调用方自行保证。
 * @warning buf 需按元素对齐要求静态分配 (如 DMA 4 字节对齐时, item_size 取 4 的倍数)。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
struct fifo_uni_spsc
{
    uint8_t* buf BUFF_ALIGN(32); /**< 数据缓冲区 (字节数组, 32 字节对齐) */
    uint16_t     size;           /**< 缓冲区容量 (元素个数, 须为 2 的幂) */
    uint16_t     item_size;      /**< 单个元素宽度 (字节) */
    uint16_t     mask;           /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */

    BUFF_ALIGN(64) uint16_t w_ptr; /**< 写指针 (元素个数, 64 字节对齐, 独占缓存行) */
    BUFF_ALIGN(64) uint16_t r_ptr; /**< 读指针 (元素个数, 64 字节对齐, 独占缓存行) */
};

/**
 * @brief 初始化统一环形 FIFO SPSC 无锁缓冲区
 * @param[in] handle FIFO 句柄
 * @param[in] buf 数据缓冲区 (由调用方静态分配, 字节容量 = item_size x item_count)
 * @param[in] item_size 单个元素宽度 (字节, 不为 0)
 * @param[in] item_count 元素个数 (须为 2 的幂, 内部取 mask = item_count-1)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_uni_init(struct fifo_uni_spsc* handle, void* buf, uint16_t item_size, uint16_t item_count);
/**
 * @brief 写入元素块 (按元素宽度拷贝)
 * @param[in] handle FIFO 句柄
 * @param[in] p_data 源缓冲区 (元素数组)
 * @param[in] count 写入元素个数 (item_size=1 时即字节数)
 * @param[out] p_actual 可选: 实际写入元素个数 (≤ count), 可传 NULL
 * @return BUFF_OK 成功 (空间不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 无可写空间
 * (*p_actual = 0)
 */
int fifo_uni_write_block(struct fifo_uni_spsc* handle, const void* p_data, uint16_t count, uint16_t* p_actual);
/**
 * @brief 读取元素块 (按元素宽度拷贝)
 * @param[in] handle FIFO 句柄
 * @param[out] p_data 目标缓冲区 (元素数组)
 * @param[in] count 期望读取元素个数 (item_size=1 时即字节数)
 * @param[out] p_actual 可选: 实际读取元素个数 (≤ count), 可传 NULL
 * @return BUFF_OK 成功 (数据不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 无可读数据
 * (*p_actual = 0)
 */
int fifo_uni_read_block(struct fifo_uni_spsc* handle, void* p_data, uint16_t count, uint16_t* p_actual);
/**
 * @brief 生产者获取下一个可写槽位 (零拷贝, 不推进写指针)
 * @param[in] handle FIFO 句柄
 * @param[out] p_slot 回传槽位指针 (仅成功时有效)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 缓冲满 (*p_slot 不写)
 */
int fifo_uni_write_acquire(struct fifo_uni_spsc* handle, void** p_slot);
/**
 * @brief 发布已填写的槽位 (与 fifo_uni_write_acquire 配对, 写指针 +1)
 * @param[in] handle FIFO 句柄
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_uni_write_commit(struct fifo_uni_spsc* handle);
/**
 * @brief 消费者察视下一个可读槽位 (零拷贝, 不推进读指针)
 * @param[in] handle FIFO 句柄
 * @param[out] p_slot 回传槽位指针 (仅成功时有效)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 缓冲空 (*p_slot 不写)
 */
int fifo_uni_read_peek(struct fifo_uni_spsc* handle, void** p_slot);
/**
 * @brief 释放已消费的槽位 (与 fifo_uni_read_peek 配对, 读指针 +1)
 * @param[in] handle FIFO 句柄
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_uni_read_release(struct fifo_uni_spsc* handle);
/**
 * @brief 查询统一 FIFO 是否已满
 * @param[in] handle FIFO 句柄
 * @param[out] p_full 回传结果: 满为 true
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_uni_isfull(struct fifo_uni_spsc* handle, bool* p_full);
/**
 * @brief 查询统一 FIFO 是否为空
 * @param[in] handle FIFO 句柄
 * @param[out] p_empty 回传结果: 空为 true
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_uni_isempty(struct fifo_uni_spsc* handle, bool* p_empty);
/**
 * @brief 查询统一 FIFO 当前元素个数 (item_size=1 时即字节数)
 * @param[in] handle FIFO 句柄
 * @param[out] p_count 回传元素个数
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int fifo_uni_get_count(struct fifo_uni_spsc* handle, uint16_t* p_count);

/* -------------------------------------------------------------------------- */
/*2.双缓冲区实现 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 双缓冲区实现
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */

typedef uintptr_t double_buffer_data_type;
struct double_buffer_spsc
{
    BUFF_ALIGN(32) double_buffer_data_type* buf1; /**< 缓冲区 1 (32 字节对齐) */
    BUFF_ALIGN(32) double_buffer_data_type* buf2; /**< 缓冲区 2 (32 字节对齐) */
    uint16_t size;                                /**< 缓冲区容量 */
    uint16_t mask;                                /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */
    BUFF_ALIGN(64) uint16_t w_ptr;                /**< 写指针  */
    BUFF_ALIGN(64) uint16_t r_ptr;                /**< 读指针  */
};
/**
 * @brief 初始化双缓冲区 SPSC (写侧/读侧各自独占缓冲)
 * @param[in] handle 双缓冲句柄
 * @param[in] buf1 缓冲区 1 (写侧)
 * @param[in] buf2 缓冲区 2 (读侧)
 * @param[in] size 每缓冲容量 (须为 2 的幂)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int double_buffer_init(struct double_buffer_spsc* handle, double_buffer_data_type* buf1, double_buffer_data_type* buf2, uint16_t size);
/**
 * @brief 写入单个数据
 * @param[in] handle 双缓冲句柄
 * @param[in] data 待写入元素
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 写侧满
 */
int double_buffer_write_data(struct double_buffer_spsc* handle, double_buffer_data_type data);
/**
 * @brief 读取单个数据
 * @param[in] handle 双缓冲句柄
 * @param[out] p_data 回传读出的元素
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 读侧空
 */
int double_buffer_read_data(struct double_buffer_spsc* handle, double_buffer_data_type* p_data);
/**
 * @brief 写入块数据
 * @param[in] handle 双缓冲句柄
 * @param[in] p_data 源缓冲区
 * @param[in] len 写入元素个数
 * @param[out] p_actual 可选: 实际写入元素个数 (≤ len), 可传 NULL
 * @return BUFF_OK 成功 (空间不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 无可写空间
 * (*p_actual = 0)
 */
int double_buffer_write_block(struct double_buffer_spsc* handle, const double_buffer_data_type* p_data, uint16_t len, uint16_t* p_actual);
/**
 * @brief 读取块数据
 * @param[in] handle 双缓冲句柄
 * @param[out] p_data 目标缓冲区
 * @param[in] len 期望读取元素个数
 * @param[out] p_actual 可选: 实际读取元素个数 (≤ len), 可传 NULL
 * @return BUFF_OK 成功 (数据不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 无可读数据
 * (*p_actual = 0)
 */
int double_buffer_read_block(struct double_buffer_spsc* handle, double_buffer_data_type* p_data, uint16_t len, uint16_t* p_actual);
/**
 * @brief 查询双缓冲是否已满
 * @param[in] handle 双缓冲句柄
 * @param[out] p_full 回传结果: 满为 true
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int double_buffer_isfull(struct double_buffer_spsc* handle, bool* p_full);
/**
 * @brief 查询双缓冲是否为空
 * @param[in] handle 双缓冲句柄
 * @param[out] p_empty 回传结果: 空为 true
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int double_buffer_isempty(struct double_buffer_spsc* handle, bool* p_empty);
/**
 * @brief 查询双缓冲当前元素个数
 * @param[in] handle 双缓冲句柄
 * @param[out] p_count 回传元素个数
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int double_buffer_get_count(struct double_buffer_spsc* handle, uint16_t* p_count);

/* -------------------------------------------------------------------------- */
/* 1.4 双缓冲独立读写 (dual_buffer, 可指定元素宽度, 同时收发不阻塞) */
/* 约束: 单生产者对单消费者 (SPSC), 生产者与消费者各为单一线程/上下文。 */
/* 与 double_buffer_spsc 的区别: 两缓冲各自有独立读写指针, 无交换门槛, */
/* 生产者写满一个自动切到另一个 (不等读空), 消费者读空一个自动切到另一个 (不等写满), */
/* 适合连续流 (音频/传感器采样) 场景, 读写速率不等也不会互相阻塞。 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 双缓冲独立读写结构体 (可指定元素宽度, 适合连续流)
 *
 * 内存布局:
 * - buf1:    缓冲区 1 (字节数组)
 * - buf2:    缓冲区 2 (字节数组)
 * - size:    单个缓冲区容量 (元素个数, 须为 2 的幂)
 * - item_size: 单个元素宽度 (字节)
 * - mask:    掩码 (size - 1)
 * - w1/r1:   缓冲区 1 的写/读指针
 * - w2/r2:   缓冲区 2 的写/读指针
 * - active_w/active_r: 当前活跃写/读缓冲索引 (0=buf1, 1=buf2)
 */
struct dual_buffer_spsc
{
    uint8_t* buf1;      /**< 缓冲区 1 */
    uint8_t* buf2;      /**< 缓冲区 2 */
    uint16_t size;      /**< 单个缓冲区容量 (元素个数, 须为 2 的幂) */
    uint16_t item_size; /**< 单个元素宽度 (字节) */
    uint16_t mask;      /**< 掩码值等于 size - 1 */

    BUFF_ALIGN(64) uint16_t w1; /**< 缓冲区 1 写指针  */
    BUFF_ALIGN(64) uint16_t r1; /**< 缓冲区 1 读指针  */
    BUFF_ALIGN(64) uint16_t w2; /**< 缓冲区 2 写指针  */
    BUFF_ALIGN(64) uint16_t r2; /**< 缓冲区 2 读指针  */
    uint8_t active_w;           /**< 当前活跃写缓冲索引 (0=buf1, 1=buf2) */
    uint8_t active_r;           /**< 当前活跃读缓冲索引 (0=buf1, 1=buf2) */
};

/**
 * @brief 初始化双缓冲独立读写结构体 (可指定元素宽度)
 *
 * @param handle     结构体指针 (不能为 NULL)
 * @param buffer1    缓冲区 1 起始地址 (须满足 size * item_size 字节)
 * @param buffer2    缓冲区 2 起始地址 (同上)
 * @param item_size  单个元素宽度 (字节)
 * @param item_count 单个缓冲区容量 (元素个数, 须为 2 的幂)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法
 */
int dual_buffer_init(struct dual_buffer_spsc* handle, void* buffer1, void* buffer2, uint16_t item_size, uint16_t item_count);

/**
 * @brief 块写入双缓冲 (轮流写两个, 无阻塞, 满则切到另一个)
 *
 * @param handle   结构体指针 (不能为 NULL)
 * @param p_data   待写入数据数组 (元素个数 >= count)
 * @param count    待写入元素个数 (>0)
 * @param p_actual 可选: 实际写入元素个数 (≤ count), 可传 NULL
 * @return BUFF_OK 成功 (空间不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_FULL 两缓冲都满
 * (*p_actual = 0)
 */
int dual_buffer_write_block(struct dual_buffer_spsc* handle, const void* p_data, uint16_t count, uint16_t* p_actual);

/**
 * @brief 块读取双缓冲 (轮流读两个, 无阻塞, 空则切到另一个)
 *
 * @param handle   结构体指针 (不能为 NULL)
 * @param p_data   读取目标数组 (元素个数 >= count)
 * @param count    待读取元素个数 (>0)
 * @param p_actual 可选: 实际读取元素个数 (≤ count), 可传 NULL
 * @return BUFF_OK 成功 (数据不足时截断); BUFF_ERR_INVAL 入参非法; BUFF_ERR_EMPTY 两缓冲都空
 * (*p_actual = 0)
 */
int dual_buffer_read_block(struct dual_buffer_spsc* handle, void* p_data, uint16_t count, uint16_t* p_actual);

/**
 * @brief 查询双缓冲是否全部写满 (两个都满)
 *
 * @param handle 结构体指针 (不能为 NULL)
 * @param p_full 回传结果: 两缓冲都满为 true, 否则为 false
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法 (p_full 回传 false)
 */
int dual_buffer_isfull(const struct dual_buffer_spsc* handle, bool* p_full);

/**
 * @brief 查询双缓冲是否全部为空 (两个都空)
 *
 * @param handle 结构体指针 (不能为 NULL)
 * @param p_empty 回传结果: 两缓冲都空为 true, 否则为 false
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法 (p_empty 回传 true)
 */
int dual_buffer_isempty(const struct dual_buffer_spsc* handle, bool* p_empty);

/**
 * @brief 获取双缓冲当前已存元素总数 (两个之和)
 *
 * @param handle 结构体指针 (不能为 NULL)
 * @param p_count 回传已存元素总数 (0 到 2 * size)
 * @return BUFF_OK 成功; BUFF_ERR_INVAL 入参非法 (*p_count = 0)
 */
int dual_buffer_get_count(const struct dual_buffer_spsc* handle, uint16_t* p_count);

#endif /* BUFFER_H */
