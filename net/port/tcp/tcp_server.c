/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file tcp_server.c
 * @brief TCP Server 多客户端支持 (lwIP Raw API 驱动)
 * @note 每个连接独立分配一个 SPSC 无锁统一 FIFO，保证多客户端并发时数据完全隔离、不串流。
 * @note SPSC 约束: 每个 session 的 FIFO 生产者是 lwIP tcpip 线程, 消费侧必须保证同一
 *       session_id 只由一个线程读取 (多线程读同一 session 即退化为多消费者, 产生数据竞争)。
 */
#include "tcp_server.h"

#include "buffer.h"
#include "compiler_compat.h"
#include "string.h"
#include "system_log.h"

static const char* const k_tag = "tcp_server";

#define MAX_CLIENT_COUNT CONFIG_TCP_SERVER_MAX_CLIENTS
#define CLIENT_RX_BUF_SIZE CONFIG_TCP_RX_BUF_SIZE

_Static_assert((CLIENT_RX_BUF_SIZE & (CLIENT_RX_BUF_SIZE - 1U)) == 0U, "CONFIG_TCP_RX_BUF_SIZE must be power of 2");

/**
 * @brief 单个客户端会话结构体
 * @note rx_fifo 为 SPSC: 生产者 = lwIP tcpip 线程 (tcp_server_receive_callback),
 *       消费者 = 调用 get_tcp_data_by_session 的应用线程 (仅限一个)
 */
struct client_session
{
    int id; /* 会话 ID (0 ~ MAX_CLIENT_COUNT-1) */
    volatile bool is_used; /* 占用标志位 */
    struct tcp_pcb* pcb; /* 对应的 lwIP TCP 控制块 */
    struct fifo_uni_spsc rx_fifo; /* 独立的统一 BUFF (item_size=1, 字节流) */
    uint8_t rx_buf[CLIENT_RX_BUF_SIZE]; /* 独立的接收缓冲物理内存 */
};

static struct tcp_pcb* s_listen_pcb = NULL;
static struct client_session s_sessions[MAX_CLIENT_COUNT];

/**
 * @brief 释放会话资源并复位状态 (单一清理点)
 * @param[in] session 待释放的会话, NULL 时直接返回
 * @param[in] pcb_alive pcb 是否仍存活: true 由本函数先解绑三个回调再复位状态;
 *            false 表示 pcb 已被 lwIP 释放或调用方已解绑, 仅复位会话状态
 * @note 前置条件必须严格成立: pcb 已释放时绝对不能再碰它 (调用任何 tcp_* 都是 UAF)
 */
static void release_session(struct client_session* session, bool pcb_alive)
{
    if (session == NULL)
        return;
    if (pcb_alive && session->pcb != NULL)
    {
        tcp_arg(session->pcb, NULL);
        tcp_recv(session->pcb, NULL);
        tcp_err(session->pcb, NULL);
    }
    session->pcb = NULL;
    session->is_used = false;
}

/**
 * @brief TCP 连接异常回调（网络超时、收到 RST、异常断开）
 * @param[in] arg lwIP 回调上下文 (绑定为 session 指针)
 * @param[in] err lwIP 错误码
 * @note 发生错误时，lwIP 内部已自动释放该 pcb，此回调中绝对不能再调用 tcp_close()
 */
static void tcp_server_error_callback(void* arg, err_t err)
{
    struct client_session* session = (struct client_session*)arg;
    if (session != NULL)
    {
        SYS_LOGE(k_tag, "session [%d] err occurred: %d\r\n", session->id, err);
        /* lwIP 在调用本回调前已释放 pcb, 回调随 pcb 一并失效,
         * 无需也无法再解绑回调, 只复位会话状态, 绝不能再碰 pcb */
        release_session(session, false);
    }
}

/**
 * @brief TCP 服务端接收数据回调
 * @param[in] arg lwIP 回调上下文 (绑定为 session 指针)
 * @param[in] pcb 触发回调的 TCP 控制块
 * @param[in] buf 接收到的 pbuf 链 (NULL 表示对端正常关闭)
 * @param[in] err 接收错误码
 * @return ERR_OK 处理成功, 其它为 lwIP 错误码
 */
static err_t tcp_server_receive_callback(void* arg, struct tcp_pcb* pcb, struct pbuf* buf, err_t err)
{
    struct client_session* session = (struct client_session*)arg;

    if (session == NULL || session->pcb != pcb)
    {
        if (buf != NULL)
            pbuf_free(buf);
        return ERR_VAL;
    }

    /* 1. 客户端正常关闭连接 (收到 FIN) */
    if (buf == NULL)
    {
        SYS_LOGI(k_tag, "session [%d] client disconnected\r\n", session->id);
        release_session(session, true); /* 先解绑回调: tcp_close 可能当场释放 pcb */
        if (tcp_close(pcb) != ERR_OK)
            tcp_abort(pcb); /* 关闭失败(极少见)时中止, 防止 PCB 悬挂泄漏 */
        return ERR_OK;
    }

    /* 2. 接收网络错误 */
    if (err != ERR_OK)
    {
        pbuf_free(buf);
        return err;
    }

    /* 3. 遍历 pbuf 链并写入当前 session 专属的 FIFO */
    struct pbuf* q = buf;
    uint16_t total_in = 0;
    uint16_t total_written = 0;

    while (q != NULL)
    {
        total_in = (uint16_t)(total_in + q->len);
        if (q->len > 0)
        {
            uint16_t written = 0;
            COMPAT_IGNORE_RESULT(fifo_uni_write_block(&session->rx_fifo, q->payload, q->len, &written));
            total_written = (uint16_t)(total_written + written);

            if (written < q->len)
                SYS_LOGW(k_tag, "session [%d] FIFO full, dropped %u bytes\r\n", session->id, (unsigned int)(q->len - written));
        }
        q = q->next;
    }

    /* 4. 确认全部来字节 (含溢出丢弃部分): 丢掉的字节若既不收也不确认,
     *    会永久占住 TCP 接收窗口, 碎片小包累积后窗口耗尽 → 连接停滞 */
    if (total_in > 0)
        tcp_recved(pcb, total_in);

    pbuf_free(buf);
    return ERR_OK;
}

/**
 * @brief 新客户端连入回调
 * @param[in] arg accept 回调上下文 (未使用)
 * @param[in] newpcb 新连接的 TCP 控制块
 * @param[in] err 连接错误码
 * @return ERR_OK 接受连接, ERR_ABRT 连接已满拒绝, 其它为错误拒绝
 */
static err_t tcp_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err)
{
    COMPAT_IGNORE_RESULT(arg);

    if (err != ERR_OK || newpcb == NULL)
        return ERR_VAL;

    /* 寻找空闲的 session 槽位 */
    struct client_session* free_session = NULL;
    for (int i = 0; i < MAX_CLIENT_COUNT; i++)
    {
        if (!s_sessions[i].is_used)
        {
            free_session = &s_sessions[i];
            break;
        }
    }

    /* 连接已满，直接拒绝新连接 */
    if (free_session == NULL)
    {
        SYS_LOGW(k_tag, "max connections reached (%d), reject client\r\n", MAX_CLIENT_COUNT);
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    /* 初始化当前会话: 先就绪 pcb 与 FIFO, 最后置位 is_used, 避免读者看到已占用但 FIFO 未初始化 */
    free_session->pcb = newpcb;
    COMPAT_IGNORE_RESULT(fifo_uni_init(&free_session->rx_fifo, free_session->rx_buf, 1U, CLIENT_RX_BUF_SIZE));
    free_session->is_used = true;

    SYS_LOGI(k_tag, "new client accepted -> session [%d]\r\n", free_session->id);

    /* 绑定 session 实例到 lwIP 回调上下文 arg */
    tcp_arg(newpcb, free_session);
    tcp_recv(newpcb, tcp_server_receive_callback);
    tcp_err(newpcb, tcp_server_error_callback);

    return ERR_OK;
}

int tcp_server_init(int port)
{
    err_t ret;

    /* 初始化全局会话列表 */
    for (int i = 0; i < MAX_CLIENT_COUNT; i++)
    {
        s_sessions[i].id = i;
        s_sessions[i].is_used = false;
        s_sessions[i].pcb = NULL;
    }

    s_listen_pcb = tcp_new();
    if (s_listen_pcb == NULL)
    {
        SYS_LOGE(k_tag, "tcp_new failed\r\n");
        return ERR_MEM;
    }

    ret = tcp_bind(s_listen_pcb, IP_ADDR_ANY, (u16_t)port);
    if (ret != ERR_OK)
    {
        SYS_LOGE(k_tag, "tcp_bind port %d failed: %d\r\n", port, ret);
        tcp_close(s_listen_pcb);
        s_listen_pcb = NULL;
        return ERR_CONN;
    }

    s_listen_pcb = tcp_listen(s_listen_pcb);
    if (s_listen_pcb == NULL)
    {
        SYS_LOGE(k_tag, "tcp_listen failed\r\n");
        return ERR_MEM;
    }

    tcp_accept(s_listen_pcb, tcp_server_accept_callback);
    SYS_LOGI(k_tag, "TCP server listening on port %d (Max clients: %d)\r\n", port, MAX_CLIENT_COUNT);

    return ERR_OK;
}

int get_tcp_data_by_session(int session_id, char* buf, int len, int* recv_len)
{
    if (session_id < 0 || session_id >= MAX_CLIENT_COUNT || !buf || len <= 0 || !recv_len)
        return ERR_ARG;

    struct client_session* session = &s_sessions[session_id];

    if (!session->is_used)
    {
        *recv_len = 0;
        return ERR_CONN;
    }

    uint16_t bytes_read = 0;

    COMPAT_IGNORE_RESULT(fifo_uni_read_block(&session->rx_fifo, (uint8_t*)buf, (uint16_t)len, &bytes_read));
    *recv_len = (int)bytes_read;

    return ERR_OK;
}

int get_tcp_data(char* buf, int len, int* recv_len)
{
    if (!buf || len <= 0 || !recv_len)
        return ERR_ARG;

    /* 兼容单客户端用法: 从第一个已连接的 session 读取 */
    for (int i = 0; i < MAX_CLIENT_COUNT; i++)
        if (s_sessions[i].is_used)
            return get_tcp_data_by_session(i, buf, len, recv_len);

    *recv_len = 0;
    return ERR_CONN;
}

bool is_session_connected(int session_id)
{
    if (session_id < 0 || session_id >= MAX_CLIENT_COUNT)
        return false;
    return s_sessions[session_id].is_used;
}

int close_session(int session_id)
{
    if (session_id < 0 || session_id >= MAX_CLIENT_COUNT)
        return ERR_ARG;

    struct client_session* session = &s_sessions[session_id];
    if (session->is_used && session->pcb != NULL)
    {
        struct tcp_pcb* pcb = session->pcb;
        release_session(session, true); /* 先解绑回调并复位状态, 防 close 挥手期回调误入已复用槽位 */
        if (tcp_close(pcb) != ERR_OK)
            tcp_abort(pcb); /* 关闭失败(有未发数据)时中止, 防止 PCB 悬挂泄漏 */
    }

    return ERR_OK;
}