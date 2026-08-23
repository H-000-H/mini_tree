/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file net_error.h
 * @brief 网络上层协议模块 (transport / mqtt / http) 统一错误码
 * @author H-000-H
 * @details 上层协议包装层对外一律返回 NET_OK / NET_ERR_* (负 errno 语义),
 *          与底层 lwip err_t 及内核错误码数值解耦; 底层错误在包装层边界翻译。
 */
#ifndef NET_ERROR_H
#define NET_ERROR_H

#include <errno.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NET_OK 0 /**< 成功 */
#define NET_ERR_INVAL (-EINVAL) /**< 入参非法 (指针为 NULL、长度为 0、枚举越界) */
#define NET_ERR_CONN (-ENOTCONN) /**< 链路错误 (未连接即收发 / 对端断开 / 建连失败) */
#define NET_ERR_TIMEOUT (-ETIMEDOUT) /**< 等待超时 (TCP 建连 / 响应接收 / TX FIFO 腾挪) */
#define NET_ERR_NOSPC (-ENOSPC) /**< 缓冲不足 (请求头组装 / 报文序列化放不下) */
#define NET_ERR_STATE (-EISCONN) /**< 状态冲突 (已连接/建连中重复发起连接) */

#ifdef __cplusplus
}
#endif
#endif /* NET_ERROR_H */
