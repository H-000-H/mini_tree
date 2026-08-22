/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file tcp_server.h
 *@brief tcp Server (lwIP netconn 驱动)
 */
#ifndef TCP_SERVER_H
#define TCP_SERVER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
    /**
     *@brief 初始化 TCP 服务端 (创建监听 socket 并绑定端口)
     *@param[in] port 监听端口号
     *@return 成功返回 VFS_OK, 失败返回负数错误码
     */
    int tcp_server_init(int port);

    /**
     *@brief 阻塞接收 TCP 客户端数据
     *@param[out] buf 接收数据缓冲区
     *@param[in] len 缓冲区容量
     *@param[out] recv_len 回传实际接收到的数据长度
     *@return 成功返回 VFS_OK, 连接关闭返回 VFS_ERR_NODEV, 失败返回负数错误码
     */
    int get_tcp_data(char* buf, int len, int* recv_len);
#ifdef __cplusplus
}
#endif
#endif