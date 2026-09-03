/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file tcp_client.h
 *@brief tcp Client (lwIP netconn 驱动)
 */
#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "buffer.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <string.h>
#ifndef CONFIG_TCP_CLIENT_RX_BUFFER_SIZE
#define TCP_CLIENT_RX_BUFFER_SIZE 1024
#else
#define TCP_CLIENT_RX_BUFFER_SIZE CONFIG_TCP_CLIENT_RX_BUFFER_SIZE
#endif

#ifndef CONFIG_TCP_CLIENT_TX_BUFFER_SIZE
#define TCP_CLIENT_TX_BUFFER_SIZE 1024
#else
#define TCP_CLIENT_TX_BUFFER_SIZE CONFIG_TCP_CLIENT_TX_BUFFER_SIZE
#endif

/**@brief TCP 客户端接收和发送数据的字节的颗粒度 */
#ifndef CONFIG_TCP_CLIENT_RX_TX_BYTE_TYPE
#define TCP_CLIENT_RX_TX_BYTE_TYPE 1
#else
#define TCP_CLIENT_RX_TX_BYTE_TYPE CONFIG_TCP_CLIENT_RX_TX_BYTE_TYPE
#endif

static const char* k_tag = "tcp_client";
/**
 * @brief TCP 客户端上下文结构体
 */
struct tcp_client_context
{
    const char*          server_ip;                            /**< 服务器 IP 地址字符串 */
    uint16_t             port;                                 /**< 服务器端口 */
    struct tcp_pcb*      pcb;                                  /**< TCP 控制块 */
    volatile bool        is_connected;                         /**< 是否已连接 */
    struct fifo_uni_spsc rx_fifo;                              /**< 接收 FIFO */
    struct fifo_uni_spsc tx_fifo;                              /**< 发送 FIFO */
    uint8_t              tx_buffer[TCP_CLIENT_TX_BUFFER_SIZE]; /**< 发送物理缓冲区 */
    uint8_t              rx_buffer[TCP_CLIENT_RX_BUFFER_SIZE]; /**< 接收物理缓冲区 */
};

/**
 * @brief 初始化客户端上下文并连接服务器
 * @param[in,out] ctx        客户端上下文指针
 * @param[in]     server_ip  服务器 IP 字符串 (如 "192.168.1.100")
 * @param[in]     port       服务器端口
 * @return ERR_OK 成功发起连接, 其它为 lwIP 错误码
 */
int tcp_client_init_and_connect(struct tcp_client_context* ctx, const char* server_ip, uint16_t port);

/**
 * @brief 发送数据（将数据写入 TX FIFO 并尝试推送给底层 TCP）
 * @param[in] ctx   客户端上下文
 * @param[in] data  待发送数据
 * @param[in] len   数据长度
 * @param[out] sent_len 返回实际发送的字节数
 * @return ERR_OK 成功, 其它为错误码
 */
int tcp_client_send(struct tcp_client_context* ctx, const void* data, uint16_t len, uint16_t* sent_len);

/**
 * @brief 从接收缓冲读取数据
 * @param[in]  ctx      客户端上下文
 * @param[out] buf      目标存储缓冲
 * @param[in]  len      期望读取的最大长度
 * @param[out] recv_len 实际读取到的字节数
 * @return ERR_OK 成功, 其它为错误码
 */
int tcp_client_read(struct tcp_client_context* ctx, void* buf, uint16_t len, uint16_t* recv_len);

/**
 * @brief 主动断开连接并清理 PCB
 * @param[in] ctx   客户端上下文
 * @return ERR_OK 成功, 其它为错误码
 */
int tcp_client_disconnect(struct tcp_client_context* ctx);

/**
 * @brief 触发刷新 TX FIFO 中排队的数据到网卡 (可在定时器或主循环中周期调用实际是调用tcp_write
 * 只是发到缓冲区)
 * @param[in] ctx   客户端上下文
 * @return ERR_OK 成功, 其它为错误码
 * @note 限制了最大发送块大小为 128 字节
 * (合适大小不建议去修改里面的逻辑了除非你真的需要更大的块大小)
 */
int tcp_client_poll_send(struct tcp_client_context* ctx);
#ifdef __cplusplus
}
#endif
#endif