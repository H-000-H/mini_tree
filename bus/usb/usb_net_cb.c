/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usb_net_cb.c
 *@brief usb net cb 实现
 *@author H-000-H
 *@details
 *   usb_net_cb.c — TinyUSB 网络 class (ECM/RNDIS) 板级数据面
 *   实现 bus/usb 契约头 usb_tusb_port.h 的帧符号:
 *   1. SPSC 环形缓冲：
 *      - 采用直接头尾游标（head/tail）管理结构体数组，消除辅助索引队列与查找循环。
 *      - 中断上下文中入队时间确定（Deterministic O(1)）。
 *   2. DMA 内存安全：
 *      - 接收与发送静态缓冲区严格按照 4 字节对齐（TU_ATTR_ALIGNED(4)）。
 *   3. 回调传参：
 *      - 利用 tud_network_xmit 传递上下文参数，消除全局状态耦合。
 *   注意: 本文件不 include system_log.h, 避免引入 mini_tree 的 osal.h
 *   与 TinyUSB 的 osal/osal.h (guard 不同名) 在同一编译单元冲突。
 */
#include "class/net/net_device.h"
#include "compiler_compat.h"
#include "device/usbd.h"
#include "status.h"
#include "usb_tusb_port.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uint32_t osal_time_ms(void);
extern void osal_delay_ms(uint32_t ms);

/** @brief 接收队列深度 必须为 2 的幂次 */
#ifndef CONFIG_USB_NET_DEPTH
#define CONFIG_USB_NET_DEPTH 4U
#endif
#define USB_NET_QUEUE_DEPTH CONFIG_USB_NET_DEPTH
#define USB_NET_QUEUE_MASK (USB_NET_QUEUE_DEPTH - 1U)

/** @brief 先保障2的x次方不然后面的代码都没意义 */
_Static_assert((USB_NET_QUEUE_DEPTH & USB_NET_QUEUE_MASK) == 0U, "CONFIG_USB_NET_DEPTH must be a power of 2");
_Static_assert(CFG_TUD_NET_MTU < UINT16_MAX, "CFG_TUD_NET_MTU must lower than UINT16_MAX");
#ifndef CONFIG_USB_NET_TX_TIMEOUT_MS
#define USB_NET_TX_TIMEOUT_MS 50U
#else
#define USB_NET_TX_TIMEOUT_MS CONFIG_USB_NET_TX_TIMEOUT_MS
#endif

/** @brief 接收以太网帧对象 */
struct ubs_net_rx_frame
{
    uint8_t data[CFG_TUD_NET_MTU]; /**< 帧数据区 */
    uint16_t len; /**< 帧有效长度 */
};

/** @brief 静态接收帧环形缓冲区 */
static struct ubs_net_rx_frame s_rx_ring[USB_NET_QUEUE_DEPTH] COMPAT_ALIGNED(4);

/*内部已经原子了这里只是做标记*/
static COMPAT_ATOMIC_UINT16 s_rx_head; /**< 生产者写入(仅 tud_network_recv_cb 修改) */
static COMPAT_ATOMIC_UINT16 s_rx_tail; /**< 消费者读取 (仅 usb_net_frame_pop_rx 修改)*/

static uint8_t s_tx_buffer[CFG_TUD_NET_MTU] COMPAT_ALIGNED(4);

/**
 * @brief 网卡 MAC 地址定义 (0x02 起头为 locally administered 地址)
 */
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

void tud_network_init_cb()
{
    COMPAT_ATOMIC_RUNTIME_INIT(&s_rx_head, 0);
    COMPAT_ATOMIC_RUNTIME_INIT(&s_rx_tail, 0);
}

/**
 * @brief TinyUSB 接收数据包回调函数 (中断 / 核心任务上下文)
 * @param[in] src 接收到的原始以太网数据包指针
 * @param[in] size 接收到的数据字节数
 * @return true: 成功接收; false: 缓存已满丢弃该包
 */
bool tud_network_recv_cb(const uint8_t* src, uint16_t size)
{
    uint16_t head;
    uint16_t next_head;
    uint16_t tail;

    if (!src || size == 0U || size > (uint16_t)CFG_TUD_NET_MTU)
        return false;

    /* 读当前写指针 (生产者私有) */
    head = COMPAT_ATOMIC_LOAD(&s_rx_head, COMPAT_MO_RELAXED);
    next_head = (uint16_t)((head + 1U) & USB_NET_QUEUE_MASK);

    /* 读消费者读指针, 判满 (next_head 追上 tail = 满) */
    tail = COMPAT_ATOMIC_LOAD(&s_rx_tail, COMPAT_MO_ACQUIRE);
    if (next_head == tail)
        return false; /* 环形满, 拒绝 */

    /* 先拷数据, 再发布 len 和 head */
    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(s_rx_ring[head].data, src, size));
    COMPAT_ATOMIC_STORE(&s_rx_ring[head].len, size, COMPAT_MO_RELEASE);
    COMPAT_ATOMIC_STORE(&s_rx_head, next_head, COMPAT_MO_RELEASE);
    return true;
}

/**
 * @brief TinyUSB 发送交付回调函数
 * @param[out] dst USB 硬件 DMA / 端点目标缓冲区
 * @param[in]  ref 用户传入的待发送数据指针
 * @param[in]  arg 用户传入的待发送数据长度
 * @return 实际拷贝交付给硬件的字节数
 */
uint16_t tud_network_xmit_cb(uint8_t* dst, void* ref, uint16_t arg)
{
    if (!dst || !ref || arg == 0)
        return 0;

    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(dst, ref, arg));

    return arg;
}

int usb_net_frame_push_tx(const void* frame, size_t len)
{
    uint32_t start_time;
    if (!frame || len == 0U || len > (size_t)CFG_TUD_NET_MTU)
        return VFS_ERR_INVAL;

    start_time = osal_time_ms();

    /* 等待 USB 枚举就绪且硬件端点可发 */
    while (!tud_ready() || !tud_network_can_xmit((uint16_t)len))
    {
        if ((osal_time_ms() - start_time) > USB_NET_TX_TIMEOUT_MS)
            return VFS_ERR_TIMEOUT;
#if defined(NO_SYS) && (NO_SYS == 1)
        usb_tusb_task();
#else
        osal_delay_ms(1);
#endif
    }
    /* 发送到缓存并通过参数直接传递至回调函数 */
    COMPAT_MEM_COPY(s_tx_buffer, frame, len);
    tud_network_xmit(s_tx_buffer, (uint16_t)len);
    return (int)len;
}

int usb_net_frame_pop_rx(void* frame, size_t len)
{
    uint16_t tail;
    uint16_t head;
    uint16_t frame_len;
    uint16_t next_tail;

    if (!frame || len == 0U)
        return VFS_ERR_INVAL;

    tail = COMPAT_ATOMIC_LOAD(&s_rx_tail, COMPAT_MO_RELAXED);
    head = COMPAT_ATOMIC_LOAD(&s_rx_head, COMPAT_MO_ACQUIRE);
    if (tail == head)
        return 0;

    frame_len = COMPAT_ATOMIC_LOAD(&s_rx_ring[tail].len, COMPAT_MO_RELAXED);
    next_tail = (uint16_t)((tail + 1U) & USB_NET_QUEUE_MASK);
    if ((size_t)frame_len > len)
    {
        COMPAT_ATOMIC_STORE(&s_rx_tail, next_tail, COMPAT_MO_RELEASE);
        return VFS_ERR_NOSPC;
    }

    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(frame, s_rx_ring[tail].data, frame_len));
    COMPAT_ATOMIC_STORE(&s_rx_tail, next_tail, COMPAT_MO_RELEASE);
    return (int)frame_len;
}