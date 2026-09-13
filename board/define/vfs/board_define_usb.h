/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_usb.h
 *@brief board define usb 头文件
 *@author H-000-H
 *@details
 *   USB VFS 板级配置宏 (vfs/usb) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_USB_H
#define BOARD_DEFINE_USB_H

/* host 池 = DTS "mt-usb-otg-host" 节点数 (缺省 1) */
/* include the dtc-lite generated truth table first: otherwise the default
   value below conflicts with the real one (macro redefinition) */
#if defined(__has_include)
#if __has_include("dt_config_gen.h")
#include "dt_config_gen.h"
#endif
#endif
#ifndef DTC_GEN_COUNT_MT_USB_OTG_HOST
#define DTC_GEN_COUNT_MT_USB_OTG_HOST 1
#endif
#ifndef USB_VFS_PRIV_COUNT
#define USB_VFS_PRIV_COUNT DTC_GEN_COUNT_MT_USB_OTG_HOST
#endif

/* client 池 = CDC-ACM + CDC-ECM + HID 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_MT_USB_CDC_ACM
#define DTC_GEN_COUNT_MT_USB_CDC_ACM 1
#endif
#ifndef DTC_GEN_COUNT_MT_USB_CDC_ECM
#define DTC_GEN_COUNT_MT_USB_CDC_ECM 1
#endif
#ifndef DTC_GEN_COUNT_MT_USB_HID
#define DTC_GEN_COUNT_MT_USB_HID 1
#endif
#ifndef USB_VFS_CLIENT_COUNT
#define USB_VFS_CLIENT_COUNT (DTC_GEN_COUNT_MT_USB_CDC_ACM + DTC_GEN_COUNT_MT_USB_CDC_ECM + DTC_GEN_COUNT_MT_USB_HID)
#endif

#endif /* BOARD_DEFINE_USB_H */
