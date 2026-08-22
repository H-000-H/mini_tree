/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file tcp_client.c
 * @brief TCP 客户端实现文件
 */
#include "tcp_client.h"

#include "compiler_compat.h"
#include "lwip/err.h"
#include "system_log.h"
_Static_assert((TCP_CLIENT_TX_BUFFER_SIZE & (TCP_CLIENT_TX_BUFFER_SIZE - 1U)) == 0U, "TCP_CLIENT_TX_BUFFER_SIZE must be power of 2");
_Static_assert((TCP_CLIENT_RX_BUFFER_SIZE & (TCP_CLIENT_RX_BUFFER_SIZE - 1U)) == 0U, "TCP_CLIENT_RX_BUFFER_SIZE must be power of 2");

int tcp_client_poll_send(struct tcp_client_context* ctx)
{
    if(ctx == NULL||!ctx->is_connected||ctx->pcb == NULL)
        return ERR_VAL;

    uint16_t snd_buf_avail = tcp_sndbuf(ctx->pcb);

    if (snd_buf_avail == 0)
        return ERR_MEM;

    uint8_t temp_buf[128];

    /*限制最大发送块大小*/
    uint16_t chunk = (snd_buf_avail < sizeof(temp_buf)) ? snd_buf_avail : (uint16_t)sizeof(temp_buf);

    uint16_t read_bytes = (uint16_t)fifo_uni_read_block(&ctx->tx_fifo, temp_buf, chunk);

    if(read_bytes>0)
    {
        err_t err = tcp_write(ctx->pcb, temp_buf, read_bytes, TCP_WRITE_FLAG_COPY);

        if (err == ERR_OK)
            return ERR_OK;
        else
            SYS_LOGE(k_tag, "tcp_write failed: %d\r\n", err);
    }

    SYS_LOGE(k_tag,"fifo buffer not read %d bytes\r\n", read_bytes);
    return ERR_VAL;
}

/**
 * @brief TCP 客户端错误回调函数
 * @param[in] arg   回调函数参数
 * @param[in] err   错误码
 * @note 该函数在 TCP 连接错误时调用
 */
static void tcp_client_error_callback(void* arg, err_t err)
{
    struct tcp_client_context* ctx = (struct tcp_client_context*)arg;
    if (ctx != NULL) 
    {
        SYS_LOGE(k_tag, "client err occurred: %d\r\n", err);
        ctx->pcb = NULL;          /* lwIP 内部已释放 PCB，此处不调用 tcp_close */
        ctx->is_connected = false;
    }
}

/**
 * @brief TCP 客户端接收回调函数(接收到对方的数据)
 * @param[in] arg           回调函数参数
 * @param[in] pcb           TCP 控制块
 * @param[in] current_buf   当前数据包
 * @param[in] err           错误码
 * @note 该函数在 TCP 接收到数据时调用
 */
static err_t tcp_client_receive_callback(void* arg, struct tcp_pcb* pcb, struct pbuf* current_buf, err_t err)
{
    struct tcp_client_context* ctx = (struct tcp_client_context*)arg;

    if (ctx == NULL || ctx->pcb != pcb)
    {
        if(current_buf)
        {
            pbuf_free(current_buf);
        }
        return ERR_VAL;
    }

    /* 服务器断开连接 */
    if (current_buf == NULL)
    {
        SYS_LOGI(k_tag, "server disconnected\r\n");
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_close(pcb);
        ctx->pcb = NULL;
        ctx->is_connected = false;
        return ERR_OK;
    }

    if(err!=ERR_OK)
    {
        SYS_LOGE(k_tag,"tcp_client_receive_callback error: %d\r\n", err);
        pbuf_free(current_buf);
        return err;
    }

    struct pbuf* buf = current_buf;
    uint16_t total_bytes = 0;

    while (buf) 
    {
        if(buf->len>0)
        {
            uint16_t written  = (uint16_t)fifo_uni_write_block(&ctx->rx_fifo, (const uint8_t*)buf->payload, buf->len);
            total_bytes = (uint16_t)(total_bytes + written);
            if(written < buf->len)
                SYS_LOGW(k_tag,"RX FIFO full, dropped %u bytes\r\n", (unsigned int)(buf->len - written));
        }
        buf = buf->next;
    }

    if (total_bytes > 0)
        tcp_recved(pcb, total_bytes);

    /* 释放整条 pbuf 链 (buf 遍历后已为 NULL, 须释放原始指针, 否则内存泄漏) */
    pbuf_free(current_buf);
    return ERR_OK;
}

/**
 * @brief TCP 客户端发送回调函数(发送成功的数据)
 * @param[in] arg   回调函数参数
 * @param[in] pcb   TCP 控制块
 * @param[in] len   发送数据长度
 * @note 该函数在 TCP 发送数据时调用
 */
static err_t tcp_client_sent_callback(void* arg, struct tcp_pcb* pcb, uint16_t len)
{
    COMPAT_IGNORE_RESULT(pcb);
    COMPAT_IGNORE_RESULT(len);
    struct tcp_client_context* ctx = (struct tcp_client_context*)arg;
    if (ctx != NULL)/* 发送窗口释放，尝试继续发 TX FIFO 里的下一批数据 */
        tcp_client_poll_send(ctx);

    return ERR_OK;
}

/**
 * @brief TCP 客户端连接回调函数(三次握手成功建立连接回调)
 * @param[in] arg   回调函数参数
 * @param[in] pcb   TCP 控制块
 * @param[in] err   错误码
 * @note 该函数在 TCP 连接成功时调用
 */
static err_t tcp_client_connected_callback(void* arg, struct tcp_pcb* pcb, err_t err)
{
    struct tcp_client_context* ctx = (struct tcp_client_context*)arg;
    if (ctx == NULL || err != ERR_OK)
    {
        SYS_LOGE(k_tag, "tcp_client_connected_callback error: %d\r\n", err);
        if (ctx) 
            ctx->is_connected = false;
        return err;
    }
    SYS_LOGI(k_tag, "connected to %s:%u\r\n", ctx->server_ip, ctx->port);

    ctx->is_connected = true;

    /* 注册数据接收和错误回调 */
    tcp_recv(pcb, tcp_client_receive_callback);
    tcp_sent(pcb, tcp_client_sent_callback);

    tcp_client_poll_send(ctx);

    /* 连接建立后   如果 TX FIFO 里有提前压入的数据，立即触发一次发送 */
    return ERR_OK;
}

int tcp_client_init_and_connect(struct tcp_client_context* ctx, const char* server_ip, uint16_t port)
{
    ip_addr_t dest_ip;
    err_t ret;

    if (!ctx || !server_ip || port == 0) 
        return ERR_ARG;
    ctx->server_ip = server_ip;
    ctx->port = port;
    ctx->is_connected = false;

    fifo_uni_init(&ctx->rx_fifo, ctx->rx_buffer,TCP_CLIENT_RX_TX_BYTE_TYPE ,TCP_CLIENT_RX_BUFFER_SIZE );
    fifo_uni_init(&ctx->tx_fifo, ctx->tx_buffer,TCP_CLIENT_RX_TX_BYTE_TYPE, TCP_CLIENT_TX_BUFFER_SIZE );

    /* 解析 IP */
    if (!ip4addr_aton(server_ip, &dest_ip)) 
    {
        SYS_LOGE(k_tag, "invalid IP: %s\r\n", server_ip);
        return ERR_ARG;
    }

    /* 创建 PCB */
    ctx->pcb = tcp_new();
    if (ctx->pcb == NULL) 
    {
        SYS_LOGE(k_tag, "tcp_new failed\r\n");
        return ERR_MEM;
    }

    /* 注入 Context 到 lwIP 回调上下文 arg */
    tcp_arg(ctx->pcb, ctx);
    tcp_err(ctx->pcb, tcp_client_error_callback);

    /* 连接服务器 */
    ret = tcp_connect(ctx->pcb, &dest_ip, port, tcp_client_connected_callback);

    if (ret != ERR_OK) 
    {
        SYS_LOGE(k_tag, "tcp_connect failed: %d\r\n", ret);
        tcp_close(ctx->pcb);
        ctx->pcb = NULL;
        return ret;
    }

    SYS_LOGI(k_tag, "connecting to %s:%d...\r\n", server_ip, port);
    return ERR_OK;
}

int tcp_client_send(struct tcp_client_context* ctx, const void* data, uint16_t len, uint16_t* sent_len)
{
    if (!ctx || !data || len == 0) 
        return ERR_ARG;

    if (!ctx->is_connected || ctx->pcb == NULL) 
        return ERR_CONN;

    uint16_t written = (uint16_t)fifo_uni_write_block(&ctx->tx_fifo, (const uint8_t*)data, len);

    tcp_client_poll_send(ctx);

    if(sent_len)
        *sent_len = written;
    return ERR_OK;
}

int tcp_client_read(struct tcp_client_context* ctx, void* buf, uint16_t len, uint16_t* recv_len)
{
    if (!ctx || !buf || len == 0 || !recv_len)
        return ERR_ARG;

    uint16_t read_bytes = fifo_uni_read_block(&ctx->rx_fifo, (uint8_t*)buf, len);
    *recv_len = read_bytes;
    return ERR_OK;
}

int tcp_client_disconnect(struct tcp_client_context* ctx)
{
    if (!ctx) 
        return ERR_ARG;

    if (ctx->pcb != NULL) {
        tcp_arg(ctx->pcb, NULL);
        tcp_recv(ctx->pcb, NULL);
        tcp_sent(ctx->pcb, NULL);
        tcp_err(ctx->pcb, NULL);
        tcp_close(ctx->pcb);
        ctx->pcb = NULL;
    }
    ctx->is_connected = false;
    SYS_LOGI(k_tag, "client disconnected\r\n");
    return ERR_OK;
}
