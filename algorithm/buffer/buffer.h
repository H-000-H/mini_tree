/**
 * @license: SPDX-License-Identifier: Apache-2.0 
 * @file: buffer.h
 * @brief: 环形FIFO SPSC无锁缓冲区实现
 * @note: 使用编译器原生对齐标签替代硬编码 Padding。
 * @note: 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning: 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
 #ifndef BUFFER_H
 #define BUFFER_H
 
 #include <stdint.h>
 #include <stdbool.h>
 #include <string.h>
/**
 * @brief 元素类型为 uintptr_t: 既能ADC 16 位采样值, 也能下半部 work 指针
 */
 typedef uintptr_t Fifo_Data_type;
 
 #if defined(__GNUC__) || defined(__clang__)
     #define ATTR_ALIGN(x) __attribute__((aligned(x)))
 #else
     #define ATTR_ALIGN(x)
 #endif
 
/*=================================================================================================================================================*/
/* 1. 环形FIFO SPSC无锁缓冲区实现                                                                                                        */
/*=================================================================================================================================================*/
 /**
  * @brief 环形FIFO SPSC无锁缓冲区实现
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 struct fifo_spsc
 {
     Fifo_Data_type* buf ATTR_ALIGN(32);
     uint16_t size;
     uint16_t mask; /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */
 
     ATTR_ALIGN(64) uint16_t w_ptr;
     ATTR_ALIGN(64) uint16_t r_ptr;
 };
 
 /**
  * @brief 环形FIFO SPSC无锁缓冲区初始化
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 void fifo_init(struct fifo_spsc* handle, Fifo_Data_type* buf, uint16_t size);
 /**
  * @brief 环形FIFO SPSC无锁缓冲区写数据
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 bool fifo_write_data(struct fifo_spsc* handle, Fifo_Data_type data);
 /**
  * @brief 环形FIFO SPSC无锁缓冲区读数据
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 bool fifo_read_data(struct fifo_spsc* handle, Fifo_Data_type* p_data);

 /**
  * @brief 环形FIFO SPSC无锁缓冲区写块数据
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 uint16_t fifo_write_block(struct fifo_spsc* handle, const Fifo_Data_type* p_data, uint16_t len);
 /**
  * @brief 环形FIFO SPSC无锁缓冲区读块数据
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 uint16_t fifo_read_block(struct fifo_spsc* handle, Fifo_Data_type* p_data, uint16_t len);
 /**
  * @brief 环形FIFO SPSC无锁缓冲区是否满
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 bool fifo_isfull(struct fifo_spsc* handle);
 /**
  * @brief 环形FIFO SPSC无锁缓冲区是否空
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 bool fifo_isempty(struct fifo_spsc* handle);
 /**
  * @brief 环形FIFO SPSC无锁缓冲区获取数据长度
  * @note 使用编译器原生对齐标签替代硬编码 Padding。
  * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
  * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
  */
 uint16_t fifo_get_count(struct fifo_spsc* handle);
 
/*=================================================================================================================================================*/
/*2.双缓冲区实现                                                                                                        */
/*=================================================================================================================================================*/
/**
 * @brief 双缓冲区实现
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */

typedef uintptr_t Double_Buffer_Data_type;
struct double_buffer_spsc
{
    ATTR_ALIGN(32) Double_Buffer_Data_type* buf1;
    ATTR_ALIGN(32) Double_Buffer_Data_type* buf2;
    uint16_t size;
    uint16_t mask; /**< 掩码值等于 size - 1，用于在最后一步映射物理数组下标 */
    ATTR_ALIGN(64) uint16_t w_ptr;
    ATTR_ALIGN(64) uint16_t r_ptr;
};
/**
 * @brief 双缓冲区初始化
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
void double_buffer_init(struct double_buffer_spsc* handle, Double_Buffer_Data_type* buf1, Double_Buffer_Data_type* buf2, uint16_t size);
/**
 * @brief 双缓冲区写数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_write_data(struct double_buffer_spsc* handle, Double_Buffer_Data_type data);
/**
 * @brief 双缓冲区读数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_read_data(struct double_buffer_spsc* handle, Double_Buffer_Data_type* p_data);   
/**
 * @brief 双缓冲区写块数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t double_buffer_write_block(struct double_buffer_spsc* handle, const Double_Buffer_Data_type* p_data, uint16_t len);
/**
 * @brief 双缓冲区读块数据
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t double_buffer_read_block(struct double_buffer_spsc* handle, Double_Buffer_Data_type* p_data, uint16_t len);
/**
 * @brief 双缓冲区是否满
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_isfull(struct double_buffer_spsc* handle);
/**
 * @brief 双缓冲区是否空
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
bool double_buffer_isempty(struct double_buffer_spsc* handle);
/**
 * @brief 双缓冲区获取数据长度
 * @note 使用编译器原生对齐标签替代硬编码 Padding。
 * @note 针对主流嵌入式核心（Cortex-M7/A、ESP32双核等），Cache Line 一般为 32 或 64 字节，这里强制对齐 64 字节。
 * @warning 本文件不允许引入 mini_tree 的其他文件。且允许不遵守 VFS 的警告规则。
 */
uint16_t double_buffer_get_count(struct double_buffer_spsc* handle);
 #endif /* BUFFER_H */