/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usb_bus.h
 *@brief usb bus 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   USB BUS — USB 总线子系统 bus 层
 *   架构: VFS → [Bus (本文件)] → HAL(基建) + TinyUSB(协议/端点)
 *   职责: host/client 池 + atomic ref_count + controller_ops +
 *   CDC/ECM/HID 分类 I/O; TinyUSB 经 usb_tusb_port 调用 (与 mini_tree osal 隔离)
 *   隔离: 未定义 USB_BUS_IMPL 时 #pragma GCC poison 禁止外部调本层符号;
 *   允许 config 类型供 VFS 填充, 强制走 usb_bus API
 *   引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY
 *   @see bus/bus.h  通用总线框架
 *   @=========================================================================================================================
 */

#ifndef USB_BUS_H
#define USB_BUS_H

#include "compiler_compat.h"
#include "hal_usb.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;
    struct usb_bus_client;

    /** @brief USB class 客户端类型 (与 DTS compatible 对应) */
    enum usb_client_class
    {
        USB_CLIENT_CDC = 0, /**< CDC ACM 虚拟串口 */
        USB_CLIENT_ECM = 1, /**< CDC-ECM 虚拟网卡 */
        USB_CLIENT_HID = 2, /**< HID */
    };

    /*===========================================================================================================================================================*/
    /* Host API (VFS 层调用) */
    /*===========================================================================================================================================================*/
    /**
     * @brief USB host 初始化 (cfg 由 VFS 从 DTSI 直投填充, bus 零翻译)
     * @param[in] pdev controller device (host)
     * @param[in] cfg host 配置
     * @return VFS_OK 或 VFS_ERR_*
     */
    int usb_bus_host_init(struct device* pdev, const struct hal_usb_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief USB host 反初始化 (仍有 client 引用时返回 BUSY)
     * @param[in] pdev controller device (host)
     * @return VFS_OK / VFS_ERR_BUSY / VFS_ERR_*
     */
    int usb_bus_host_deinit(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;

    /*===========================================================================================================================================================*/
    /* Client API (VFS 层调用) */
    /*===========================================================================================================================================================*/
    /**
     * @brief 注册 USB class client, 绑定 parent host, ref_count +1
     * @param[in] pdev client device
     * @param[in] cls CDC / ECM / HID
     * @param[out] out 输出 client 句柄
     * @return VFS_OK 或 VFS_ERR_*
     */
    int usb_bus_client_register(struct device* pdev, enum usb_client_class cls, struct usb_bus_client** out) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 注销 USB class client, ref_count -1
     * @param[in] pdev client device
     */
    void usb_bus_client_unregister(struct device* pdev);

    /**
     * @brief 打开 client (幂等)
     * @param[in] pdev client device
     * @return VFS_OK 或 VFS_ERR_*
     */
    int usb_bus_open(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 client (幂等)
     * @param[in] pdev client device
     * @return VFS_OK 或 VFS_ERR_*
     */
    int usb_bus_close(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 解析传输路径 (AUTO/POLL/DMA)
     * @param[in] client_or_host client 或 host device
     * @param[in] xfer_mode HAL_USB_XFER_*
     * @return HAL_USB_XFER_POLL / HAL_USB_XFER_DMA, 或负数 VFS_ERR_*
     */
    int usb_bus_resolve_xfer_mode(struct device* client_or_host, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 推进 TinyUSB 事件队列 (主循环或调度任务周期调用)
     */
    void usb_bus_task(void);

    /**
     * @brief CDC ACM 写
     * @param[in] pdev client
     * @param[in] buf 数据
     * @param[in] len 长度
     * @param[in] timeout_ms 超时毫秒; 0 表示非阻塞尝试
     * @param[in] xfer_mode HAL_USB_XFER_*
     * @return 已写字节数, 或负数 VFS_ERR_*
     */
    int usb_bus_cdc_write(struct device* pdev, const void* buf, size_t len, uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief CDC ACM 读
     * @param[in] pdev client device
     * @param[out] buf 接收缓冲区
     * @param[in] len 缓冲区长度
     * @param[in] timeout_ms 超时毫秒; 0 表示非阻塞尝试
     * @param[in] xfer_mode HAL_USB_XFER_*
     * @return 已读字节数, 或负数 VFS_ERR_*
     */
    int usb_bus_cdc_read(struct device* pdev, void* buf, size_t len, uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief CDC-ECM 发送一帧
     * @param[in] pdev client device
     * @param[in] frame 帧数据 (MAC 层, 不含 FCS)
     * @param[in] len 帧长度
     * @param[in] timeout_ms 超时毫秒; 0 表示非阻塞尝试
     * @param[in] xfer_mode HAL_USB_XFER_*
     * @return 帧长或 VFS_ERR_*
     */
    int usb_bus_ecm_write(struct device* pdev, const void* frame, size_t len, uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief CDC-ECM 接收一帧
     * @param[in] pdev client device
     * @param[out] frame 接收帧缓冲区
     * @param[in] len 缓冲区长度
     * @param[in] timeout_ms 超时毫秒; 0 表示非阻塞尝试
     * @param[in] xfer_mode HAL_USB_XFER_*
     * @return 帧长 / 0(无数据且非阻塞) / 负数 VFS_ERR_*
     */
    int usb_bus_ecm_read(struct device* pdev, void* frame, size_t len, uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief HID 发送 report
     * @param[in] pdev client device
     * @param[in] report report 数据
     * @param[in] len report 长度
     * @param[in] timeout_ms 超时毫秒; 0 表示非阻塞尝试
     * @param[in] xfer_mode HAL_USB_XFER_*
     * @return 已发字节数或 VFS_ERR_*
     */
    int usb_bus_hid_write(struct device* pdev, const void* report, size_t len, uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef USB_BUS_IMPL
#pragma GCC poison usb_bus_host_init usb_bus_host_deinit
#pragma GCC poison usb_bus_client_register usb_bus_client_unregister
#pragma GCC poison usb_bus_open usb_bus_close usb_bus_task usb_bus_resolve_xfer_mode
#pragma GCC poison usb_bus_cdc_write usb_bus_cdc_read
#pragma GCC poison usb_bus_ecm_write usb_bus_ecm_read usb_bus_hid_write
#endif

#endif /* USB_BUS_H */
