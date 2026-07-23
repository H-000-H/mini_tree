/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file        circle_fifo_buffer.c
 * @brief       环形 FIFO SPSC 无锁缓冲区实现
 * @note        acquire/release 内存序保证单生产者单消费者安全; 见 buffer.h
 */
#include "buffer.h"

#define FIFO_LOAD_ACQUIRE(ptr)       __atomic_load_n(&((ptr)), __ATOMIC_ACQUIRE)
#define FIFO_LOAD_RELAXED(ptr)     __atomic_load_n(&((ptr)), __ATOMIC_RELAXED)
#define FIFO_STORE_RELEASE(ptr, val) __atomic_store_n(&((ptr)), (val), __ATOMIC_RELEASE)

/**
 * @brief 初始化 SPSC FIFO
 * @param handle 句柄
 * @param buf 缓冲
 * @param size 容量
 */
void fifo_init(struct fifo_spsc* handle, Fifo_Data_type* buf, uint16_t size)
{
    /**< 防御性空指针与 2的幂次方 拦截 */
    if (!handle || !buf || size == 0 || (size & (size - 1)) != 0)
        return;

    handle->buf = buf;
    handle->size = size;
    handle->mask = (uint16_t)(size - 1); 
    
    FIFO_STORE_RELEASE(handle->w_ptr, 0);
    FIFO_STORE_RELEASE(handle->r_ptr, 0);
}

/**
 * @brief 写单元素
 * @param handle 句柄
 * @param data 数据
 * @return true
 */
bool fifo_write_data(struct fifo_spsc* handle, Fifo_Data_type data)
{
    uint16_t r = FIFO_LOAD_ACQUIRE(handle->r_ptr);
    uint16_t w = FIFO_LOAD_RELAXED(handle->w_ptr);
    
    /**< 利用 uint16_t 溢出特性，已用空间就是纯粹的 w - r */
    if ((uint16_t)(w - r) >= handle->size) return false;

    /**< 写入时通过掩码映射物理数组下标 */
    handle->buf[w & handle->mask] = data;
    
    /**< 指针自增，不执行提前裁剪 */
    FIFO_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + 1));
    return true;
}

/**
 * @brief 块写
 * @param handle 句柄
 * @param p_data 源
 * @param len 长度
 * @return 写入数
 */
uint16_t fifo_write_block(struct fifo_spsc* handle, const Fifo_Data_type* p_data, uint16_t len)
{
    if (!handle || !p_data || len == 0) return 0;

    uint16_t r = FIFO_LOAD_ACQUIRE(handle->r_ptr);
    uint16_t w = FIFO_LOAD_RELAXED(handle->w_ptr);
    
    uint16_t free_len = handle->size - (uint16_t)(w - r);
    
    if (len > free_len) len = free_len;
    if (len == 0)        return 0;

    /**< 计算物理下标以及映射到连续线性末尾的实际空间 */
    uint16_t w_idx = w & handle->mask;
    uint16_t space_to_end = handle->size - w_idx;
    
    if (space_to_end >= len)
    {
        __builtin_memcpy(&handle->buf[w_idx], p_data, len * sizeof(Fifo_Data_type));
    }
    else
    {
        __builtin_memcpy(&handle->buf[w_idx], p_data, space_to_end * sizeof(Fifo_Data_type));
        __builtin_memcpy(&handle->buf[0], p_data + space_to_end, (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    
    FIFO_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + len));
    return len;
}

/**
 * @brief 读单元素
 * @param handle 句柄
 * @param p_data 输出
 * @return true
 */
bool fifo_read_data(struct fifo_spsc* handle, Fifo_Data_type* p_data)
{
    if (!handle || !p_data) return false;

    uint16_t w = FIFO_LOAD_ACQUIRE(handle->w_ptr);
    uint16_t r = FIFO_LOAD_RELAXED(handle->r_ptr);
    if (r == w) return false;

    *p_data = handle->buf[r & handle->mask];
    FIFO_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + 1));
    return true;
}

/**
 * @brief 块读
 * @param handle 句柄
 * @param p_data 输出
 * @param len 长度
 * @return 读出数
 */
uint16_t fifo_read_block(struct fifo_spsc* handle, Fifo_Data_type* p_data, uint16_t len)
{
    if (!handle || !p_data || len == 0) return 0;

    uint16_t w = FIFO_LOAD_ACQUIRE(handle->w_ptr);
    uint16_t r = FIFO_LOAD_RELAXED(handle->r_ptr);
    
    uint16_t count = (uint16_t)(w - r);
    if (len > count) len = count;
    if (len == 0)    return 0;

    uint16_t r_idx = r & handle->mask;
    uint16_t space_to_end = handle->size - r_idx;
    
    if (space_to_end >= len)
    {
        __builtin_memcpy(p_data, &handle->buf[r_idx], len * sizeof(Fifo_Data_type));
    }
    else
    {
        __builtin_memcpy(p_data, &handle->buf[r_idx], space_to_end * sizeof(Fifo_Data_type));
        __builtin_memcpy(p_data + space_to_end, &handle->buf[0], (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    
    FIFO_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + len));
    return len;
}

/**
 * @brief 元素计数
 * @param handle 句柄
 * @return 个数
 */
uint16_t fifo_get_count(struct fifo_spsc* handle)
{
    if (!handle) return 0;
    uint16_t w = FIFO_LOAD_ACQUIRE(handle->w_ptr);
    uint16_t r = FIFO_LOAD_ACQUIRE(handle->r_ptr);
    return (uint16_t)(w - r);
}

/**
 * @brief 是否空
 * @param handle 句柄
 * @return true
 */
bool fifo_isempty(struct fifo_spsc* handle)
{
    if (!handle) return true;
    uint16_t r = FIFO_LOAD_RELAXED(handle->r_ptr);
    uint16_t w = FIFO_LOAD_ACQUIRE(handle->w_ptr);
    return r == w;
}

/**
 * @brief 是否满
 * @param handle 句柄
 * @return true
 */
bool fifo_isfull(struct fifo_spsc* handle)
{
    if (!handle) return false;
    uint16_t r = FIFO_LOAD_ACQUIRE(handle->r_ptr);
    uint16_t w = FIFO_LOAD_RELAXED(handle->w_ptr);
    return (uint16_t)(w - r) >= handle->size;
}