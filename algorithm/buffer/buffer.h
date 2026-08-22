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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/**
 * @brief 元素类型为 uintptr_t: 既能ADC 16 位采样值, 也能下半部 work 指针
 */
typedef uintptr_t fifo_data_type;

#if defined(__GNUC__) || defined(__clang__)
#define ATTR_ALIGN(x) __attribute__((aligned(x)))
#else
#define ATTR_ALIGN(x)
#endif

/*=================================================================================================================================================*/
/* 1. 环形FIFO SPSC无锁缓冲区实现 */
/*=================================================================================================================================================*/
/**
 * @brief 环形FIFO SPSC无锁缓冲区实现
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
struct fifo_spsc
{
    fifo_data_type* buf ATTR_ALIGN(32); /**< 数据缓冲区 (32 字节对齐) */
    uint16_t size; /**< 缓冲区容量 */
    uint16_t mask; /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */

    ATTR_ALIGN(64) uint16_t w_ptr; /**< 写指针 (64 字节对齐, 独占缓存行) */
    ATTR_ALIGN(64) uint16_t r_ptr; /**< 读指针 (64 字节对齐, 独占缓存行) */
};

/**
 * @brief 初始化环形 FIFO SPSC 无锁缓冲区
 * @param[in] handle FIFO 句柄
 * @param[in] buf 数据缓冲区 (由调用方静态分配)
 * @param[in] size 缓冲区容量 (须为 2 的幂, 内部取 mask = size-1)
 */
void fifo_init(struct fifo_spsc* handle, fifo_data_type* buf, uint16_t size);
/**
 * @brief 写入单个数据 (SPSC 无锁)
 * @param[in] handle FIFO 句柄
 * @param[in] data 待写入元素
 * @return 成功返回 true, 缓冲满返回 false
 */
bool fifo_write_data(struct fifo_spsc* handle, fifo_data_type data);
/**
 * @brief 读取单个数据 (SPSC 无锁)
 * @param[in] handle FIFO 句柄
 * @param[out] p_data 回传读出的元素
 * @return 成功返回 true, 缓冲空返回 false
 */
bool fifo_read_data(struct fifo_spsc* handle, fifo_data_type* p_data);

/**
 * @brief 写入块数据
 * @param[in] handle FIFO 句柄
 * @param[in] p_data 源缓冲区
 * @param[in] len 写入元素个数
 * @return 实际写入元素个数
 */
uint16_t fifo_write_block(struct fifo_spsc* handle, const fifo_data_type* p_data, uint16_t len);
/**
 * @brief 读取块数据
 * @param[in] handle FIFO 句柄
 * @param[out] p_data 目标缓冲区
 * @param[in] len 期望读取元素个数
 * @return 实际读取元素个数
 */
uint16_t fifo_read_block(struct fifo_spsc* handle, fifo_data_type* p_data, uint16_t len);
/**
 * @brief 查询 FIFO 是否已满
 * @param[in] handle FIFO 句柄
 * @return 满返回 true
 */
bool fifo_isfull(struct fifo_spsc* handle);
/**
 * @brief 查询 FIFO 是否为空
 * @param[in] handle FIFO 句柄
 * @return 空返回 true
 */
bool fifo_isempty(struct fifo_spsc* handle);
/**
 * @brief 查询 FIFO 当前元素个数
 * @param[in] handle FIFO 句柄
 * @return 元素个数
 */
uint16_t fifo_get_count(struct fifo_spsc* handle);

/*=================================================================================================================================================*/
/*2.双缓冲区实现 */
/*=================================================================================================================================================*/
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
    ATTR_ALIGN(32) double_buffer_data_type* buf1; /**< 缓冲区 1 (32 字节对齐) */
    ATTR_ALIGN(32) double_buffer_data_type* buf2; /**< 缓冲区 2 (32 字节对齐) */
    uint16_t size; /**< 缓冲区容量 */
    uint16_t mask; /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */
    ATTR_ALIGN(64) uint16_t w_ptr; /**< 写指针 (64 字节对齐, 独占缓存行) */
    ATTR_ALIGN(64) uint16_t r_ptr; /**< 读指针 (64 字节对齐, 独占缓存行) */
};
/**
 * @brief 初始化双缓冲区 SPSC (写侧/读侧各自独占缓冲)
 * @param[in] handle 双缓冲句柄
 * @param[in] buf1 缓冲区 1 (写侧)
 * @param[in] buf2 缓冲区 2 (读侧)
 * @param[in] size 每缓冲容量 (须为 2 的幂)
 */
void double_buffer_init(struct double_buffer_spsc* handle, double_buffer_data_type* buf1, double_buffer_data_type* buf2, uint16_t size);
/**
 * @brief 写入单个数据
 * @param[in] handle 双缓冲句柄
 * @param[in] data 待写入元素
 * @return 成功返回 true, 写侧满返回 false
 */
bool double_buffer_write_data(struct double_buffer_spsc* handle, double_buffer_data_type data);
/**
 * @brief 读取单个数据
 * @param[in] handle 双缓冲句柄
 * @param[out] p_data 回传读出的元素
 * @return 成功返回 true, 读侧空返回 false
 */
bool double_buffer_read_data(struct double_buffer_spsc* handle, double_buffer_data_type* p_data);
/**
 * @brief 写入块数据
 * @param[in] handle 双缓冲句柄
 * @param[in] p_data 源缓冲区
 * @param[in] len 写入元素个数
 * @return 实际写入元素个数
 */
uint16_t double_buffer_write_block(struct double_buffer_spsc* handle, const double_buffer_data_type* p_data, uint16_t len);
/**
 * @brief 读取块数据
 * @param[in] handle 双缓冲句柄
 * @param[out] p_data 目标缓冲区
 * @param[in] len 期望读取元素个数
 * @return 实际读取元素个数
 */
uint16_t double_buffer_read_block(struct double_buffer_spsc* handle, double_buffer_data_type* p_data, uint16_t len);
/**
 * @brief 查询双缓冲是否已满
 * @param[in] handle 双缓冲句柄
 * @return 满返回 true
 */
bool double_buffer_isfull(struct double_buffer_spsc* handle);
/**
 * @brief 查询双缓冲是否为空
 * @param[in] handle 双缓冲句柄
 * @return 空返回 true
 */
bool double_buffer_isempty(struct double_buffer_spsc* handle);
/**
 * @brief 查询双缓冲当前元素个数
 * @param[in] handle 双缓冲句柄
 * @return 元素个数
 */
uint16_t double_buffer_get_count(struct double_buffer_spsc* handle);
#endif /* BUFFER_H */
