/*
 * 无锁 SPSC 环形缓冲区 — 并发合约参见 m_buffer.h。
 * 禁止在没有外部互斥锁的情况下向同一个 FIFO_Type_Def 添加第二个生产者或消费者。
 */
#include "m_buffer.h"

void fifo_init(FIFO_Type_Def* handle, Fifo_Data_type* buf, uint16_t size)
{
    handle->buf = buf;
    handle->size = size;
    atomic_init(&handle->r_ptr, 0);
    atomic_init(&handle->w_ptr, 0);
}

bool fifo_write_data(FIFO_Type_Def* handle, Fifo_Data_type data)
{
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_relaxed);
    if (((w + 1) % handle->size) == r) return false;

    handle->buf[w] = data;
    atomic_store_explicit(&handle->w_ptr, (uint16_t)((w + 1) % handle->size), memory_order_release);
    return true;
}

uint16_t fifo_write_block(FIFO_Type_Def* handle, const Fifo_Data_type* p_data, uint16_t len)
{
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_relaxed);
    uint16_t used = (w + handle->size - r) % handle->size;
    uint16_t free_len = (handle->size - 1) - used;
    if (free_len < len)
    {
        len = free_len;
    }
    if (free_len == 0)
    {
        return 0;
    }

    uint16_t space_to_end = handle->size - w;
    if (space_to_end >= len)
    {
        memcpy(&handle->buf[w], p_data, len * sizeof(Fifo_Data_type));
    }
    else
    {
        memcpy(&handle->buf[w], p_data, space_to_end * sizeof(Fifo_Data_type));
        memcpy(&handle->buf[0], p_data + space_to_end, (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    atomic_store_explicit(&handle->w_ptr, (uint16_t)((w + len) % handle->size), memory_order_release);
    return len;
}

bool fifo_read_data(FIFO_Type_Def* handle, Fifo_Data_type* p_data)
{
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_relaxed);
    if (r == w) return false;

    *p_data = handle->buf[r];
    atomic_store_explicit(&handle->r_ptr, (uint16_t)((r + 1) % handle->size), memory_order_release);
    return true;
}

uint16_t fifo_read_block(FIFO_Type_Def* handle, Fifo_Data_type* p_data, uint16_t len)
{
    memset(p_data, 0, sizeof(*p_data) * len);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_relaxed);
    uint16_t count = (w + handle->size - r) % handle->size;
    if (len > count)
    {
        len = count;
    }
    if (count == 0) return 0;

    uint16_t space_to_end = handle->size - r;
    if (space_to_end >= len)
    {
        memcpy(p_data, &handle->buf[r], len * sizeof(Fifo_Data_type));
    }
    else
    {
        memcpy(p_data, &handle->buf[r], space_to_end * sizeof(Fifo_Data_type));
        memcpy(p_data + space_to_end, &handle->buf[0], (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    atomic_store_explicit(&handle->r_ptr, (uint16_t)((r + len) % handle->size), memory_order_release);
    return len;
}

uint16_t fifo_get_count(FIFO_Type_Def* handle)
{
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    return (w + handle->size - r) % handle->size;
}

bool fifo_isempty(FIFO_Type_Def* handle)
{
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_relaxed);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_acquire);
    return r == w;
}

bool fifo_isfull(FIFO_Type_Def* handle)
{
    uint16_t r = atomic_load_explicit(&handle->r_ptr, memory_order_acquire);
    uint16_t w = atomic_load_explicit(&handle->w_ptr, memory_order_relaxed);
    return ((w + 1) % handle->size) == r;
}