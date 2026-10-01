/**
 * @file https_client.h
 * @author H-000-H
 * @brief HTTPS Client Header File (coreHTTP + lwIP altcp_tls)
 * @note 本文件与 http_client 功能一致, 差别仅在传输通道: 加密通道按项目设计
 *       不走 transport_glue, 由本包装层直接基于 tls_client (lwIP altcp_tls
 *       直连封装) 提供 coreHTTP 需要的 send/recv 适配。
 *       连接采用 keep-alive: do_connect (TCP + TLS 握手) 完成后可连续多次
 *       request; 响应体指针指向上下文内部静态缓冲, 有效至下一次 request 或断连。
 *       驱动模型: 应用先调用 https_client_process() 等待握手完成, 再发起 request。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "core_http_client.h"
#include "net_error.h"
#include "tls/tls_client.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_HTTPS_REQUEST_BUFFER_SIZE
#define HTTPS_REQUEST_BUFFER_SIZE CONFIG_HTTPS_REQUEST_BUFFER_SIZE
#else
#define HTTPS_REQUEST_BUFFER_SIZE 512
#endif

#ifdef CONFIG_HTTPS_RESPONSE_BUFFER_SIZE
#define HTTPS_RESPONSE_BUFFER_SIZE CONFIG_HTTPS_RESPONSE_BUFFER_SIZE
#else
#define HTTPS_RESPONSE_BUFFER_SIZE 2048
#endif

/**
 * @brief 附加请求头键值对 (field/value 均为调用方拥有的字符串)
 */
struct https_header
{
    const char* field; /**< 头域名 (如 "Content-Type") */
    const char* value; /**< 头域值 (如 "application/json") */
};

/**
 * @brief HTTPS 响应出参 (指针指向包装层内部缓冲)
 */
struct https_client_response
{
    uint16_t       status_code; /**< 响应状态码 (如 200) */
    const uint8_t* body;        /**< 响应体指针 (有效至下次 request/断连) */
    size_t         body_len;    /**< 响应体长度 */
    const uint8_t* headers;     /**< 原始响应头文本 */
    size_t         headers_len; /**< 响应头长度 */
};

/**
 * @brief HTTPS 传输层网络上下文 (供 coreHTTP 回调持有)
 */
struct https_network_context
{
    struct tls_client_context* tls_client; /**< 加密通道 (altcp_tls) */
};

/**
 * @brief HTTPS 客户端控制块
 */
struct https_client_context
{
    /**< coreHTTP 与传输层 (静态嵌入, 零堆) */
    struct tls_client_context    tls_client;                                  /**< 加密通道 (TCP + TLS 握手/收发) */
    struct https_network_context network_context;                             /**< coreHTTP 网络上下文 */
    TransportInterface_t         transport_interface;                         /**< 传输接口 (altcp_tls 适配) */
    HTTPRequestHeaders_t         request_headers;                             /**< 请求头描述 (指向下方缓冲) */
    uint8_t                      request_buffer[HTTPS_REQUEST_BUFFER_SIZE];   /**< 请求头组装静态缓冲 */
    HTTPResponse_t               response;                                    /**< 响应描述 (指向下方缓冲) */
    uint8_t                      response_buffer[HTTPS_RESPONSE_BUFFER_SIZE]; /**< 响应接收静态缓冲 */

    /**< 连接配置 */
    const char*    server_ip; /**< 服务器 IP 地址字符串 (点分十进制) */
    uint16_t       port;      /**< 端口 (标准加密为 443) */
    const char*    host;      /**< Host 头域名 (NULL 时用 server_ip) */
    const uint8_t* ca_cert;   /**< 服务器 CA 证书 (PEM, 含结尾 '\0'; NULL=不校验) */
    uint32_t       ca_len;    /**< CA 证书长度 (含结尾 '\0') */

    /**< 运行时连接状态 */
    bool     connect_requested;    /**< do_connect 已发起, 等待握手流程走完 */
    uint32_t tls_connect_start_ms; /**< 建连发起时间戳 (超时兜底) */
};

/**
 * @brief 初始化 HTTPS 客户端上下文 (绑定 coreHTTP 传输接口与静态缓冲)
 * @param[in] context HTTPS 客户端控制块
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int https_client_init(struct https_client_context* context);

/**
 * @brief 发起 HTTPS 连接 (异步: TCP + TLS 握手在回调上下文完成)
 * @param[in] context HTTPS 客户端控制块
 * @return int NET_OK 已发起; NET_ERR_INVAL 配置缺失; NET_ERR_STATE 已在连接中;
 *             NET_ERR_CONN 建连发起失败
 * @note CA 校验使用 context->ca_cert (NULL 为不校验, 仅调试用)
 */
int https_client_do_connect(struct https_client_context* context);

/**
 * @brief 断开 HTTPS 连接并清理 TLS 资源
 * @param[in] context HTTPS 客户端控制块
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int https_client_disconnect(struct https_client_context* context);

/**
 * @brief 检查 HTTPS 客户端是否已连接 (TCP + TLS 握手均完成)
 * @param[in] context HTTPS 客户端控制块
 * @return bool 连接状态
 */
bool is_https_client_connected(const struct https_client_context* context);

/**
 * @brief 发起一次 HTTPS 请求并同步等待完整响应 (keep-alive, 可连续调用)
 * @param[in]  context       HTTPS 客户端控制块
 * @param[in]  method        请求方法 ("GET"/"POST"/"PUT"/"DELETE" 等)
 * @param[in]  path          请求路径 (如 "/api/data", 可带查询串)
 * @param[in]  headers       附加头数组 (可含 Content-Type 等; 可为 NULL)
 * @param[in]  headers_count 附加头个数 (无则为 0)
 * @param[in]  body          请求体 (可为 NULL)
 * @param[in]  body_len      请求体长度 (无则为 0)
 * @param[out] response      响应出参 (状态码/体/头指针; 不可为 NULL)
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法/未连接;
 *             NET_ERR_NOSPC 请求头缓冲不足; NET_ERR_TIMEOUT 等待响应超时;
 *             NET_ERR_CONN 链路中断
 * @note 响应体指针有效至下次 request 或 disconnect; 方法串需全大写
 */
int https_client_request(struct https_client_context* context, const char* method, const char* path, const struct https_header* headers,
                         size_t headers_count, const void* body, size_t body_len, struct https_client_response* response);

/**
 * @brief 协议栈处理引擎, 需在主循环或专用任务中周期性调用
 * @param[in] context HTTPS 客户端控制块
 * @note 驱动建连确认 (connected 回调触发即握手完成) 与超时兜底,
 *       并泵送 TX FIFO 中排队的加密数据; 请求收发为同步流程
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int https_client_process(struct https_client_context* context);
#ifdef __cplusplus
}
#endif
#endif /* HTTPS_CLIENT_H */
