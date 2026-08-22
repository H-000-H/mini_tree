/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file can_hook.h
 *@brief can hook 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   CAN_HOOK — Classic CAN 协议超集钩子 (COMPAT_WEAK)
 *   VFS 开闭读写一律经这些钩子（不是单独「hook 模式」）：
 *   无强符号 → 弱默认透传 = 普通 Classic CAN
 *   有强符号 → 同一路径叠加过滤/改写等扩展
 *   不是第二条总线；参数面不同于 DTSI 硬件配置。
 *   @=========================================================================================================================
 */

#ifndef CAN_HOOK_H
#define CAN_HOOK_H

#include "compiler_compat.h"
#include "hal_can.h"
#include "status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;

    /**
     * @brief 打开设备钩子 (VFS open 路径触发, weak 默认透传)
     * @param[in] pdev CAN 设备对象指针
     * @return 成功返回 VFS_OK, 拦截失败返回负数错误码
     */
    int can_hook_on_open(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭设备钩子 (VFS close 路径触发)
     * @param[in] pdev CAN 设备对象指针
     * @return 成功返回 VFS_OK, 拦截失败返回负数错误码
     */
    int can_hook_on_close(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 发送前置钩子 (帧可被改写/拦截)
     * @param[in] pdev CAN 设备对象指针
     * @param[in,out] frame 待发送帧 (钩子可修改)
     * @return 成功返回 VFS_OK, 拦截丢弃返回负数错误码
     */
    int can_hook_pre_tx(struct device* pdev, struct can_frame* frame) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 发送后置钩子 (发送结果通知)
     * @param[in] pdev CAN 设备对象指针
     * @param[in] frame 已发送帧
     * @param[in] tx_ret 底层发送返回值
     * @return 成功返回 VFS_OK, 失败返回负数错误码
     */
    int can_hook_post_tx(struct device* pdev, const struct can_frame* frame, int tx_ret) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 接收过滤钩子 (决定帧是否接受)
     * @param[in] pdev CAN 设备对象指针
     * @param[in] frame 接收帧
     * @return 匹配返回 VFS_OK, 过滤丢弃返回负数错误码
     */
    int can_hook_filter_match(struct device* pdev, const struct can_frame* frame) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 接收钩子 (帧可被改写)
     * @param[in] pdev CAN 设备对象指针
     * @param[in,out] frame 接收帧 (钩子可修改)
     * @return 成功返回 VFS_OK, 丢弃返回负数错误码
     */
    int can_hook_on_rx(struct device* pdev, struct can_frame* frame) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 错误钩子 (总线错误/收发失败通知)
     * @param[in] pdev CAN 设备对象指针
     * @param[in] err 错误码
     * @return 成功返回 VFS_OK, 失败返回负数错误码
     */
    int can_hook_on_err(struct device* pdev, int err) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* CAN_HOOK_H */
