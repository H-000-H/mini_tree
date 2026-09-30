/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file tls_client.h
 * @brief TLS 客户端通道 (lwIP altcp_tls 直连封装)
 * @author H-000-H
 * @note https/mqtts 共用的加密通道: 加密本身全部交给 lwIP 自带的 altcp_tls,
 *       本文件只负责建连和收发数据。
 *       建连是异步的: TCP 握手和 TLS 握手都在回调里完成, connected 回调
 *       触发时加密通道已经就绪; 收到的数据存进 RX 缓冲, 要发的数据先写入
 *       TX 缓冲再分块加密发出。
 *       随机数: mbedtls 靠 mbedtls_hardware_poll() 取随机数, 这里提供的是占位实现;
 *       板上如有硬件随机数发生器, 请用同名函数替换掉它。
 */
#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "buffer.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_TLS_CLIENT_RX_BUFFER_SIZE
#define TLS_CLIENT_RX_BUFFER_SIZE 2048
#else
#define TLS_CLIENT_RX_BUFFER_SIZE CONFIG_TLS_CLIENT_RX_BUFFER_SIZE
#endif

#ifndef CONFIG_TLS_CLIENT_TX_BUFFER_SIZE
#define TLS_CLIENT_TX_BUFFER_SIZE 2048
#else
#define TLS_CLIENT_TX_BUFFER_SIZE CONFIG_TLS_CLIENT_TX_BUFFER_SIZE
#endif

/**
 * @brief TLS 客户端上下文结构体
 */
struct tls_client_context
{
    const char*              server_ip;                            /**< 服务器 IP 地址字符串 */
    uint16_t                 port;                                 /**< 服务器端口 */
    struct altcp_tls_config* tls_config;                           /**< TLS 配置 (里面存着 CA 证书) */
    struct altcp_pcb*        pcb;                                  /**< 连接句柄 */
    volatile bool            is_connected;                         /**< TCP 和 TLS 握手是否都已完成 */
    struct fifo_uni_spsc     rx_fifo;                              /**< 接收缓冲 (已解密的数据) */
    struct fifo_uni_spsc     tx_fifo;                              /**< 发送缓冲 (待发数据, 加密由 altcp 内部处理) */
    uint8_t                  tx_buffer[TLS_CLIENT_TX_BUFFER_SIZE]; /**< 发送物理缓冲区 */
    uint8_t                  rx_buffer[TLS_CLIENT_RX_BUFFER_SIZE]; /**< 接收物理缓冲区 */
};

/**
 * @brief 初始化 TLS 客户端上下文并发起加密连接 (异步: 握手在回调上下文完成)
 * @param[in,out] ctx        客户端上下文指针
 * @param[in]     server_ip  服务器 IP 字符串 (如 "192.168.1.100")
 * @param[in]     port       服务器端口 (https 标准 443 / mqtts 标准 8883)
 * @param[in]     ca_cert    服务器 CA 证书 (PEM, 须含结尾 '\0'), NULL 表示不校验
 *                           证书 (仅调试用, 易受中间人攻击)
 * @param[in]     ca_len     CA 证书长度 (含结尾 '\0'), ca_cert 为 NULL 时无效
 * @return int ERR_OK 成功发起连接, 其它为 lwIP 错误码
 */
int tls_client_init_and_connect(struct tls_client_context* ctx, const char* server_ip, uint16_t port, const uint8_t* ca_cert, uint32_t ca_len);

/**
 * @brief 发送数据 (写入 TX FIFO 并尝试经 TLS 加密推送)
 * @param[in] ctx   客户端上下文
 * @param[in] data  待发送数据
 * @param[in] len   数据长度
 * @param[out] sent_len 实际写入 FIFO 的字节数 (可能小于 len)
 * @return int ERR_OK 成功, ERR_CONN 链路未建立, 其它为错误码
 */
int tls_client_send(struct tls_client_context* ctx, const void* data, uint16_t len, uint16_t* sent_len);

/**
 * @brief 从接收缓冲读取解密后的数据
 * @param[in]  ctx      客户端上下文
 * @param[out] buf      目标存储缓冲
 * @param[in]  len      期望读取的最大长度
 * @param[out] recv_len 实际读取到的字节数
 * @return int ERR_OK 成功, 其它为错误码
 */
int tls_client_read(struct tls_client_context* ctx, void* buf, uint16_t len, uint16_t* recv_len);

/**
 * @brief 主动断开连接并清理 altcp 控制块与 TLS 配置
 * @param[in] ctx 客户端上下文
 * @return int ERR_OK 成功, 其它为错误码
 */
int tls_client_disconnect(struct tls_client_context* ctx);

/**
 * @brief 把 TX 缓冲里排队的数据加密发出去 (主循环周期调用)
 * @param[in] ctx 客户端上下文
 * @return int ERR_OK 成功, 其它为错误码
 * @note 可发送长度会自动扣除 TLS 本身的开销; 缓冲满了就等下次再发
 */
int tls_client_poll_send(struct tls_client_context* ctx);
#ifdef __cplusplus
}
#endif
#endif /* TLS_CLIENT_H */
