/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file tusb_config.h
 *@brief TinyUSB 适配层 — USB 端口配置与板级数据面
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_
#ifdef __cplusplus
extern "C" {
#endif
#include "config.h"
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU CONFIG_USB_TUSB_MCU
#endif

/* USB 端口0的模式配置: 主机/设备/双角色 */
#if CONFIG_USB_RHPORT0_HOST
#define CFG_TUSB_RHPORT0_MODE  (OPT_MODE_HOST | CFG_USB_RHPORT0_SPEED_BITS)
#else
#define CFG_TUSB_RHPORT0_MODE  (OPT_MODE_DEVICE | CFG_USB_RHPORT0_SPEED_BITS)
#endif

/* USB 端口0的速度配置 */
#if CONFIG_USB_RHPORT0_SPEED_HIGH
#define CFG_USB_RHPORT0_SPEED_BITS  OPT_MODE_HIGH_SPEED
#elif CONFIG_USB_RHPORT0_SPEED_FULL
#define CFG_USB_RHPORT0_SPEED_BITS  OPT_MODE_FULL_SPEED
#else
#define CFG_USB_RHPORT0_SPEED_BITS  OPT_MODE_DEFAULT_SPEED
#endif

#define CFG_TUSB_OS CONFIG_USB_TUSB_OS /* TinyUSB OS 适配层: none / freertos / cmsis_rtos2 / rtosal */

#if CONFIG_USB_NET
#if CONFIG_USB_NET_RNDIS
#define CFG_TUD_NET_RNDIS 1
#else
#define CFG_TUD_NET_ECM 1
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif /* TUSB_CONFIG_H_ */
