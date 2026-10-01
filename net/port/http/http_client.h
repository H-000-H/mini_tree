/**
 * @file http_client.h
 * @author H-000-H
 * @brief HTTP Client Header File
 * @note 本文件为 coreHTTP 的薄包装层: 请求组装/响应解析/分块解码全部由
 *       coreHTTP 负责, 传输走 transport_glue (tcp_client FIFO 通道)。
 *       连接采用 keep-alive: do_connect 建连后可连续多次 request,
 *       响应体指针指向上下文内部静态缓冲, 有效至下一次 request 或断连。
 *       驱动模型: 应用先调用 http_client_process() 等待底层建连完成,
 *       再发起 request (请求/响应为同步流程)。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "core_http_client.h"
#include "net_error.h"
#include "transport_glue/transport_glue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_HTTP_REQUEST_BUFFER_SIZE
#define HTTP_REQUEST_BUFFER_SIZE CONFIG_HTTP_REQUEST_BUFFER_SIZE
#else
#define HTTP_REQUEST_BUFFER_SIZE 512
#endif

#ifdef CONFIG_HTTP_RESPONSE_BUFFER_SIZE
#define HTTP_RESPONSE_BUFFER_SIZE CONFIG_HTTP_RESPONSE_BUFFER_SIZE
#else
#define HTTP_RESPONSE_BUFFER_SIZE 2048
#endif

/**
 * @brief 附加请求头键值对 (field/value 均为调用方拥有的字符串)
 */
struct http_header
{
    const char* field; /**< 头域名 (如 "Content-Type") */
    const char* value; /**< 头域值 (如 "application/json") */
};

/**
 * @brief HTTP 响应出参 (指针指向包装层内部缓冲)
 */
struct http_client_response
{
    uint16_t       status_code; /**< 响应状态码 (如 200) */
    const uint8_t* body;        /**< 响应体指针 (有效至下次 request/断连) */
    size_t         body_len;    /**< 响应体长度 */
    const uint8_t* headers;     /**< 原始响应头文本 */
    size_t         headers_len; /**< 响应头长度 */
};

/**
 * @brief HTTP 客户端控制块
 */
struct http_client_context
{
    /**< coreHTTP 与传输层 (静态嵌入, 零堆) */
    struct NetworkContext     network_context;                            /**< 胶水层网络上下文 */
    struct tcp_client_context tcp_client;                                 /**< 底层 TCP 通道 (fifo 收发) */
    TransportInterface_t      transport_interface;                        /**< 传输接口 (胶水函数) */
    HTTPRequestHeaders_t      request_headers;                            /**< 请求头描述 (指向下方缓冲) */
    uint8_t                   request_buffer[HTTP_REQUEST_BUFFER_SIZE];   /**< 请求头组装静态缓冲 */
    HTTPResponse_t            response;                                   /**< 响应描述 (指向下方缓冲) */
    uint8_t                   response_buffer[HTTP_RESPONSE_BUFFER_SIZE]; /**< 响应接收静态缓冲 */

    /**< 连接配置 */
    const char* server_ip; /**< 服务器 IP 地址字符串 (点分十进制) */
    uint16_t    port;      /**< 端口 (标准明文为 80) */
    const char* host;      /**< Host 头域名 (NULL 时用 server_ip) */

    /**< 运行时连接状态 */
    bool     connect_requested;    /**< do_connect 已发起, 等待建连流程走完 */
    uint32_t tcp_connect_start_ms; /**< TCP 建连发起时间戳 (超时兜底) */
};

/**
 * @brief 初始化 HTTP 客户端上下文 (绑定 coreHTTP 传输接口与静态缓冲)
 * @param[in] context HTTP 客户端控制块
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int http_client_init(struct http_client_context* context);

/**
 * @brief 发起 HTTP 连接 (异步: TCP 握手在回调上下文完成, process 里确认就绪)
 * @param[in] context HTTP 客户端控制块
 * @return int NET_OK 已发起; NET_ERR_INVAL 配置缺失; NET_ERR_STATE 已在连接中;
 *             NET_ERR_CONN TCP 发起失败
 */
int http_client_do_connect(struct http_client_context* context);

/**
 * @brief 断开 HTTP 连接
 * @param[in] context HTTP 客户端控制块
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int http_client_disconnect(struct http_client_context* context);

/**
 * @brief 检查 HTTP 客户端是否已连接
 * @param[in] context HTTP 客户端控制块
 * @return bool 连接状态
 */
bool is_http_client_connected(const struct http_client_context* context);

/**
 * @brief 发起一次 HTTP 请求并同步等待完整响应 (keep-alive, 可连续调用)
 * @param[in]  context       HTTP 客户端控制块
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
int http_client_request(struct http_client_context* context, const char* method, const char* path, const struct http_header* headers,
                        size_t headers_count, const void* body, size_t body_len, struct http_client_response* response);

/**
 * @brief 协议栈处理引擎, 需在主循环或专用任务中周期性调用
 * @param[in] context HTTP 客户端控制块
 * @note 驱动 TCP 建连确认与超时兜底; 请求收发为同步流程, 不在此进行
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int http_client_process(struct http_client_context* context);
#ifdef __cplusplus
}
#endif
#endif /* HTTP_CLIENT_H */
