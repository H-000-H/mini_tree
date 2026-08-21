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
 * @brief 环形FIFO SPSC无锁缓冲区初始化
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
void fifo_init(struct fifo_spsc* handle, fifo_data_type* buf, uint16_t size);
/**
 * @brief 环形FIFO SPSC无锁缓冲区写数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool fifo_write_data(struct fifo_spsc* handle, fifo_data_type data);
/**
 * @brief 环形FIFO SPSC无锁缓冲区读数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool fifo_read_data(struct fifo_spsc* handle, fifo_data_type* p_data);

/**
 * @brief 环形FIFO SPSC无锁缓冲区写块数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t fifo_write_block(struct fifo_spsc* handle, const fifo_data_type* p_data, uint16_t len);
/**
 * @brief 环形FIFO SPSC无锁缓冲区读块数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t fifo_read_block(struct fifo_spsc* handle, fifo_data_type* p_data, uint16_t len);
/**
 * @brief 环形FIFO SPSC无锁缓冲区是否满
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool fifo_isfull(struct fifo_spsc* handle);
/**
 * @brief 环形FIFO SPSC无锁缓冲区是否空
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool fifo_isempty(struct fifo_spsc* handle);
/**
 * @brief 环形FIFO SPSC无锁缓冲区获取数据长度
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
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
 * @brief 双缓冲区初始化
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
void double_buffer_init(struct double_buffer_spsc* handle, double_buffer_data_type* buf1,
                        double_buffer_data_type* buf2, uint16_t size);
/**
 * @brief 双缓冲区写数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_write_data(struct double_buffer_spsc* handle, double_buffer_data_type data);
/**
 * @brief 双缓冲区读数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_read_data(struct double_buffer_spsc* handle, double_buffer_data_type* p_data);
/**
 * @brief 双缓冲区写块数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t double_buffer_write_block(struct double_buffer_spsc* handle,
                                   const double_buffer_data_type* p_data, uint16_t len);
/**
 * @brief 双缓冲区读块数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t double_buffer_read_block(struct double_buffer_spsc* handle,
                                  double_buffer_data_type* p_data, uint16_t len);
/**
 * @brief 双缓冲区是否满
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_isfull(struct double_buffer_spsc* handle);
/**
 * @brief 双缓冲区是否空
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_isempty(struct double_buffer_spsc* handle);
/**
 * @brief 双缓冲区获取数据长度
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64
 * 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t double_buffer_get_count(struct double_buffer_spsc* handle);
#endif /* BUFFER_H */
