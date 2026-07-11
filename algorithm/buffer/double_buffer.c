/* SPDX-License-Identifier: Apache-2.0 */
#include "buffer.h"

void double_buffer_init(struct double_buffer_spsc* handle, Double_Buffer_Data_type* buf1, Double_Buffer_Data_type* buf2, uint16_t size)
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