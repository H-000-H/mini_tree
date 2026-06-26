#ifndef M_BUFFER_H
#define M_BUFFER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*
 * 警告: 无锁 SPSC（单生产者单消费者）环形缓冲区。
 *   - 对恰好一个写入线程和一个读取线程是线程安全的。
 *   - 严重: 禁止在没有外部互斥锁的情况下允许多个线程并发写入或读取！
 *   - 内存序: acquire/release 协议。已在弱内存一致性双核 SMP（Xtensa / ARM Cortex-A）上验证。
 */

typedef int16_t Fifo_Data_type;

/*
 * Cache Line 隔离 (False-Sharing 防御):
 *   w_ptr 由 Producer Core 专写, r_ptr 由 Consumer Core 专读.
 *   中间 32 字节 padding 强制两者落入不同 cache line,
 *   杜绝 SMP 缓存一致性协议引发的 cache line ping-pong.
 */
struct fifo_spsc
{
    Fifo_Data_type* buf;
    uint16_t w_ptr;
    uint8_t _pad1[32];
    uint16_t r_ptr;
    uint8_t _pad2[32];
    uint16_t size;
};

void fifo_init(struct fifo_spsc* handle, Fifo_Data_type* buf, uint16_t size);

bool fifo_write_data(struct fifo_spsc* handle, Fifo_Data_type data);

bool fifo_read_data(struct fifo_spsc* handle, Fifo_Data_type* p_data);

uint16_t fifo_write_block(struct fifo_spsc* handle, const Fifo_Data_type* p_data, uint16_t len);

uint16_t fifo_read_block(struct fifo_spsc* handle, Fifo_Data_type* p_data, uint16_t len);

bool fifo_isfull(struct fifo_spsc* handle);

bool fifo_isempty(struct fifo_spsc* handle);

uint16_t fifo_get_count(struct fifo_spsc* handle);

#ifdef __cplusplus
}
#endif
#endif