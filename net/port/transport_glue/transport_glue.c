/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file transport_glue.c
 * @brief coreMQTT / coreHTTP 共享传输胶水层实现
 * @author H-000-H
 * @details write 把数据写入底层通道的 TX FIFO (写满则让出调度等待);
 *          read 从 RX FIFO 取数据, 无数据时按 recv_timeout_ms 轮询等待。
 *          裸机协作式调度下, 等待均通过 osal_delay_ms(1) 让出,
 *          让网卡收包泵任务有机会把数据推进协议栈。
 *          末尾 send/recv 是把 NET_* 错误码翻译为 core 库字节数契约的薄适配。
 */
#include "transport_glue.h"

#include "compiler_compat.h"
#include "lwip/err.h"
#include "osal.h"
#include "system_log.h"
#include <stdbool.h>

static const char* const s_kTag = "transport_glue";

/* ========================================================================= */
/* 建连 / 断连 / 链路状态                                                     */
/* ========================================================================= */
int network_transport_connect(struct NetworkContext* context, const char* server_ip, uint16_t port)
{
    if (context == NULL || context->tcp_client == NULL || server_ip == NULL || port == 0U)
        return NET_ERR_INVAL;

    err_t err = tcp_client_init_and_connect(context->tcp_client, server_ip, port);
    return (err == ERR_OK) ? NET_OK : NET_ERR_CONN;
}

int network_transport_disconnect(struct NetworkContext* context)
{
    if (context == NULL || context->tcp_client == NULL)
        return NET_ERR_INVAL;

    COMPAT_IGNORE_RESULT(tcp_client_disconnect(context->tcp_client));
    return NET_OK;
}

bool network_transport_is_connected(struct NetworkContext* context)
{
    return (context != NULL) && (context->tcp_client != NULL) && context->tcp_client->is_connected;
}

bool network_transport_link_failed(struct NetworkContext* context)
{
    if (context == NULL || context->tcp_client == NULL)
        return true;
    /* err 回调已置空控制块且未连上: 建连失败 */
    return (context->tcp_client->pcb == NULL) && !context->tcp_client->is_connected;
}

/* ========================================================================= */
/* 收发                                                                       */
/* ========================================================================= */
int network_transport_write(struct NetworkContext* context, const void* buffer, size_t length)
{
    if (context == NULL || context->tcp_client == NULL || buffer == NULL || length == 0U)
        return NET_ERR_INVAL;

    struct tcp_client_context* tcp_client = context->tcp_client;
    const uint8_t* data = (const uint8_t*)buffer;
    size_t remaining = length;
    uint32_t start_ms = osal_time_ms();

    while (remaining > 0U)
    {
        if (!tcp_client->is_connected)
        {
            SYS_LOGW(s_kTag, "write aborted, link down");
            return NET_ERR_CONN;
        }

        uint16_t chunk = (remaining > UINT16_MAX) ? UINT16_MAX : (uint16_t)remaining;
        uint16_t written = 0;

        if (tcp_client_send(tcp_client, data, chunk, &written) == ERR_CONN)
            return NET_ERR_CONN;
        data += written;
        remaining -= written;

        if (written == 0U)
        {
            /* TX FIFO 满: 让出调度等待窗口释放 */
            if ((osal_time_ms() - start_ms) >= NETWORK_TRANSPORT_TX_TIMEOUT_MS)
            {
                SYS_LOGE(s_kTag, "write timeout, fifo full");
                return NET_ERR_TIMEOUT;
            }
            osal_delay_ms(1);
        }
    }

    return NET_OK;
}

int network_transport_read(struct NetworkContext* context, void* buffer, size_t length,
                           size_t* read_length)
{
    if (context == NULL || context->tcp_client == NULL || buffer == NULL || length == 0U ||
        read_length == NULL)
        return NET_ERR_INVAL;

    struct tcp_client_context* tcp_client = context->tcp_client;
    uint32_t start_ms = osal_time_ms();

    for (;;)
    {
        uint16_t want = (length > UINT16_MAX) ? UINT16_MAX : (uint16_t)length;
        uint16_t got = 0;

        if (tcp_client_read(tcp_client, buffer, want, &got) == ERR_CONN)
            return NET_ERR_CONN;

        if (got > 0U)
        {
            *read_length = got;
            return NET_OK;
        }

        /* FIFO 空: 链路已断则报错, 否则按超时策略等待 */
        if (!tcp_client->is_connected)
            return NET_ERR_CONN;

        *read_length = 0U;
        if (context->recv_timeout_ms == 0U)
            return NET_OK;
        if ((osal_time_ms() - start_ms) >= context->recv_timeout_ms)
            return NET_OK;
        osal_delay_ms(1);
    }
}

/* ========================================================================= */
/* core 库 TransportSend_t / TransportRecv_t 签名适配                        */
/* ========================================================================= */
int32_t network_transport_send(struct NetworkContext* context, const void* buffer, size_t length)
{
    int err = network_transport_write(context, buffer, length);
    if (err != NET_OK)
        return -1;
    return (int32_t)length;
}

int32_t network_transport_recv(struct NetworkContext* context, void* buffer, size_t length)
{
    size_t read_length = 0;
    int err = network_transport_read(context, buffer, length, &read_length);
    if (err != NET_OK)
        return -1;
    return (int32_t)read_length;
}
