/**
 * @file https_client.c
 * @author H-000-H
 * @brief HTTPS Client Implementation (coreHTTP + lwIP altcp_tls)
 * @note  报文处理由 coreHTTP / 加密由 altcp_tls 负责; 本文件只做上下文绑定 + TLS 建连两段式驱动 + request 同步流程
 * @note  加密通道不经 transport_glue, send/recv 直接适配到 tls_client
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include "https_client.h"

#include "compiler_compat.h"
#include "mini_time.h"
#include "system_log.h"
#include <string.h>

static const char* const s_kTag = "https_client";

#ifdef CONFIG_TLS_HANDSHAKE_TIMEOUT_MS
#define HTTPS_CONNECT_TIMEOUT_MS CONFIG_TLS_HANDSHAKE_TIMEOUT_MS
#else
#define HTTPS_CONNECT_TIMEOUT_MS 20000 /**< TCP + TLS 握手等待上限 */
#endif

/**
 * @brief coreHTTP 时间源 (毫秒), 用于收发零字节重试窗口计时
 * @return uint32_t 当前时间戳
 */
static uint32_t https_get_time_ms(void) { return mini_time_ms(); }

/**
 * @brief coreHTTP 状态码到 NET_* 错误码映射
 * @param[in] status coreHTTP 返回码
 * @return int 对应 NET_* 错误码 (成功为 NET_OK)
 */
static int https_status_to_net(HTTPStatus_t status)
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
/* 传输适配 (altcp_tls, 不经 transport_glue)                                  */
/* -------------------------------------------------------------------------- */
/**
 * @brief coreHTTP 发送适配: 数据写入加密通道 (TX FIFO -> altcp_write)
 * @param[in] net_ctx coreHTTP 网络上下文 (实际为 https_network_context)
 * @param[in] buf 待发送数据
 * @param[in] len 数据长度
 * @return int32_t 实际入队字节数; 负值为错误; 0 表示缓冲满可重试
 */
static int32_t https_transport_send(NetworkContext_t* net_ctx, const void* buf, size_t len)
{
    struct https_network_context* ctx = (struct https_network_context*)(void*)net_ctx;

    if ((ctx == NULL) || (ctx->tls_client == NULL) || !ctx->tls_client->is_connected)
        return -1;

    if (len > UINT16_MAX)
        len = UINT16_MAX;

    uint16_t sent_len = 0;
    int      err = tls_client_send(ctx->tls_client, buf, (uint16_t)len, &sent_len);
    if (err != ERR_OK)
        return -1;
    return (int32_t)sent_len;
}

/**
 * @brief coreHTTP 接收适配: 从解密后的接收缓冲读取 (非阻塞)
 * @param[in]  net_ctx coreHTTP 网络上下文 (实际为 https_network_context)
 * @param[out] buf     目标缓冲
 * @param[in]  len     期望读取长度
 * @return int32_t 实际读取字节数; 负值为错误; 0 表示暂无数据可重试
 */
static int32_t https_transport_recv(NetworkContext_t* net_ctx, void* buf, size_t len)
{
    struct https_network_context* ctx = (struct https_network_context*)(void*)net_ctx;

    if ((ctx == NULL) || (ctx->tls_client == NULL))
        return -1;

    if (!ctx->tls_client->is_connected)
    {
        /* 链路已断: 契约要求断连不得返回 0, 直接报错终止本次收发 */
        return -1;
    }

    if (len > UINT16_MAX)
        len = UINT16_MAX;

    uint16_t recv_len = 0;
    int      err = tls_client_read(ctx->tls_client, buf, (uint16_t)len, &recv_len);
    if (err != ERR_OK)
        return -1;
    return (int32_t)recv_len;
}

/* -------------------------------------------------------------------------- */
/* 连接管理                                                                   */
/* -------------------------------------------------------------------------- */
int https_client_init(struct https_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    memset(context, 0, sizeof(*context));

    /* 传输层绑定: altcp_tls 适配函数 + 加密通道 */
    context->network_context.tls_client = &context->tls_client;
    context->transport_interface.recv = https_transport_recv;
    context->transport_interface.send = https_transport_send;
    context->transport_interface.pNetworkContext = (NetworkContext_t*)(void*)&context->network_context;

    /* coreHTTP 请求/响应静态缓冲挂载 */
    context->request_headers.pBuffer = context->request_buffer;
    context->request_headers.bufferLen = sizeof(context->request_buffer);
    context->response.pBuffer = context->response_buffer;
    context->response.bufferLen = sizeof(context->response_buffer);
    context->response.getTime = https_get_time_ms;

    return NET_OK;
}

/**
 * @brief 复位连接流程标志并关闭加密通道
 * @param[in,out] context HTTPS 客户端控制块
 */
static void https_reset_connection(struct https_client_context* context)
{
    context->connect_requested = false;
    MINI_IGNORE_RESULT(tls_client_disconnect(&context->tls_client));
}

int https_client_do_connect(struct https_client_context* context)
{
    if (context == NULL || context->server_ip == NULL || context->port == 0)
        return NET_ERR_INVAL;

    if (context->connect_requested || is_https_client_connected(context))
    {
        MT_LOG_WARN(s_kTag, "already connecting/connected");
        return NET_ERR_STATE;
    }

    int err = tls_client_init_and_connect(&context->tls_client, context->server_ip, context->port, context->ca_cert, context->ca_len);
    if (err != ERR_OK)
    {
        MT_LOG_ERROR(s_kTag, "tls connect start failed: %d", err);
        return NET_ERR_CONN;
    }

    context->connect_requested = true;
    context->tls_connect_start_ms = mini_time_ms();

    MT_LOG_INFO(s_kTag, "connecting to %s:%u (tls)...", context->server_ip, context->port);
    return NET_OK;
}

int https_client_disconnect(struct https_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    https_reset_connection(context);
    MT_LOG_INFO(s_kTag, "https disconnected");
    return NET_OK;
}

bool is_https_client_connected(const struct https_client_context* context) { return (context != NULL) && context->tls_client.is_connected; }

/* -------------------------------------------------------------------------- */
/* 请求                                                                       */
/* -------------------------------------------------------------------------- */
int https_client_request(struct https_client_context* context, const char* method, const char* path, const struct https_header* headers,
                         size_t headers_count, const void* body, size_t body_len, struct https_client_response* response)
{
    if (context == NULL || method == NULL || path == NULL || response == NULL)
        return NET_ERR_INVAL;

    if (!is_https_client_connected(context))
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
        return https_status_to_net(status);
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
            return https_status_to_net(status);
        }
    }

    /* 响应缓冲复位: 保留 pBuffer/bufferLen/getTime, 其余清零 */
    uint8_t* resp_buf = context->response.pBuffer;
    size_t   resp_buf_len = context->response.bufferLen;
    memset(&context->response, 0, sizeof(context->response));
    context->response.pBuffer = resp_buf;
    context->response.bufferLen = resp_buf_len;
    context->response.getTime = https_get_time_ms;

    /* 同步发送请求并接收完整响应 (coreHTTP 内部按 getTime 计时重试) */
    status = HTTPClient_Send(&context->transport_interface, &context->request_headers, (const uint8_t*)body, body_len, &context->response, 0);
    if (status != HTTPSuccess)
    {
        MT_LOG_ERROR(s_kTag, "HTTPClient_Send failed: %s", HTTPClient_strerror(status));
        return https_status_to_net(status);
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
int https_client_process(struct https_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    /* 两段式建连: do_connect 发起, connected 回调 (TLS 握手后) 置位; 此处超时兜底 */
    if (context->connect_requested && !context->tls_client.is_connected)
    {
        if ((mini_time_ms() - context->tls_connect_start_ms) >= HTTPS_CONNECT_TIMEOUT_MS)
        {
            MT_LOG_ERROR(s_kTag, "tls handshake timeout");
            https_reset_connection(context);
            return NET_OK;
        }
    }

    /* 泵送发送: TX FIFO 中排队数据经 TLS 加密下推 (sent 回调也会驱动, 此处兜底) */
    MINI_IGNORE_RESULT(tls_client_poll_send(&context->tls_client));

    return NET_OK;
}
