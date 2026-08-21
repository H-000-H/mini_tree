/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file can_bus.h
 *@brief can bus 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   CAN BUS — CAN 总线子系统 bus 层
 *   架构: VFS → [Bus (本文件)] → HAL; hal_can_bus_host 嵌入 can_bus_host (无 vtable)
 *   职责: host/client 池 + atomic ref_count + controller_ops (host 生命周期) +
 *   client I/O (open/close/transmit/receive/filter)
 *   隔离: 未定义 CAN_BUS_IMPL 时 #pragma GCC poison 禁止外部调 hal_can_*;
 *   允许 config 类型供 VFS 填充, 强制走 can_bus API
 *   引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY
 *   @see bus/bus.h  通用总线框架
 *   @=========================================================================================================================
 */

#ifndef CAN_BUS_H
#define CAN_BUS_H

#include "compiler_compat.h"
#include "hal_can.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;
    struct can_bus_client;

    /*===========================================================================================================================================================*/
    /*Host API (VFS 层调用)*/
    /*===========================================================================================================================================================*/
    /**
     * @brief CAN host 初始化 (config 类型直接用 hal_can_bus_config, bus 零翻译透传)
     * @param pdev controller device (host)
     * @param cfg host 配置 (VFS 填充 DTSI 硬件直投值)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int can_bus_host_init(struct device* pdev,
                          const struct hal_can_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief CAN host 反初始化 (ref_count > 0 时返回 BUSY)
     * @param pdev controller device (host)
     * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
     */
    int can_bus_host_deinit(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /*===========================================================================================================================================================*/

    /*Client API (VFS 层调用)*/
    /*===========================================================================================================================================================*/
    /**
     * @brief CAN client 注册 (CAN 无设备级配置, 仅绑定 host)
     * @param pdev client device
     * @param out 输出 can_bus_client 指针
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int can_bus_client_register(struct device* pdev,
                                struct can_bus_client** out) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 注销 CAN client 并递减 host 引用计数 (ref_count -1, 清零槽位)
     * @param pdev client device
     */
    void can_bus_client_unregister(struct device* pdev);

    /**
     * @brief 打开 CAN client 硬件 (幂等)
     * @param pdev client device
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int can_bus_open(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 CAN client 硬件 (幂等)
     * @param pdev client device
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int can_bus_close(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief CAN 发送一帧
     * @param pdev client device
     * @param frame 待发送帧
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int can_bus_transmit(struct device* pdev, const struct can_frame* frame,
                         uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief CAN 从指定 FIFO 接收一帧
     * @param pdev client device
     * @param frame 输出帧
     * @param fifo 接收 FIFO 编号 (0 / 1)
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT, 失败返回 VFS_ERR_*
     */
    int can_bus_receive(struct device* pdev, struct can_frame* frame, uint32_t fifo,
                        uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 配置 CAN 过滤器
     * @param pdev client device
     * @param filter 过滤器配置
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int can_bus_filter_config(struct device* pdev,
                              const struct hal_can_filter_config* filter) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 查询 CAN 控制器状态 (error-active / passive / bus-off / stopped)
     * @param pdev client device
     * @param out_state 输出 HAL_CAN_STATE_*
     */
    int can_bus_get_state(struct device* pdev, uint32_t* out_state) COMPAT_WARN_UNUSED_RESULT;
    /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#ifndef CAN_BUS_IMPL
/* 禁止 bus 层外部直接调用 HAL 函数 — 强制走 can_bus API。
 * 允许 config 类型 (hal_can_bus_config, hal_can_filter_config, can_frame 等)
 * 供 VFS 层填充 DTSI 值。 */
#pragma GCC poison hal_can_bus_host_init hal_can_bus_host_deinit
#pragma GCC poison hal_can_dev_init hal_can_dev_deinit
#pragma GCC poison hal_can_dev_hw_open hal_can_dev_hw_close
#pragma GCC poison hal_can_transmit hal_can_receive hal_can_filter_config hal_can_get_state
#endif

#endif /* CAN_BUS_H */
