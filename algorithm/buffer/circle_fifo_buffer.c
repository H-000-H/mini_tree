/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file circle_fifo_buffer.c
 *@brief 环形FIFO SPSC无锁缓冲区实现
 *@author H-000-H
 *@details
 *   @note        acquire/release 内存序保证单生产者单消费者安全; 见 buffer.h
 *   @note        全部接口返回 BUFF_* 错误码; 长度类结果经指针参数回传
 */

#include "buffer.h"

/* 原子操作原语统一由 buffer.h 的 BUFF_LOAD_xxx / BUFF_STORE_xxx 宏提供 (带降级) */

int fifo_init(struct fifo_spsc* handle, fifo_data_type* buf, uint16_t size)
{
    /**< 防御性空指针与 2的幂次方 拦截 */
    if (!handle || !buf || size == 0 || (size & (size - 1)) != 0)
        return BUFF_ERR_INVAL;

    handle->buf = buf;
    handle->size = size;
    handle->mask = (uint16_t)(size - 1);

    BUFF_STORE_RELEASE(handle->w_ptr, 0);
    BUFF_STORE_RELEASE(handle->r_ptr, 0);
    return BUFF_OK;
}

int fifo_write_data(struct fifo_spsc* handle, fifo_data_type data)
{
    uint16_t r;
    uint16_t w;

    if (!handle)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    w = BUFF_LOAD_RELAXED(handle->w_ptr);

    /**< 利用 uint16_t 溢出特性，已用空间就是纯粹的 w - r */
    if ((uint16_t)(w - r) >= handle->size)
        return BUFF_ERR_FULL;

    /**< 写入时通过掩码映射物理数组下标 */
    handle->buf[w & handle->mask] = data;

    /**< 指针自增，不执行提前裁剪 */
    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + 1));
    return BUFF_OK;
}

int fifo_write_block(struct fifo_spsc* handle, const fifo_data_type* p_data, uint16_t len,
                     uint16_t* p_actual)
{
    uint16_t r;
    uint16_t w;
    uint16_t free_len;
    uint16_t w_idx;
    uint16_t space_to_end;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || len == 0)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    w = BUFF_LOAD_RELAXED(handle->w_ptr);

    free_len = (uint16_t)(handle->size - (uint16_t)(w - r));
    if (free_len == 0)
        return BUFF_ERR_FULL;

    if (len > free_len)
        len = free_len;

    /**< 计算物理下标以及映射到连续线性末尾的实际空间 */
    w_idx = (uint16_t)(w & handle->mask);
    space_to_end = (uint16_t)(handle->size - w_idx);

    if (space_to_end >= len)
    {
        BUFF_MEM_COPY(&handle->buf[w_idx], p_data, len * sizeof(fifo_data_type));
    }
    else
    {
        BUFF_MEM_COPY(&handle->buf[w_idx], p_data, space_to_end * sizeof(fifo_data_type));
        BUFF_MEM_COPY(&handle->buf[0], p_data + space_to_end,
                      (len - space_to_end) * sizeof(fifo_data_type));
    }

    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + len));
    if (p_actual)
        *p_actual = len;
    return BUFF_OK;
}

int fifo_read_data(struct fifo_spsc* handle, fifo_data_type* p_data)
{
    uint16_t w;
    uint16_t r;

    if (!handle || !p_data)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    r = BUFF_LOAD_RELAXED(handle->r_ptr);
    if (r == w)
        return BUFF_ERR_EMPTY;

    *p_data = handle->buf[r & handle->mask];
    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + 1));
    return BUFF_OK;
}

int fifo_read_block(struct fifo_spsc* handle, fifo_data_type* p_data, uint16_t len,
                    uint16_t* p_actual)
{
    uint16_t w;
    uint16_t r;
    uint16_t count;
    uint16_t r_idx;
    uint16_t space_to_end;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || len == 0)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    r = BUFF_LOAD_RELAXED(handle->r_ptr);

    count = (uint16_t)(w - r);
    if (count == 0)
        return BUFF_ERR_EMPTY;
    if (len > count)
        len = count;

    r_idx = (uint16_t)(r & handle->mask);
    space_to_end = (uint16_t)(handle->size - r_idx);

    if (space_to_end >= len)
    {
        BUFF_MEM_COPY(p_data, &handle->buf[r_idx], len * sizeof(fifo_data_type));
    }
    else
    {
        BUFF_MEM_COPY(p_data, &handle->buf[r_idx], space_to_end * sizeof(fifo_data_type));
        BUFF_MEM_COPY(p_data + space_to_end, &handle->buf[0],
                      (len - space_to_end) * sizeof(fifo_data_type));
    }

    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + len));
    if (p_actual)
        *p_actual = len;
    return BUFF_OK;
}

int fifo_get_count(struct fifo_spsc* handle, uint16_t* p_count)
{
    uint16_t w;
    uint16_t r;

    if (!handle || !p_count)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    *p_count = (uint16_t)(w - r);
    return BUFF_OK;
}

int fifo_isempty(struct fifo_spsc* handle, bool* p_empty)
{
    uint16_t r;
    uint16_t w;

    if (!handle || !p_empty)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_RELAXED(handle->r_ptr);
    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    *p_empty = (r == w);
    return BUFF_OK;
}

int fifo_isfull(struct fifo_spsc* handle, bool* p_full)
{
    uint16_t r;
    uint16_t w;

    if (!handle || !p_full)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    w = BUFF_LOAD_RELAXED(handle->w_ptr);
    *p_full = ((uint16_t)(w - r) >= handle->size);
    return BUFF_OK;
}

/*=================================================================================================================================================*/
/* 统一环形缓冲区 SPSC无锁实现 (BUFF: 元素宽度可指定, 字节流/帧/任意定长项) */
/*=================================================================================================================================================*/

int fifo_uni_init(struct fifo_uni_spsc* handle, void* buf, uint16_t item_size, uint16_t item_count)
{
    /**< 防御性空指针 / 零宽度 / 2的幂次方 拦截 */
    if (!handle || !buf || item_size == 0 || item_count == 0 ||
        (item_count & (item_count - 1)) != 0)
        return BUFF_ERR_INVAL;

    handle->buf = (uint8_t*)buf;
    handle->size = item_count;
    handle->item_size = item_size;
    handle->mask = (uint16_t)(item_count - 1);

    BUFF_STORE_RELEASE(handle->w_ptr, 0);
    BUFF_STORE_RELEASE(handle->r_ptr, 0);
    return BUFF_OK;
}

int fifo_uni_write_block(struct fifo_uni_spsc* handle, const void* p_data, uint16_t count,
                         uint16_t* p_actual)
{
    uint16_t r;
    uint16_t w;
    uint16_t free_count;
    uint32_t bytes;
    uint32_t w_off;
    uint32_t space_to_end;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || count == 0)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    w = BUFF_LOAD_RELAXED(handle->w_ptr);

    free_count = (uint16_t)(handle->size - (uint16_t)(w - r));
    if (free_count == 0)
        return BUFF_ERR_FULL;

    if (count > free_count)
        count = free_count;

    /**< 计算物理字节偏移以及映射到连续线性末尾的实际空间 (字节) */
    bytes = (uint32_t)count * handle->item_size;
    w_off = (uint32_t)(w & handle->mask) * handle->item_size;
    space_to_end = ((uint32_t)handle->size * handle->item_size) - w_off;

    if (space_to_end >= bytes)
    {
        BUFF_MEM_COPY(&handle->buf[w_off], p_data, bytes);
    }
    else
    {
        BUFF_MEM_COPY(&handle->buf[w_off], p_data, space_to_end);
        BUFF_MEM_COPY(&handle->buf[0], (const uint8_t*)p_data + space_to_end, bytes - space_to_end);
    }

    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + count));
    if (p_actual)
        *p_actual = count;
    return BUFF_OK;
}

int fifo_uni_read_block(struct fifo_uni_spsc* handle, void* p_data, uint16_t count,
                        uint16_t* p_actual)
{
    uint16_t w;
    uint16_t r;
    uint16_t avail;
    uint32_t bytes;
    uint32_t r_off;
    uint32_t space_to_end;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || count == 0)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    r = BUFF_LOAD_RELAXED(handle->r_ptr);

    avail = (uint16_t)(w - r);
    if (avail == 0)
        return BUFF_ERR_EMPTY;
    if (count > avail)
        count = avail;

    /**< 计算物理字节偏移以及映射到连续线性末尾的实际空间 (字节) */
    bytes = (uint32_t)count * handle->item_size;
    r_off = (uint32_t)(r & handle->mask) * handle->item_size;
    space_to_end = ((uint32_t)handle->size * handle->item_size) - r_off;

    if (space_to_end >= bytes)
    {
        BUFF_MEM_COPY(p_data, &handle->buf[r_off], bytes);
    }
    else
    {
        BUFF_MEM_COPY(p_data, &handle->buf[r_off], space_to_end);
        BUFF_MEM_COPY((uint8_t*)p_data + space_to_end, &handle->buf[0], bytes - space_to_end);
    }

    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + count));
    if (p_actual)
        *p_actual = count;
    return BUFF_OK;
}

int fifo_uni_write_acquire(struct fifo_uni_spsc* handle, void** p_slot)
{
    uint16_t r;
    uint16_t w;

    if (!handle || !p_slot)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    w = BUFF_LOAD_RELAXED(handle->w_ptr);

    /**< 利用 uint16_t 溢出特性，已用空间就是纯粹的 w - r */
    if ((uint16_t)(w - r) >= handle->size)
        return BUFF_ERR_FULL;

    /**< 不推进写指针, 与 fifo_uni_write_commit 配对发布 */
    *p_slot = &handle->buf[(uint32_t)(w & handle->mask) * handle->item_size];
    return BUFF_OK;
}

int fifo_uni_write_commit(struct fifo_uni_spsc* handle)
{
    uint16_t w;

    if (!handle)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_RELAXED(handle->w_ptr);
    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + 1));
    return BUFF_OK;
}

int fifo_uni_read_peek(struct fifo_uni_spsc* handle, void** p_slot)
{
    uint16_t w;
    uint16_t r;

    if (!handle || !p_slot)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    r = BUFF_LOAD_RELAXED(handle->r_ptr);
    if (r == w)
        return BUFF_ERR_EMPTY;

    /**< 不推进读指针, 与 fifo_uni_read_release 配对消费 */
    *p_slot = &handle->buf[(uint32_t)(r & handle->mask) * handle->item_size];
    return BUFF_OK;
}

int fifo_uni_read_release(struct fifo_uni_spsc* handle)
{
    uint16_t r;

    if (!handle)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_RELAXED(handle->r_ptr);
    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + 1));
    return BUFF_OK;
}

int fifo_uni_get_count(struct fifo_uni_spsc* handle, uint16_t* p_count)
{
    uint16_t w;
    uint16_t r;

    if (!handle || !p_count)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    *p_count = (uint16_t)(w - r);
    return BUFF_OK;
}

int fifo_uni_isempty(struct fifo_uni_spsc* handle, bool* p_empty)
{
    uint16_t r;
    uint16_t w;

    if (!handle || !p_empty)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_RELAXED(handle->r_ptr);
    w = BUFF_LOAD_ACQUIRE(handle->w_ptr);
    *p_empty = (r == w);
    return BUFF_OK;
}

int fifo_uni_isfull(struct fifo_uni_spsc* handle, bool* p_full)
{
    uint16_t r;
    uint16_t w;

    if (!handle || !p_full)
        return BUFF_ERR_INVAL;

    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    w = BUFF_LOAD_RELAXED(handle->w_ptr);
    *p_full = ((uint16_t)(w - r) >= handle->size);
    return BUFF_OK;
}
