/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file tcp_server.h
 * @brief TCP 服务器头文件
 */
#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include "lwip/err.h"
#include "lwip/tcp.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief 最大并发客户端数由 Kconfig TCP_SERVER_MAX_CLIENTS 配置 (见 config.h) */

    /**
     * @brief 初始化并启动多客户端 TCP 服务端
     * @param[in] port 监听端口
     * @return ERR_OK 成功，其它为 lwIP 错误码
     */
    int tcp_server_init(int port);

    /**
     * @brief 读取指定客户端 session 的接收数据
     * @note SPSC 约束: 同一 session_id 只能由一个消费者线程调用, 多线程读同一 session
     * 会产生数据竞争
     * @param[in]  session_id 客户端会话 ID (0 ~ CONFIG_TCP_SERVER_MAX_CLIENTS - 1)
     * @param[out] buf        接收目标缓冲区
     * @param[in]  len        期望读取的最大长度
     * @param[out] recv_len   实际读取到的字节数
     * @return ERR_OK 成功, ERR_ARG 参数错误, ERR_CONN 客户端未连接或已断开
     */
    int get_tcp_data_by_session(int session_id, char* buf, int len, int* recv_len);

    /**
     * @brief 读取 TCP 接收数据 (兼容单客户端用法)
     * @note 从第一个已连接的 session 读取; 多客户端同时在线时请改用 get_tcp_data_by_session
     * @note SPSC 约束: 不要与其他线程同时读取同一 session (同 get_tcp_data_by_session)
     * @param[out] buf       接收目标缓冲区
     * @param[in]  len       期望读取的最大长度
     * @param[out] recv_len  实际读取到的字节数
     * @return ERR_OK 成功, ERR_ARG 参数错误, ERR_CONN 无已连接客户端
     */
    int get_tcp_data(char* buf, int len, int* recv_len);

    /**
     * @brief 查询指定 session 是否处于连接活跃状态
     * @param[in] session_id 客户端会话 ID
     * @return true 已连接, false 空闲/已断开
     */
    bool is_session_connected(int session_id);

    /**
     * @brief 主动断开指定 session 的客户端连接
     * @param[in] session_id 客户端会话 ID
     * @return ERR_OK 成功, ERR_ARG 参数错误
     */
    int close_session(int session_id);

    /**
     * @brief 向指定 session 的客户端发送数据
     * @note SPSC 约束: 同一 session_id 只能由一个生产者线程调用 (与接收侧消费者独立)
     * @param[in] session_id 客户端会话 ID (0 ~ CONFIG_TCP_SERVER_MAX_CLIENTS - 1)
     * @param[in] data       待发送数据 (内部拷贝, 返回后可释放)
     * @param[in] len        数据长度
     * @return int 成功为实际写入字节数 (可能因发送窗口不足小于 len, 需稍后重试);
     *         失败为负 lwIP 错误码 (ERR_ARG 参数错误 / ERR_CONN 未连接)
     */
    int tcp_server_send(int session_id, const void* data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* TCP_SERVER_H_ */