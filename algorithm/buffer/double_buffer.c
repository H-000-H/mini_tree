/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file double_buffer.c
 *@brief 双缓冲实现 — 读写分离, swap 切换
 *@author H-000-H
 *@details
 *   @note        适用于 DMA 采集 + CPU 处理并行场景; 见 buffer.h
 *   @note        acquire/release 内存序保证单生产者单消费者安全;
 *                buf1 为写侧、buf2 为读侧, 仅生产者在「写缓冲满 且 读缓冲读空」
 *                时驱动 swap (buf1/buf2 字段只有生产者写), 消费者每拍重新加载指针
 *   @note        全部接口返回 BUFF_* 错误码; 长度类结果经指针参数回传
 */

#include "buffer.h"

/**
 * @brief 写满且读空时交换读写缓冲 (仅生产者调用, 生产侧驱动)
 * @param[in] handle 双缓冲句柄
 */
static void double_buffer_swap(struct double_buffer_spsc* handle)
{
    double_buffer_data_type* tmp = handle->buf1;
    handle->buf1 = handle->buf2;
    handle->buf2 = tmp;

    /**< 先复位读指针再复位写指针, 均以 release 发布, 保证消费者看到数据与字段 */
    BUFF_STORE_RELEASE(handle->r_ptr, 0);
    BUFF_STORE_RELEASE(handle->w_ptr, 0);
}

int double_buffer_init(struct double_buffer_spsc* handle, double_buffer_data_type* buf1, double_buffer_data_type* buf2, uint16_t size)
{
    /**< 防御性空指针与 2的幂次方 拦截 */
    if (!handle || !buf1 || !buf2 || size == 0 || (size & (size - 1)) != 0)
        return BUFF_ERR_INVAL;

    handle->buf1 = buf1;
    handle->buf2 = buf2;
    handle->size = size;
    handle->mask = (uint16_t)(size - 1);

    /**< w_ptr=0 写侧空; r_ptr=size 读侧空 (首次 swap 前读侧无数据) */
    BUFF_STORE_RELEASE(handle->w_ptr, 0);
    BUFF_STORE_RELEASE(handle->r_ptr, size);
    return BUFF_OK;
}

int double_buffer_write_data(struct double_buffer_spsc* handle, double_buffer_data_type data)
{
    uint16_t w;

    if (!handle)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_RELAXED(handle->w_ptr);
    if (w >= handle->size)
        return BUFF_ERR_FULL; /**< 写缓冲满, 等待消费者读空读侧后由下次写入触发 swap */

    handle->buf1[w] = data;
    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + 1));

    /**< 写满 且 读侧已读空 → 交换读写角色 */
    if ((uint16_t)(w + 1) == handle->size && BUFF_LOAD_ACQUIRE(handle->r_ptr) == handle->size)
        double_buffer_swap(handle);

    return BUFF_OK;
}

int double_buffer_read_data(struct double_buffer_spsc* handle, double_buffer_data_type* p_data)
{
    uint16_t r;

    if (!handle || !p_data)
        return BUFF_ERR_INVAL;

    /**< acquire 加载: 生产者 swap 时会复位读指针并交换缓冲字段 */
    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    if (r >= handle->size)
        return BUFF_ERR_EMPTY; /**< 读侧空 (含首次 swap 前) */

    *p_data = handle->buf2[r];
    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + 1));
    return BUFF_OK;
}

int double_buffer_write_block(struct double_buffer_spsc* handle, const double_buffer_data_type* p_data, uint16_t len, uint16_t* p_actual)
{
    uint16_t w;
    uint16_t free_len;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || len == 0)
        return BUFF_ERR_INVAL;

    w = BUFF_LOAD_RELAXED(handle->w_ptr);
    free_len = (uint16_t)(handle->size - w);
    if (free_len == 0)
        return BUFF_ERR_FULL;

    if (len > free_len)
        len = free_len;

    BUFF_MEM_COPY(&handle->buf1[w], p_data, len * sizeof(double_buffer_data_type));
    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + len));

    /**< 写满 且 读侧已读空 → 交换读写角色 */
    if ((uint16_t)(w + len) == handle->size && BUFF_LOAD_ACQUIRE(handle->r_ptr) == handle->size)
        double_buffer_swap(handle);

    if (p_actual)
        *p_actual = len;
    return BUFF_OK;
}

int double_buffer_read_block(struct double_buffer_spsc* handle, double_buffer_data_type* p_data, uint16_t len, uint16_t* p_actual)
{
    uint16_t r;
    uint16_t avail;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || len == 0)
        return BUFF_ERR_INVAL;

    /**< acquire 加载: 生产者 swap 时会复位读指针并交换缓冲字段 */
    r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    avail = (uint16_t)(handle->size - r);
    if (avail == 0)
        return BUFF_ERR_EMPTY;

    if (len > avail)
        len = avail;

    BUFF_MEM_COPY(p_data, &handle->buf2[r], len * sizeof(double_buffer_data_type));
    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + len));
    if (p_actual)
        *p_actual = len;
    return BUFF_OK;
}

int double_buffer_get_count(struct double_buffer_spsc* handle, uint16_t* p_count)
{
    if (!handle || !p_count)
        return BUFF_ERR_INVAL;
    /**< 可读量 = size - 读侧进度 (读侧整块换入, 恒为 size 深) */
    *p_count = (uint16_t)(handle->size - BUFF_LOAD_ACQUIRE(handle->r_ptr));
    return BUFF_OK;
}

int double_buffer_isempty(struct double_buffer_spsc* handle, bool* p_empty)
{
    if (!handle || !p_empty)
        return BUFF_ERR_INVAL;
    *p_empty = (BUFF_LOAD_ACQUIRE(handle->r_ptr) >= handle->size);
    return BUFF_OK;
}

int double_buffer_isfull(struct double_buffer_spsc* handle, bool* p_full)
{
    if (!handle || !p_full)
        return BUFF_ERR_INVAL;
    /**< 写侧无空间即满 (能否 swap 取决于读侧, 查询只报写侧状态) */
    *p_full = (BUFF_LOAD_ACQUIRE(handle->w_ptr) >= handle->size);
    return BUFF_OK;
}

/* ============================================================================
 * 1.4 双缓冲独立读写 (dual_buffer) — 同时收发不阻塞, 线性缓冲无环绕
 *
 * 与 double_buffer_spsc 的区别: 两缓冲各自有独立读写指针, 无交换门槛,
 * 生产者写满一个自动切到另一个 (不等读空), 消费者读空一个自动切到另一个 (不等写满),
 * 适合连续流 (音频/传感器采样) 场景, 读写速率不等也不会互相阻塞。
 * ========================================================================== */

int dual_buffer_init(struct dual_buffer_spsc* handle, void* buffer1, void* buffer2, uint16_t item_size, uint16_t item_count)
{
    /**< 防御性空指针与 2的幂次方 拦截 */
    if (!handle || !buffer1 || !buffer2 || item_size == 0 || item_count == 0 || (item_count & (item_count - 1)) != 0)
        return BUFF_ERR_INVAL;

    handle->buf1 = buffer1;
    handle->buf2 = buffer2;
    handle->size = item_count;
    handle->item_size = item_size;
    handle->mask = (uint16_t)(item_count - 1); /**< 线性缓冲不用, 保留兼容 */
    handle->active_w = 0;
    handle->active_r = 0;

    /**< 四个指针全部复位为 0 (两缓冲均空) */
    BUFF_STORE_RELEASE(handle->w1, 0);
    BUFF_STORE_RELEASE(handle->r1, 0);
    BUFF_STORE_RELEASE(handle->w2, 0);
    BUFF_STORE_RELEASE(handle->r2, 0);
    return BUFF_OK;
}

int dual_buffer_write_block(struct dual_buffer_spsc* handle, const void* p_data, uint16_t count, uint16_t* p_actual)
{
    uint16_t written = 0;
    uint8_t* buf;
    uint16_t w;
    uint16_t free_len;
    uint16_t to_write;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || count == 0)
        return BUFF_ERR_INVAL;

    buf = (handle->active_w == 0) ? handle->buf1 : handle->buf2;
    w = (handle->active_w == 0) ? BUFF_LOAD_RELAXED(handle->w1) : BUFF_LOAD_RELAXED(handle->w2);

    /**< 先写当前活跃缓冲 */
    free_len = (uint16_t)(handle->size - w);
    to_write = (count < free_len) ? count : free_len;
    if (to_write > 0)
    {
        BUFF_MEM_COPY(buf + (uint32_t)w * handle->item_size, p_data, (uint32_t)to_write * handle->item_size);
        w = (uint16_t)(w + to_write);
        if (handle->active_w == 0)
            BUFF_STORE_RELEASE(handle->w1, w);
        else
            BUFF_STORE_RELEASE(handle->w2, w);
        written = (uint16_t)(written + to_write);
        p_data = (const uint8_t*)p_data + (uint32_t)to_write * handle->item_size;
        count = (uint16_t)(count - to_write);
    }

    /**< 若还有剩余且当前缓冲满, 切到另一个 */
    if (count > 0 && w >= handle->size)
    {
        handle->active_w = (uint8_t)(1 - handle->active_w);
        buf = (handle->active_w == 0) ? handle->buf1 : handle->buf2;
        w = (handle->active_w == 0) ? BUFF_LOAD_RELAXED(handle->w1) : BUFF_LOAD_RELAXED(handle->w2);
        free_len = (uint16_t)(handle->size - w);
        to_write = (count < free_len) ? count : free_len;
        if (to_write > 0)
        {
            BUFF_MEM_COPY(buf + (uint32_t)w * handle->item_size, p_data, (uint32_t)to_write * handle->item_size);
            w = (uint16_t)(w + to_write);
            if (handle->active_w == 0)
                BUFF_STORE_RELEASE(handle->w1, w);
            else
                BUFF_STORE_RELEASE(handle->w2, w);
            written = (uint16_t)(written + to_write);
        }
    }

    if (written == 0)
        return BUFF_ERR_FULL;
    if (p_actual)
        *p_actual = written;
    return BUFF_OK;
}

int dual_buffer_read_block(struct dual_buffer_spsc* handle, void* p_data, uint16_t count, uint16_t* p_actual)
{
    uint16_t read = 0;
    uint8_t* buf;
    uint16_t w;
    uint16_t r;
    uint16_t avail;
    uint16_t to_read;

    if (p_actual)
        *p_actual = 0;
    if (!handle || !p_data || count == 0)
        return BUFF_ERR_INVAL;

    buf = (handle->active_r == 0) ? handle->buf1 : handle->buf2;
    w = (handle->active_r == 0) ? BUFF_LOAD_ACQUIRE(handle->w1) : BUFF_LOAD_ACQUIRE(handle->w2);
    r = (handle->active_r == 0) ? BUFF_LOAD_RELAXED(handle->r1) : BUFF_LOAD_RELAXED(handle->r2);

    /**< 先读当前活跃缓冲 */
    avail = (uint16_t)(w - r);
    to_read = (count < avail) ? count : avail;
    if (to_read > 0)
    {
        BUFF_MEM_COPY(p_data, buf + (uint32_t)r * handle->item_size, (uint32_t)to_read * handle->item_size);
        r = (uint16_t)(r + to_read);
        if (handle->active_r == 0)
            BUFF_STORE_RELEASE(handle->r1, r);
        else
            BUFF_STORE_RELEASE(handle->r2, r);
        read = (uint16_t)(read + to_read);
        p_data = (uint8_t*)p_data + (uint32_t)to_read * handle->item_size;
        count = (uint16_t)(count - to_read);
    }

    /**< 若还有剩余且当前缓冲空, 切到另一个 */
    if (count > 0 && r >= w)
    {
        handle->active_r = (uint8_t)(1 - handle->active_r);
        buf = (handle->active_r == 0) ? handle->buf1 : handle->buf2;
        w = (handle->active_r == 0) ? BUFF_LOAD_ACQUIRE(handle->w1) : BUFF_LOAD_ACQUIRE(handle->w2);
        r = (handle->active_r == 0) ? BUFF_LOAD_RELAXED(handle->r1) : BUFF_LOAD_RELAXED(handle->r2);
        avail = (uint16_t)(w - r);
        to_read = (count < avail) ? count : avail;
        if (to_read > 0)
        {
            BUFF_MEM_COPY(p_data, buf + (uint32_t)r * handle->item_size, (uint32_t)to_read * handle->item_size);
            r = (uint16_t)(r + to_read);
            if (handle->active_r == 0)
                BUFF_STORE_RELEASE(handle->r1, r);
            else
                BUFF_STORE_RELEASE(handle->r2, r);
            read = (uint16_t)(read + to_read);
        }
    }

    if (read == 0)
        return BUFF_ERR_EMPTY;
    if (p_actual)
        *p_actual = read;
    return BUFF_OK;
}

int dual_buffer_isfull(const struct dual_buffer_spsc* handle, bool* p_full)
{
    uint16_t w1;
    uint16_t w2;

    if (!handle || !p_full)
        return BUFF_ERR_INVAL;

    w1 = BUFF_LOAD_ACQUIRE(handle->w1);
    w2 = BUFF_LOAD_ACQUIRE(handle->w2);
    *p_full = (w1 >= handle->size) && (w2 >= handle->size);
    return BUFF_OK;
}

int dual_buffer_isempty(const struct dual_buffer_spsc* handle, bool* p_empty)
{
    uint16_t w1;
    uint16_t r1;
    uint16_t w2;
    uint16_t r2;

    if (!handle || !p_empty)
        return BUFF_ERR_INVAL;

    w1 = BUFF_LOAD_ACQUIRE(handle->w1);
    r1 = BUFF_LOAD_ACQUIRE(handle->r1);
    w2 = BUFF_LOAD_ACQUIRE(handle->w2);
    r2 = BUFF_LOAD_ACQUIRE(handle->r2);
    *p_empty = (w1 == r1) && (w2 == r2);
    return BUFF_OK;
}

int dual_buffer_get_count(const struct dual_buffer_spsc* handle, uint16_t* p_count)
{
    uint16_t w1;
    uint16_t r1;
    uint16_t w2;
    uint16_t r2;

    if (!handle || !p_count)
        return BUFF_ERR_INVAL;

    w1 = BUFF_LOAD_ACQUIRE(handle->w1);
    r1 = BUFF_LOAD_ACQUIRE(handle->r1);
    w2 = BUFF_LOAD_ACQUIRE(handle->w2);
    r2 = BUFF_LOAD_ACQUIRE(handle->r2);
    *p_count = (uint16_t)((uint16_t)(w1 - r1) + (uint16_t)(w2 - r2));
    return BUFF_OK;
}
