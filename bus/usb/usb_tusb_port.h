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
    /**
     * @brief 初始化 TinyUSB 协议栈 (平台实现)
     * @param[in] rhport 根 Hub 端口号
     * @return 成功返回 true, 失败返回 false
     */
    bool usb_tusb_init(uint8_t rhport);
    /**
     * @brief 推进 TinyUSB 主任务 (主循环周期调用, 平台实现)
     */
    void usb_tusb_task(void); /*必须实现的*/
    /**
     * @brief USB 中断入口 (ISR 调用, 平台实现)
     * @param[in] rhport 根 Hub 端口号
     */
    void usb_tusb_int_handler(uint8_t rhport);

    /* CDC ACM 数据通路 */
    /**
     * @brief 查询 CDC ACM 是否已连接 (平台实现)
     * @return 已连接返回 true
     */
    bool usb_tusb_cdc_connected(void);
    /**
     * @brief CDC ACM 写 (平台实现)
     * @param[in] buf 数据缓冲区
     * @param[in] len 字节数
     * @return 实际写入字节数
     */
    uint32_t usb_tusb_cdc_write(const void* buf, uint32_t len);
    /**
     * @brief 强制刷新 CDC ACM 发送缓冲 (平台实现)
     */
    void usb_tusb_cdc_write_flush(void);
    /**
     * @brief 查询 CDC ACM 接收可用字节数 (平台实现)
     * @return 可读取字节数
     */
    uint32_t usb_tusb_cdc_available(void);
    /**
     * @brief CDC ACM 读 (平台实现)
     * @param[out] buf 接收缓冲区
     * @param[in] len 缓冲区长度
     * @return 实际读取字节数
     */
    uint32_t usb_tusb_cdc_read(void* buf, uint32_t len);

    /* HID 数据通路 */
    /**
     * @brief 查询 HID 是否就绪 (平台实现)
     * @return 就绪返回 true
     */
    bool usb_tusb_hid_ready(void);
    /**
     * @brief 发送 HID report (平台实现)
     * @param[in] report_id report ID
     * @param[in] report report 数据
     * @param[in] len report 长度
     * @return 成功返回 true, 失败返回 false
     */
    bool usb_tusb_hid_report(uint8_t report_id, const void* report, uint16_t len);

    /**
     * @brief 推送一帧到 ECM TX 队列 (平台实现)
     * @param[in] frame 帧数据
     * @param[in] len 帧长度
     * @return 成功返回 VFS_OK, 队列满返回 VFS_ERR_NOMEM
     */
    int usb_net_frame_push_tx(const void* frame, size_t len);
    /**
     * @brief 从 ECM RX 队列弹出一帧 (平台实现)
     * @param[out] frame 帧缓冲区
     * @param[in] len 缓冲区长度
     * @return 成功返回 VFS_OK, 无数据返回 VFS_ERR_AGAIN
     */
    int usb_net_frame_pop_rx(void* frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_TUSB_PORT_H */
