/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file http_client.c
 * @brief HTTP Client Implementation (coreHTTP 薄包装)
 * @author H-000-H
 * @details 请求组装/响应解析/分块解码由 coreHTTP 负责, 传输走
 *          transport_glue (tcp_client FIFO 通道); 本文件只做:
 *          1. 上下文与 coreHTTP 传输接口/静态缓冲的绑定;
 *          2. TCP 建连两段式驱动 (与 mqtt_client 一致);
 *          3. request 同步流程: 组装请求头 -> HTTPClient_Send -> 出参映射。
 */
#include "http_client.h"

#include "compiler_compat.h"
#include "mini_time.h"
#include "system_log.h"
#include <string.h>

static const char* const s_kTag = "http_client";

#define HTTP_TCP_CONNECT_TIMEOUT_MS 15000 /**< TCP 三次握手等待上限 */
#define HTTP_TRANSPORT_RECV_WAIT_MS 1     /**< 传输层 recv 单次等待 (让出调度给收包泵) */

/**
 * @brief coreHTTP 时间源 (毫秒), 用于收发零字节重试窗口计时
 * @return uint32_t 当前时间戳
 */
static uint32_t http_get_time_ms(void) { return mini_time_ms(); }

/**
 * @brief coreHTTP 状态码到 NET_* 错误码映射
 * @param[in] status coreHTTP 返回码
 * @return int 对应 NET_* 错误码 (成功为 NET_OK)
 */
static int http_status_to_net(HTTPStatus_t status)
{
    switch (status)
    {
    case HTTPSuccess:
        return NET_OK;
    case HTTPInvalidParameter:
        return NET_ERR_INVAL;
    case HTTPInsufficientMemory:
    case HTTPSecurityAlertExtraneousResponseData:
        return NET_ERR_NOSPC;
    case HTTPNoResponse:
        return NET_ERR_TIMEOUT;
    case HTTPNetworkError:
    case HTTPPartialResponse:
    default:
        return NET_ERR_CONN;
    }
}

/* -------------------------------------------------------------------------- */
/* 连接管理                                                                   */
/* -------------------------------------------------------------------------- */
int http_client_init(struct http_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    memset(context, 0, sizeof(*context));

    /* 传输层绑定: 胶水函数 + tcp_client 通道 */
    context->network_context.tcp_client = &context->tcp_client;
    context->network_context.recv_timeout_ms = HTTP_TRANSPORT_RECV_WAIT_MS;
    context->transport_interface.recv = network_transport_recv;
    context->transport_interface.send = network_transport_send;
    context->transport_interface.pNetworkContext = &context->network_context;

    /* coreHTTP 请求/响应静态缓冲挂载 */
    context->request_headers.pBuffer = context->request_buffer;
    context->request_headers.bufferLen = sizeof(context->request_buffer);
    context->response.pBuffer = context->response_buffer;
    context->response.bufferLen = sizeof(context->response_buffer);
    context->response.getTime = http_get_time_ms;

    return NET_OK;
}

/**
 * @brief 复位连接流程标志并关闭底层 TCP
 * @param[in,out] context HTTP 客户端控制块
 */
static void http_reset_connection(struct http_client_context* context)
{
    context->connect_requested = false;
    MINI_IGNORE_RESULT(network_transport_disconnect(&context->network_context));
}

int http_client_do_connect(struct http_client_context* context)
{
    if (context == NULL || context->server_ip == NULL || context->port == 0)
        return NET_ERR_INVAL;

    if (context->connect_requested || is_http_client_connected(context))
    {
        MT_LOG_WARN(s_kTag, "already connecting/connected");
        return NET_ERR_STATE;
    }

    int err = network_transport_connect(&context->network_context, context->server_ip, context->port);
    if (err != NET_OK)
        return err;

    context->connect_requested = true;
    context->tcp_connect_start_ms = mini_time_ms();

    MT_LOG_INFO(s_kTag, "connecting to %s:%u...", context->server_ip, context->port);
    return NET_OK;
}

int http_client_disconnect(struct http_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    http_reset_connection(context);
    MT_LOG_INFO(s_kTag, "http disconnected");
    return NET_OK;
}

bool is_http_client_connected(const struct http_client_context* context)
{
    return (context != NULL) && network_transport_is_connected((struct NetworkContext*)&context->network_context);
}

/* -------------------------------------------------------------------------- */
/* 请求                                                                       */
/* -------------------------------------------------------------------------- */
int http_client_request(struct http_client_context* context, const char* method, const char* path, const struct http_header* headers,
                        size_t headers_count, const void* body, size_t body_len, struct http_client_response* response)
{
    if (context == NULL || method == NULL || path == NULL || response == NULL)
        return NET_ERR_INVAL;

    if (!is_http_client_connected(context))
    {
        MT_LOG_ERROR(s_kTag, "request rejected: not connected");
        return NET_ERR_INVAL;
    }

    /* 请求描述: 方法/路径/Host + keep-alive (连接可复用, 多次 request) */
    HTTPRequestInfo_t request_info = {0};
    request_info.pMethod = method;
    request_info.methodLen = strlen(method);
    request_info.pPath = path;
    request_info.pathLen = strlen(path);
    const char* host = (context->host != NULL) ? context->host : context->server_ip;
    request_info.pHost = host;
    request_info.hostLen = strlen(host);
    request_info.reqFlags = HTTP_REQUEST_KEEP_ALIVE_FLAG;

    HTTPStatus_t status = HTTPClient_InitializeRequestHeaders(&context->request_headers, &request_info);
    if (status != HTTPSuccess)
    {
        MT_LOG_ERROR(s_kTag, "InitRequestHeaders failed: %s", HTTPClient_strerror(status));
        return http_status_to_net(status);
    }

    /* 附加头逐个写入 (缓冲不足即报错, 不截断请求) */
    for (size_t i = 0; i < headers_count; i++)
    {
        if (headers[i].field == NULL || headers[i].value == NULL)
            continue;

        status =
            HTTPClient_AddHeader(&context->request_headers, headers[i].field, strlen(headers[i].field), headers[i].value, strlen(headers[i].value));
        if (status != HTTPSuccess)
        {
            MT_LOG_ERROR(s_kTag, "AddHeader %s failed: %s", headers[i].field, HTTPClient_strerror(status));
            return http_status_to_net(status);
        }
    }

    /* 响应缓冲复位: 保留 pBuffer/bufferLen/getTime, 其余清零 */
    uint8_t* resp_buf = context->response.pBuffer;
    size_t   resp_buf_len = context->response.bufferLen;
    memset(&context->response, 0, sizeof(context->response));
    context->response.pBuffer = resp_buf;
    context->response.bufferLen = resp_buf_len;
    context->response.getTime = http_get_time_ms;

    /* 同步发送请求并接收完整响应 (coreHTTP 内部按 getTime 计时重试) */
    status = HTTPClient_Send(&context->transport_interface, &context->request_headers, (const uint8_t*)body, body_len, &context->response, 0);
    if (status != HTTPSuccess)
    {
        MT_LOG_ERROR(s_kTag, "HTTPClient_Send failed: %s", HTTPClient_strerror(status));
        return http_status_to_net(status);
    }

    response->status_code = context->response.statusCode;
    response->body = context->response.pBody;
    response->body_len = context->response.bodyLen;
    response->headers = context->response.pHeaders;
    response->headers_len = context->response.headersLen;

    MT_LOG_INFO(s_kTag, "%s %s -> %u (%u bytes)", method, path, (unsigned)response->status_code, (unsigned)response->body_len);
    return NET_OK;
}

/* -------------------------------------------------------------------------- */
/* 协议栈驱动                                                                 */
/* -------------------------------------------------------------------------- */
int http_client_process(struct http_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    /* 两段式建连: do_connect 发起底层链路, 这里做超时兜底 */
    if (context->connect_requested && !is_http_client_connected(context))
    {
        if (network_transport_link_failed(&context->network_context) ||
            ((mini_time_ms() - context->tcp_connect_start_ms) >= HTTP_TCP_CONNECT_TIMEOUT_MS))
        {
            /* 底层建连失败 (err 回调已置空控制块) 或超时 */
            MT_LOG_ERROR(s_kTag, "link connect failed/timeout");
            http_reset_connection(context);
        }
    }

    return NET_OK;
}
