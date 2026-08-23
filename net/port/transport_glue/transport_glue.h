/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file transport_glue.h
 * @brief coreMQTT / coreHTTP 共享传输胶水层
 * @author H-000-H
 * @details FreeRTOS core 库 (coreMQTT / coreHTTP) 使用同一套传输抽象:
 *          TransportInterface_t = send/recv 函数指针 + NetworkContext_t。
 *          本模块把该接口实现在 tcp_client 的 FIFO 之上, MQTT 与 HTTP
 *          包装层共用; 加密通道 (mqtts/https) 不走本层, 由各自的包装层
 *          直接基于 lwIP altcp_tls 封装。
 *          主 API 走 NET_* 错误码 + 出参; send/recv 仅为满足 core 库
 *          函数指针签名的薄适配, 返回值语义由库契约规定。
 */
#ifndef NET_TRANSPORT_GLUE_H
#define NET_TRANSPORT_GLUE_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "net_error.h"
#include "tcp/tcp_client.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETWORK_TRANSPORT_TX_TIMEOUT_MS 5000 /**< 等待 TX FIFO 容纳全部待发数据的上限 (ms) */

    /**
     * @brief core 库传输层网络上下文 (应用层定义, 与库头声明的不完全类型对应)
     */
    struct NetworkContext
    {
        struct tcp_client_context* tcp_client; /**< 底层 TCP 通道 (归调用方所有) */
        uint32_t recv_timeout_ms; /**< recv 等待上限 (ms): 0 表示无数据立即返回 */
    };

    /**
     * @brief 发起底层 TCP 建连 (异步: 三次握手在回调上下文完成)
     * @param[in] context   网络上下文 (须已填入 tcp_client 指针)
     * @param[in] server_ip 服务器 IP 字符串 (点分十进制)
     * @param[in] port      服务器端口
     * @return int NET_OK 已发起; NET_ERR_INVAL 入参非法; NET_ERR_CONN 发起失败
     */
    int network_transport_connect(struct NetworkContext* context, const char* server_ip,
                                  uint16_t port);

    /**
     * @brief 断开底层 TCP 连接
     * @param[in] context 网络上下文
     * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
     */
    int network_transport_disconnect(struct NetworkContext* context);

    /**
     * @brief 查询底层链路是否已建立
     * @param[in] context 网络上下文
     * @return bool TCP 连接是否存活
     */
    bool network_transport_is_connected(struct NetworkContext* context);

    /**
     * @brief 查询底层建连是否已失败 (err 回调置空控制块)
     * @param[in] context 网络上下文
     * @return bool true 表示链路已失败, 无需再等待
     */
    bool network_transport_link_failed(struct NetworkContext* context);

    /**
     * @brief 传输层写入 (把数据全部压入底层 TX FIFO)
     * @param[in] context 网络上下文
     * @param[in] buffer  待发送数据
     * @param[in] length  数据长度
     * @return int NET_OK 全部写入; NET_ERR_INVAL 入参非法;
     *             NET_ERR_CONN 链路已断; NET_ERR_TIMEOUT 等待 FIFO 腾挪超时
     */
    int network_transport_write(struct NetworkContext* context, const void* buffer, size_t length);

    /**
     * @brief 传输层读取 (从底层 RX FIFO 取数据)
     * @param[in]  context     网络上下文
     * @param[out] buffer      接收缓冲
     * @param[in]  length      期望最大读取长度
     * @param[out] read_length 实际读到的字节数 (0 表示等待窗口内无数据)
     * @return int NET_OK 读取完成 (可能 0 字节); NET_ERR_INVAL 入参非法;
     *             NET_ERR_CONN 链路已断
     * @note 无数据时按 recv_timeout_ms 轮询等待并让出调度 (裸机协作式),
     *       保证其他任务 (网卡收包泵) 有机会运行
     */
    int network_transport_read(struct NetworkContext* context, void* buffer, size_t length,
                               size_t* read_length);

    /**
     * @brief core 库 TransportSend_t 签名适配 (内部转发 network_transport_write)
     * @note 返回值语义由 core 库契约规定: >=0 已写字节数, -1 失败
     */
    int32_t network_transport_send(struct NetworkContext* context, const void* buffer,
                                   size_t length);

    /**
     * @brief core 库 TransportRecv_t 签名适配 (内部转发 network_transport_read)
     * @note 返回值语义由 core 库契约规定: >0 读到字节数, 0 暂无数据, -1 链路错误
     */
    int32_t network_transport_recv(struct NetworkContext* context, void* buffer, size_t length);
#ifdef __cplusplus
}
#endif
#endif /* NET_TRANSPORT_GLUE_H */
