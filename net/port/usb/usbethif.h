/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usbethif.h
 *@brief USB ECM/RNDIS 网卡 lwIP 接口适配 (usb_ethif)
 *@author H-000-H
 *@details
 *   将 USB 网卡 (ECM/RNDIS) 桥接到 lwIP netif: 收帧/发帧/轮询均由本层适配。
 *   通过 VFS 获取 USB 以太网 device, 存于 netif->state 供回调用。
 */
#ifndef USBETHIF_H
#define USBETHIF_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "lwip/netif.h"
    /**
     * @brief USB → lwIP 收帧入口 (被后台任务 / 裸机 poll 调用)
     * @param[in] netif lwIP 网络接口
     * @param[in] frame 收到的以太网帧数据
     * @param[in] len   帧长度
     * @return 0: 成功; 其他: 失败
     */
    int usb_ethif_input(struct netif* netif, const uint8_t* frame, size_t len);

    /**
     * @brief lwIP netif 初始化回调
     * @param[in] netif lwIP 网络接口结构体 (state 已含 USB 网卡 device)
     * @return ERR_OK: 成功; 其他: 失败
     */
    err_t usb_ethif_init(struct netif* netif);

    /**
     * @brief 注册 USB 网卡 lwIP 接口 (走 VFS 拿 device, 存进 netif->state)
     * @param[in] dev_name USB 以太网设备 label (与 dts/devtable 对应)
     * @return 0: 成功; -1: 失败
     */
    int usb_ethif_init_dev(const char* dev_name);

#if NO_SYS == 1
    /**
     * @brief 裸机 usb 网卡轮询函数，从 ECM 链路拉帧送入 lwIP
     * @param[in] netif lwIP 网络接口
     * @return 0: 没有接收到数据包; 1: 处理了一帧
     */
    int usb_ethif_poll(struct netif* netif);
#endif /* NO_SYS */
#ifdef __cplusplus
}
#endif
#endif
