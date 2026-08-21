/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usb_net_cb.c
 *@brief usb net cb 实现
 *@author H-000-H
 *@details
 *   usb_net_cb.c — TinyUSB 网络 class (ECM/RNDIS) 板级数据面
 *   实现 bus/usb 契约头 usb_tusb_port.h 的帧符号:
 *   usb_net_frame_push_tx / usb_net_frame_pop_rx
 *   及 TinyUSB 网络应用回调 (tud_network_*)。
 *   帧格式: 标准以太网帧 。
 *   NO_SYS 单线程: 收帧入静态环形队列, 发帧用静态缓冲 + 同步等待。
 */

#include "buffer.h"
#include "status.h"
#include "system_log.h"
#include "tusb.h"
#include "usb_tusb_port.h"
#include <string.h>

#ifndef CONFIG_USB_NET_DEPTH
#define CONFIG_USB_NET_DEPTH 4U
#endif
#define USB_NET_QUEUE_DEPTH CONFIG_USB_NET_DEPTH

static const char* const k_tag = "usb_net";
struct usb_net_rx_frame
{
    uint8_t data[CFG_TUD_N]
}
