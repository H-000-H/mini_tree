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

void double_buffer_init(struct double_buffer_spsc* handle, double_buffer_data_type* buf1, double_buffer_data_type* buf2, uint16_t size)
{
    /**< 防御性空指针与 2的幂次方 拦截 */
    if (!handle || !buf1 || !buf2 || size == 0 || (size & (size - 1)) != 0)
        return;

    handle->buf1 = buf1;
    handle->buf2 = buf2;
    handle->size = size;
    handle->mask = size - 1;

    /**< w_ptr=0 写侧空; r_ptr=size 读侧空 (首次 swap 前读侧无数据) */
    BUFF_STORE_RELEASE(handle->w_ptr, 0);
    BUFF_STORE_RELEASE(handle->r_ptr, size);
}

bool double_buffer_write_data(struct double_buffer_spsc* handle, double_buffer_data_type data)
{
    if (!handle)
        return false;

    uint16_t w = BUFF_LOAD_RELAXED(handle->w_ptr);
    if (w >= handle->size)
        return false; /**< 写缓冲满, 等待消费者读空读侧后由下次写入触发 swap */

    handle->buf1[w] = data;
    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + 1));

    /**< 写满 且 读侧已读空 → 交换读写角色 */
    if ((uint16_t)(w + 1) == handle->size && BUFF_LOAD_ACQUIRE(handle->r_ptr) == handle->size)
        double_buffer_swap(handle);

    return true;
}

bool double_buffer_read_data(struct double_buffer_spsc* handle, double_buffer_data_type* p_data)
{
    if (!handle || !p_data)
        return false;

    /**< acquire 加载: 生产者 swap 时会复位读指针并交换缓冲字段 */
    uint16_t r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    if (r >= handle->size)
        return false; /**< 读侧空 (含首次 swap 前) */

    *p_data = handle->buf2[r];
    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + 1));
    return true;
}

uint16_t double_buffer_write_block(struct double_buffer_spsc* handle, const double_buffer_data_type* p_data, uint16_t len)
{
    if (!handle || !p_data || len == 0)
        return 0;

    uint16_t w = BUFF_LOAD_RELAXED(handle->w_ptr);
    uint16_t free_len = handle->size - w;

    if (len > free_len)
        len = free_len;
    if (len == 0)
        return 0;

    BUFF_MEM_COPY(&handle->buf1[w], p_data, len * sizeof(double_buffer_data_type));
    BUFF_STORE_RELEASE(handle->w_ptr, (uint16_t)(w + len));

    /**< 写满 且 读侧已读空 → 交换读写角色 */
    if ((uint16_t)(w + len) == handle->size && BUFF_LOAD_ACQUIRE(handle->r_ptr) == handle->size)
        double_buffer_swap(handle);

    return len;
}

uint16_t double_buffer_read_block(struct double_buffer_spsc* handle, double_buffer_data_type* p_data, uint16_t len)
{
    if (!handle || !p_data || len == 0)
        return 0;

    /**< acquire 加载: 生产者 swap 时会复位读指针并交换缓冲字段 */
    uint16_t r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    uint16_t avail = handle->size - r;

    if (len > avail)
        len = avail;
    if (len == 0)
        return 0;

    BUFF_MEM_COPY(p_data, &handle->buf2[r], len * sizeof(double_buffer_data_type));
    BUFF_STORE_RELEASE(handle->r_ptr, (uint16_t)(r + len));
    return len;
}

uint16_t double_buffer_get_count(struct double_buffer_spsc* handle)
{
    if (!handle)
        return 0;
    /**< 可读量 = size - 读侧进度 (读侧整块换入, 恒为 size 深) */
    uint16_t r = BUFF_LOAD_ACQUIRE(handle->r_ptr);
    return (uint16_t)(handle->size - r);
}

bool double_buffer_isempty(struct double_buffer_spsc* handle)
{
    if (!handle)
        return true;
    return BUFF_LOAD_ACQUIRE(handle->r_ptr) >= handle->size;
}

bool double_buffer_isfull(struct double_buffer_spsc* handle)
{
    if (!handle)
        return false;
    /**< 写侧无空间即满 (能否 swap 取决于读侧, 查询只报写侧状态) */
    return BUFF_LOAD_ACQUIRE(handle->w_ptr) >= handle->size;
}

/* ============================================================================
 * 1.4 双缓冲独立读写 (dual_buffer) — 同时收发不阻塞, 线性缓冲无环绕
 *
 * 与 double_buffer_spsc 的区别: 两缓冲各自有独立读写指针, 无交换门槛,
 * 生产者写满一个自动切到另一个 (不等读空), 消费者读空一个自动切到另一个 (不等写满),
 * 适合连续流 (音频/传感器采样) 场景, 读写速率不等也不会互相阻塞。
 * ========================================================================== */

/**
 * @brief 初始化双缓冲独立读写结构体 (线性缓冲, 可指定元素宽度)
 *
 * @param handle     结构体指针 (不能为 NULL)
 * @param buffer1    缓冲区 1 起始地址 (须满足 size * item_size 字节)
 * @param buffer2    缓冲区 2 起始地址 (同上)
 * @param item_size  单个元素宽度 (字节)
 * @param item_count 单个缓冲区容量 (元素个数, 须为 2 的幂)
 */
void dual_buffer_init(struct dual_buffer_spsc* handle, void* buffer1, void* buffer2, uint16_t item_size, uint16_t item_count)
{
    /**< 防御性空指针与 2的幂次方 拦截 */
    if (!handle || !buffer1 || !buffer2 || item_size == 0 || item_count == 0 || (item_count & (item_count - 1)) != 0)
        return;

    handle->buf1 = buffer1;
    handle->buf2 = buffer2;
    handle->size = item_count;
    handle->item_size = item_size;
    handle->mask = item_count - 1; /**< 线性缓冲不用, 保留兼容 */
    handle->active_w = 0;
    handle->active_r = 0;

    /**< 四个指针全部复位为 0 (两缓冲均空) */
    BUFF_STORE_RELEASE(handle->w1, 0);
    BUFF_STORE_RELEASE(handle->r1, 0);
    BUFF_STORE_RELEASE(handle->w2, 0);
    BUFF_STORE_RELEASE(handle->r2, 0);
}

/**
 * @brief 块写入双缓冲 (轮流写两个, 无阻塞, 满则切到另一个)
 *
 * @param handle   结构体指针 (不能为 NULL)
 * @param p_data   待写入数据数组 (元素个数 >= count)
 * @param count    待写入元素个数 (>0)
 * @return 实际写入元素个数 (若两缓冲都满则返回 0)
 */
uint16_t dual_buffer_write_block(struct dual_buffer_spsc* handle, const void* p_data, uint16_t count)
{
    if (!handle || !p_data || count == 0)
        return 0;

    uint16_t written = 0;
    uint8_t* buf = (handle->active_w == 0) ? handle->buf1 : handle->buf2;
    uint16_t w = (handle->active_w == 0) ? BUFF_LOAD_RELAXED(handle->w1) : BUFF_LOAD_RELAXED(handle->w2);

    /**< 先写当前活跃缓冲 */
    uint16_t free_len = handle->size - w;
    uint16_t to_write = (count < free_len) ? count : free_len;
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
        free_len = handle->size - w;
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

    return written;
}

/**
 * @brief 块读取双缓冲 (轮流读两个, 无阻塞, 空则切到另一个)
 *
 * @param handle   结构体指针 (不能为 NULL)
 * @param p_data   读取目标数组 (元素个数 >= count)
 * @param count    待读取元素个数 (>0)
 * @return 实际读取元素个数 (若两缓冲都空则返回 0)
 */
uint16_t dual_buffer_read_block(struct dual_buffer_spsc* handle, void* p_data, uint16_t count)
{
    if (!handle || !p_data || count == 0)
        return 0;

    uint16_t read = 0;
    uint8_t* buf = (handle->active_r == 0) ? handle->buf1 : handle->buf2;
    uint16_t w = (handle->active_r == 0) ? BUFF_LOAD_ACQUIRE(handle->w1) : BUFF_LOAD_ACQUIRE(handle->w2);
    uint16_t r = (handle->active_r == 0) ? BUFF_LOAD_RELAXED(handle->r1) : BUFF_LOAD_RELAXED(handle->r2);

    /**< 先读当前活跃缓冲 */
    uint16_t avail = (uint16_t)(w - r);
    uint16_t to_read = (count < avail) ? count : avail;
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

    return read;
}

/**
 * @brief 查询双缓冲是否全部写满 (两个都满)
 *
 * @param handle 结构体指针 (不能为 NULL)
 * @return 两缓冲都满返回 true, 否则返回 false
 */
bool dual_buffer_isfull(const struct dual_buffer_spsc* handle)
{
    if (!handle)
        return false;
    uint16_t w1 = BUFF_LOAD_ACQUIRE(handle->w1);
    uint16_t w2 = BUFF_LOAD_ACQUIRE(handle->w2);
    return (w1 >= handle->size) && (w2 >= handle->size);
}

/**
 * @brief 查询双缓冲是否全部为空 (两个都空)
 *
 * @param handle 结构体指针 (不能为 NULL)
 * @return 两缓冲都空返回 true, 否则返回 false
 */
bool dual_buffer_isempty(const struct dual_buffer_spsc* handle)
{
    if (!handle)
        return true;
    uint16_t w1 = BUFF_LOAD_ACQUIRE(handle->w1);
    uint16_t r1 = BUFF_LOAD_ACQUIRE(handle->r1);
    uint16_t w2 = BUFF_LOAD_ACQUIRE(handle->w2);
    uint16_t r2 = BUFF_LOAD_ACQUIRE(handle->r2);
    return (w1 == r1) && (w2 == r2);
}

/**
 * @brief 获取双缓冲当前已存元素总数 (两个之和)
 *
 * @param handle 结构体指针 (不能为 NULL)
 * @return 已存元素总数 (0 到 2 * size)
 */
uint16_t dual_buffer_get_count(const struct dual_buffer_spsc* handle)
{
    if (!handle)
        return 0;
    uint16_t w1 = BUFF_LOAD_ACQUIRE(handle->w1);
    uint16_t r1 = BUFF_LOAD_ACQUIRE(handle->r1);
    uint16_t w2 = BUFF_LOAD_ACQUIRE(handle->w2);
    uint16_t r2 = BUFF_LOAD_ACQUIRE(handle->r2);
    return (uint16_t)((uint16_t)(w1 - r1) + (uint16_t)(w2 - r2));
}
