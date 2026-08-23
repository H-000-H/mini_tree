/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usb_net_cb.c
 *@brief usb net cb 实现
 *@author H-000-H
 *@details
 *   usb_net_cb.c — TinyUSB 网络 class (ECM/RNDIS) 板级数据面
 *   实现 bus/usb 契约头 usb_tusb_port.h 的帧符号:
 *   1. SPSC 帧队列：
 *      - 复用 buffer.h 统一 FIFO (fifo_uni, item_size=帧, 零拷贝 acquire/commit + peek/release)。
 *      - 中断上下文中入队时间确定（Deterministic O(1)）。
 *   2. DMA 内存安全：
 *      - 接收缓冲静态分配且 4 字节对齐, 帧宽 sizeof(帧) 为 4 的倍数, 逐槽对齐成立。
 *   3. 回调传参：
 *      - 利用 tud_network_xmit 传递上下文参数，消除全局状态耦合。
 *   注意: 本文件不 include system_log.h, 避免引入 mini_tree 的 osal.h
 *   与 TinyUSB 的 osal/osal.h (guard 不同名) 在同一编译单元冲突。
 */
#include "buffer.h"
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
_Static_assert((USB_NET_QUEUE_DEPTH & USB_NET_QUEUE_MASK) == 0U,
               "CONFIG_USB_NET_DEPTH must be a power of 2");
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

/** @brief 静态接收帧队列: 统一 FIFO, 元素 = 完整帧 (SPSC: 生产 = tud_network_recv_cb, 消费 =
 * usb_net_frame_pop_rx) */
static struct fifo_uni_spsc s_rx_fifo;
static uint8_t s_rx_ring[USB_NET_QUEUE_DEPTH * sizeof(struct ubs_net_rx_frame)] COMPAT_ALIGNED(4);

static uint8_t s_tx_buffer[CFG_TUD_NET_MTU] COMPAT_ALIGNED(4);

/**
 * @brief 网卡 MAC 地址定义 (0x02 起头为 locally administered 地址)
 */
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

void tud_network_init_cb()
{
    COMPAT_IGNORE_RESULT(fifo_uni_init(
        &s_rx_fifo, s_rx_ring, (uint16_t)sizeof(struct ubs_net_rx_frame), USB_NET_QUEUE_DEPTH));
}

/**
 * @brief TinyUSB 接收数据包回调函数 (中断 / 核心任务上下文)
 * @param[in] src 接收到的原始以太网数据包指针
 * @param[in] size 接收到的数据字节数
 * @return true: 成功接收; false: 缓存已满丢弃该包
 */
bool tud_network_recv_cb(const uint8_t* src, uint16_t size)
{
    if (!src || size == 0U || size > (uint16_t)CFG_TUD_NET_MTU)
        return false;

    /* 零拷贝获取可写帧槽, 队列满则拒收 */
    void* slot_raw = NULL;
    if (fifo_uni_write_acquire(&s_rx_fifo, &slot_raw) != BUFF_OK)
        return false;
    struct ubs_net_rx_frame* slot = (struct ubs_net_rx_frame*)slot_raw;

    /* 先拷数据与长度, 再 commit 发布 (release 内存序由 fifo 内部保证) */
    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(slot->data, src, size));
    slot->len = size;
    COMPAT_IGNORE_RESULT(fifo_uni_write_commit(&s_rx_fifo));
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
        return MINI_ERR_INVAL;

    start_time = osal_time_ms();

    /* 等待 USB 枚举就绪且硬件端点可发 */
    while (!tud_ready() || !tud_network_can_xmit((uint16_t)len))
    {
        if ((osal_time_ms() - start_time) > USB_NET_TX_TIMEOUT_MS)
            return MINI_ERR_TIMEOUT;
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
    if (!frame || len == 0U)
        return MINI_ERR_INVAL;

    /* 零拷贝察视可读帧槽, 队列空直接返回 */
    void* slot_raw = NULL;
    if (fifo_uni_read_peek(&s_rx_fifo, &slot_raw) != BUFF_OK)
        return 0;
    struct ubs_net_rx_frame* slot = (struct ubs_net_rx_frame*)slot_raw;

    uint16_t frame_len = slot->len;
    if ((size_t)frame_len > len)
    {
        /* 调用方缓冲放不下: 丢弃该帧避免死队, 调用方应加大接收缓冲 */
        COMPAT_IGNORE_RESULT(fifo_uni_read_release(&s_rx_fifo));
        return MINI_ERR_NOSPC;
    }

    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(frame, slot->data, frame_len));
    COMPAT_IGNORE_RESULT(fifo_uni_read_release(&s_rx_fifo));
    return (int)frame_len;
}