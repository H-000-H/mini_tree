/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-usb.h
 *@brief vfs-usb 头文件
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   USB VFS — USB 子系统 VFS 层
 *   Driver:
 *   usb-otg-host                         — host (DTSI → hal / tusb_init)
 *   heterogeneous,usb-cdc-acm            — CDC ACM 虚拟串口
 *   heterogeneous,usb-cdc-ecm            — CDC-ECM 虚拟网卡
 *   heterogeneous,usb-hid                — HID
 *   write/read 默认 USB_XFER_AUTO; ioctl SET_XFER_MODE / GET_XFER_MODE 切换 POLL/DMA。
 *   生命周期: open/close 引用与 remove drain 走 dev_lifecycle。
 *   --------------------------------------------------------------------------
 */

#ifndef USB_VFS_H
#define USB_VFS_H

#include "compiler_compat.h"
#include "device.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 推进 TinyUSB 事件 (应用主循环应周期调用)
     */
    void usb_bus_task(void);

    /* -------------------------------------------------------------------------- */
    /* ioctl */
/* -------------------------------------------------------------------------- */
#define USB_CMD_BASE MINI_MAGIC(USB)
#define USB_CMD_SET_XFER_MODE (USB_CMD_BASE + 0x01) /**< 设置后续 write/read 的 xfer_mode */
#define USB_CMD_GET_XFER_MODE (USB_CMD_BASE + 0x02) /**< 查询当前 xfer_mode */
#define USB_CMD_COUNT 2

/** 与 HAL_USB_XFER_* 同值 */
#define USB_XFER_AUTO 0U /**< 隐式: dma_enable 非 0 则 DMA, 否则 poll */
#define USB_XFER_POLL 1U /**< 强制普通路径 */
#define USB_XFER_DMA 2U /**< 强制 DMA; host 未开 dma-enable 则 NOTSUPP */

    /**
     * @brief 传输模式参数 (SET_XFER_MODE / GET_XFER_MODE)
     */
    struct usb_xfer_mode_arg
    {
        uint32_t xfer_mode; /**< USB_XFER_AUTO / POLL / DMA */
    };

#ifdef __cplusplus
}
#endif

#endif /* USB_VFS_H */
