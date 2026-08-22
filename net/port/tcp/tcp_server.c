/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file tcp_server.c
 *@brief TCP Server (lwIP netconn 驱动)
 *@note 接收缓冲复用 buffer.h 的字节流 SPSC 无锁环形 FIFO (fifo_byte_*),
 *      元素粒度 1 字节, 适配任意 TCP 字节流, 无 8 字节截断问题。
 */
#include "tcp_server.h"

#include "buffer.h"
#include "string.h"
#include "system_log.h"

static const char* const k_tag = "tcp_server";
static struct tcp_pcb* server_pcb;

/* TCP 接收环形缓冲 回调写入 应用 get_tcp_data 读取。
 * 复用 buffer.h 三合一 SPSC FIFO (FIFO_TYPE_BYTE, 字节流, acquire/release 内存序无锁)。
 * 大小由 Kconfig CONFIG_TCP_RX_BUF_SIZE 配置 (须为 2 的幂次方)。 */
#define TCP_RX_BUF_SIZE CONFIG_TCP_RX_BUF_SIZE
static uint8_t s_rx_buf[TCP_RX_BUF_SIZE];
static struct fifo_spsc_unified s_rx_fifo;
_Static_assert((TCP_RX_BUF_SIZE & (TCP_RX_BUF_SIZE - 1U)) == 0U, "TCP_RX_BUF_SIZE must be power of 2");
/**
 *@brief tcp服务端接收到客户端数据
 *@param[in] arg 用户自定义参数
 *@param[in] pcb tcp控制块
 *@param[in] buf 接收到的数据
 *@param[in] err 错误码
 *@return 错误码
 */
static err_t server_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* buf, err_t err)
{
    struct pbuf* current_pbuf = buf;
    uint16_t written = 0;
    COMPAT_IGNORE_RESULT(arg);

    if (!current_pbuf)
        return tcp_close(pcb);

    if (err != ERR_OK)
    {
        pbuf_free(current_pbuf);
        return err;
    }

    /* 遍历 pbuf 链, 把数据存入接收环形缓冲 */
    while (current_pbuf != NULL)
    {
        if (current_pbuf->len > 0)
            written = (uint16_t)(written + fifo_uni_write(&s_rx_fifo, (uint8_t*)current_pbuf->payload, current_pbuf->len));
        current_pbuf = current_pbuf->next;
    }

    tcp_recved(pcb, buf->tot_len); /* 告诉 TCP: 数据已处理 */
    pbuf_free(buf);

    return ERR_OK;
}

/**
 *@brief tcp有新的客户端连接
 *@param[in] arg 用户自定义参数
 *@param[in] newpcb 新的tcp控制块
 *@param[in] err 错误码
 *@return 错误码
 */
static err_t server_accept(void* arg, struct tcp_pcb* newpcb, err_t err)
{
    COMPAT_IGNORE_RESULT(arg);
    if (err != ERR_OK || newpcb == NULL)
        return ERR_VAL;
    SYS_LOGI(k_tag, "client connected\r\n");

    tcp_recv(newpcb, server_recv);

    return ERR_OK;
}

int tcp_server_init(int port)
{
    err_t ret;

    fifo_uni_init(&s_rx_fifo, FIFO_TYPE_BYTE, s_rx_buf, TCP_RX_BUF_SIZE, 1);

    server_pcb = tcp_new();

    if (server_pcb == NULL)
    {
        SYS_LOGE(k_tag, "tcp_new failed\r\n");
        return ERR_MEM;
    }

    ret = tcp_bind(server_pcb, IP_ADDR_ANY, port);

    if (ret != ERR_OK)
    {
        SYS_LOGE(k_tag, "tcp_bind failed\r\n");

        tcp_close(server_pcb);
        server_pcb = NULL;

        return ERR_CONN;
    }
    server_pcb = tcp_listen(server_pcb);

    if (server_pcb == NULL)
    {
        SYS_LOGE(k_tag, "tcp_listen failed\r\n");
        return ret;
    }

    tcp_accept(server_pcb, server_accept);

    SYS_LOGI(k_tag, "TCP server listen %d\r\n", port);

    return ERR_OK;
}

int get_tcp_data(char* buf, int len, int* recv_len)
{
    uint16_t bytes_read;

    if (!buf || len <= 0 || !recv_len)
        return ERR_ARG;

    bytes_read = fifo_uni_read(&s_rx_fifo, (uint8_t*)buf, (uint16_t)len);
    *recv_len = (int)bytes_read;

    return ERR_OK;
}
