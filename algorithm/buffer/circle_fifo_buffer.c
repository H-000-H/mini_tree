/*
 * 无锁 SPSC 环形缓冲区 — 并发合约参见 m_buffer.h。
 * 禁止在没有外部互斥锁的情况下向同一个 struct fifo_spsc 添加第二个生产者或消费者。
 */
#include "m_buffer.h"
#include "compiler_compat_poison.h"

#define FIFO_LOAD_ACQ(ptr)   __atomic_load_n(&(ptr), __ATOMIC_ACQUIRE)
#define FIFO_LOAD_RELAX(ptr) __atomic_load_n(&(ptr), __ATOMIC_RELAXED)
#define FIFO_STORE_REL(ptr, val) __atomic_store_n(&(ptr), (val), __ATOMIC_RELEASE)

void fifo_init(struct fifo_spsc* handle, Fifo_Data_type* buf, uint16_t size)
{
    handle->buf = buf;
    handle->size = size;
    FIFO_STORE_REL(handle->r_ptr, 0);
    FIFO_STORE_REL(handle->w_ptr, 0);
}

bool fifo_write_data(struct fifo_spsc* handle, Fifo_Data_type data)
{
    uint16_t r = FIFO_LOAD_ACQ(handle->r_ptr);
    uint16_t w = FIFO_LOAD_RELAX(handle->w_ptr);
    if (((w + 1) % handle->size) == r) return false;

    handle->buf[w] = data;
    FIFO_STORE_REL(handle->w_ptr, (uint16_t)((w + 1) % handle->size));
    return true;
}

uint16_t fifo_write_block(struct fifo_spsc* handle, const Fifo_Data_type* p_data, uint16_t len)
{
    uint16_t r = FIFO_LOAD_ACQ(handle->r_ptr);
    uint16_t w = FIFO_LOAD_RELAX(handle->w_ptr);
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
        __builtin_memcpy(&handle->buf[w], p_data, len * sizeof(Fifo_Data_type));
    }
    else
    {
        __builtin_memcpy(&handle->buf[w], p_data, space_to_end * sizeof(Fifo_Data_type));
        __builtin_memcpy(&handle->buf[0], p_data + space_to_end, (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    FIFO_STORE_REL(handle->w_ptr, (uint16_t)((w + len) % handle->size));
    return len;
}

bool fifo_read_data(struct fifo_spsc* handle, Fifo_Data_type* p_data)
{
    uint16_t w = FIFO_LOAD_ACQ(handle->w_ptr);
    uint16_t r = FIFO_LOAD_RELAX(handle->r_ptr);
    if (r == w) return false;

    *p_data = handle->buf[r];
    FIFO_STORE_REL(handle->r_ptr, (uint16_t)((r + 1) % handle->size));
    return true;
}

uint16_t fifo_read_block(struct fifo_spsc* handle, Fifo_Data_type* p_data, uint16_t len)
{
    __builtin_memset(p_data, 0, sizeof(*p_data) * len);
    uint16_t w = FIFO_LOAD_ACQ(handle->w_ptr);
    uint16_t r = FIFO_LOAD_RELAX(handle->r_ptr);
    uint16_t count = (w + handle->size - r) % handle->size;
    if (len > count)
    {
        len = count;
    }
    if (count == 0) return 0;

    uint16_t space_to_end = handle->size - r;
    if (space_to_end >= len)
    {
        __builtin_memcpy(p_data, &handle->buf[r], len * sizeof(Fifo_Data_type));
    }
    else
    {
        __builtin_memcpy(p_data, &handle->buf[r], space_to_end * sizeof(Fifo_Data_type));
        __builtin_memcpy(p_data + space_to_end, &handle->buf[0], (len - space_to_end) * sizeof(Fifo_Data_type));
    }
    FIFO_STORE_REL(handle->r_ptr, (uint16_t)((r + len) % handle->size));
    return len;
}

uint16_t fifo_get_count(struct fifo_spsc* handle)
{
    uint16_t w = FIFO_LOAD_ACQ(handle->w_ptr);
    uint16_t r = FIFO_LOAD_ACQ(handle->r_ptr);
    return (w + handle->size - r) % handle->size;
}

bool fifo_isempty(struct fifo_spsc* handle)
{
    uint16_t r = FIFO_LOAD_RELAX(handle->r_ptr);
    uint16_t w = FIFO_LOAD_ACQ(handle->w_ptr);
    return r == w;
}

bool fifo_isfull(struct fifo_spsc* handle)
{
    uint16_t r = FIFO_LOAD_ACQ(handle->r_ptr);
    uint16_t w = FIFO_LOAD_RELAX(handle->w_ptr);
    return ((w + 1) % handle->size) == r;
}
