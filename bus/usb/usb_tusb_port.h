/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usb_tusb_port.h
 *@brief usb tusb port 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   USB TinyUSB 板级契约头 — 中间件 bus/usb 依赖，实现属平台
 *   中间件 bus/usb 只经本头调用 TinyUSB 粘合层（docs/usb_tusb_port.md）；
 *   板级负责实现全部符号，TinyUSB API 不泄漏进中间件公共头。
 *   本头在中间件（bus/usb/），平台树无需复制。
 *   @=========================================================================================================================
 */

#ifndef USB_TUSB_PORT_H
#define USB_TUSB_PORT_H

#include "compiler_compat.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* TinyUSB 粘合：协议栈入口（tusb_init / tud_task / tud_int_handler） */
    bool usb_tusb_init(uint8_t rhport);
    void usb_tusb_task(void);/*必须实现的*/
    void usb_tusb_int_handler(uint8_t rhport);

    /* CDC ACM 数据通路 */
    bool usb_tusb_cdc_connected(void);
    uint32_t usb_tusb_cdc_write(const void* buf, uint32_t len);
    void usb_tusb_cdc_write_flush(void);
    uint32_t usb_tusb_cdc_available(void);
    uint32_t usb_tusb_cdc_read(void* buf, uint32_t len);

    /* HID 数据通路 */
    bool usb_tusb_hid_ready(void);
    bool usb_tusb_hid_report(uint8_t report_id, const void* report, uint16_t len);

    int usb_net_frame_push_tx(const void* frame, size_t len);
    int usb_net_frame_pop_rx(void* frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_TUSB_PORT_H */
