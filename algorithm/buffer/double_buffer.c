/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file double_buffer.c
 *@brief 双缓冲实现 — 读写分离, swap 切换
 *@author H-000-H
 *@details
 *   @note        适用于 DMA 采集 + CPU 处理并行场景; 见 buffer.h
 */

#include "buffer.h"

void double_buffer_init(struct double_buffer_spsc* handle, double_buffer_data_type* buf1, double_buffer_data_type* buf2, uint16_t size)
{
    if (!handle || !buf1 || !buf2 || size == 0)
        return;

    handle->buf1 = buf1;
    handle->buf2 = buf2;
    handle->size = size;
    handle->mask = size - 1;
    handle->w_ptr = 0;
    handle->r_ptr = 0;
}