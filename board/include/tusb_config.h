/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file tusb_config.h
 *@brief TinyUSB 适配层 — USB 端口配置与板级数据面
 *@author H-000-H
 *@details
 *   @note 先决 TinyUSB 端口配置与 class 配置，再由 Kconfig 生成的 config.h 提供数值直配。
 *   @details
 *   1. TinyUSB 端口配置: CFG_TUSB_RHPORT0_MODE / CFG_TUSB_RHPORT0_SPEED / CFG_TUSB_OS
 *   2. TinyUSB 网络 class 配置: CFG_TUD_ECM_RNDIS / CFG_TUD_NCM / CFG_TUD_NET_MTU
 *   3. TinyUSB 其他 class 配置: CFG_TUD_MSC / CFG_TUD_HID / CFG_TUD_VENDOR / CFG_TUD_CDC
 *   4. TinyUSB 端口配置与 class 配置由 Kconfig 生成的 config.h 提供，tusb_config.h 仅做数值直配。
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h" /* IWYU pragma: keep */

/* -------------------------------------------------------------------------- */
/* 1. 硬件芯片平台与操作系统适配                                              */
/* -------------------------------------------------------------------------- */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU                CONFIG_USB_TUSB_MCU
#endif

#if CONFIG_USB_TUSB_OS_FREERTOS
#define CFG_TUSB_OS                 OPT_OS_FREERTOS
#elif CONFIG_USB_TUSB_OS_RTTHREAD
#define CFG_TUSB_OS                 OPT_OS_RTTHREAD
#elif CONFIG_USB_TUSB_OS_THREADX
#define CFG_TUSB_OS                 OPT_OS_THREADX
#else
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              CONFIG_USB_TUSB_DEBUG
#endif

/* -------------------------------------------------------------------------- */
/* 2. 端口速度与运行模式 (先配速度位，再配端口模式)                           */
/* -------------------------------------------------------------------------- */
#if CONFIG_USB_RHPORT0_SPEED_HIGH
#define CFG_USB_RHPORT0_SPEED_BITS  OPT_MODE_HIGH_SPEED
#elif CONFIG_USB_RHPORT0_SPEED_FULL
#define CFG_USB_RHPORT0_SPEED_BITS  OPT_MODE_FULL_SPEED
#else
#define CFG_USB_RHPORT0_SPEED_BITS  OPT_MODE_DEFAULT_SPEED
#endif

#if CONFIG_USB_RHPORT0_HOST
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | CFG_USB_RHPORT0_SPEED_BITS)
#else
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | CFG_USB_RHPORT0_SPEED_BITS)
#endif

/* -------------------------------------------------------------------------- */
/* 3. Device 控制端点与内存对齐                                              */
/* -------------------------------------------------------------------------- */
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE      64
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          TU_ATTR_ALIGNED(4)
#endif

/* -------------------------------------------------------------------------- */
/* 4. 标准设备 Class 使能                                                    */
/* -------------------------------------------------------------------------- */
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC                 CONFIG_USB_MSC
#endif

#ifndef CFG_TUD_HID
#define CFG_TUD_HID                 CONFIG_USB_HID
#endif

#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR              CONFIG_USB_VENDOR
#endif

#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC                 CONFIG_USB_CDC
#endif

#if CFG_TUD_CDC
#define CFG_TUD_CDC_RX_BUFSIZE      CONFIG_USB_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE      CONFIG_USB_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE      CONFIG_USB_CDC_EP_BUFSIZE
#endif

/* -------------------------------------------------------------------------- */
/* 5. 网络 Class (RNDIS / ECM / NCM)                                         */
/* -------------------------------------------------------------------------- */
#if CONFIG_USB_NET
#if CONFIG_USB_NET_RNDIS
#define CFG_TUD_ECM_RNDIS           1
#define CFG_TUD_NCM                 0
#elif CONFIG_USB_NET_NCM
#define CFG_TUD_ECM_RNDIS           0
#define CFG_TUD_NCM                 1
#endif
#define CFG_TUD_NET_MTU             CONFIG_USB_NET_MTU/*单帧以太网数据包最大缓冲区大小*/
#else
#define CFG_TUD_ECM_RNDIS           0
#define CFG_TUD_NCM                 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */