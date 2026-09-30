/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file tls_client.c
 * @brief TLS 客户端通道实现 (lwIP altcp_tls 直连封装)
 * @author H-000-H
 * @details 加密、握手、数据包拆分全部由 lwIP 自带的 altcp_tls 完成,
 *          本文件只做三件事:
 *          1. 创建 TLS 配置 (可选带 CA 证书) 并发起连接;
 *          2. 收到数据时把解密后的内容搬进 RX 缓冲;
 *          3. 把 TX 缓冲里待发的数据分块交给 altcp 加密发出。
 *          与明文 TCP 的区别: connected 回调会在 TLS 握手完成后才触发;
 *          断连时还要释放 TLS 配置和随机数相关的共享资源。
 */
#include "tls_client.h"

#include "compiler_compat.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "mbedtls/platform_time.h"
#include "mini_time.h"
#include "system_log.h"

static const char* const s_kTag = "tls_client";

_Static_assert((TLS_CLIENT_TX_BUFFER_SIZE & (TLS_CLIENT_TX_BUFFER_SIZE - 1U)) == 0U, "TLS_CLIENT_TX_BUFFER_SIZE must be power of 2");
_Static_assert((TLS_CLIENT_RX_BUFFER_SIZE & (TLS_CLIENT_RX_BUFFER_SIZE - 1U)) == 0U, "TLS_CLIENT_RX_BUFFER_SIZE must be power of 2");

/* -------------------------------------------------------------------------- */
/* 随机数取数接口 (板上应替换为真实硬件随机源)                                */
/* -------------------------------------------------------------------------- */
/**
 * @brief 为 TLS 提供随机数的默认实现 (占位用)
 * @param[out] data   存放随机数的缓冲
 * @param[in]  len    需要多少字节
 * @param[out] olen   实际产生了多少字节
 * @return int 0 成功; 负值为 mbedtls 错误码
 * @note 默认实现只是让代码能链接通过, 产生的数不是真随机,
 *       用在这种状态下握手密钥可能被猜到, 等于没加密。
 *       板上如有硬件随机数发生器, 请写一个同名函数替换掉本实现。
 */
__attribute__((weak)) mt_err_t mbedtls_hardware_poll(void* data, size_t len, size_t* olen)
{
    static uint32_t s_lfsr = 0x5EED5EEDU;
    uint8_t*        out = (uint8_t*)data;

    for (size_t i = 0; i < len; i++)
    {
        /* 占位的伪随机算法, 不能当真正的随机源用 */
        uint32_t lsb = s_lfsr & 1U;
        s_lfsr >>= 1;
        if (lsb != 0U)
            s_lfsr ^= 0x80200003U;
        out[i] = (uint8_t)(s_lfsr ^ (s_lfsr >> 8) ^ (s_lfsr >> 16) ^ (s_lfsr >> 24));
    }
    *olen = len;
    return 0;
}

/* -------------------------------------------------------------------------- */
/* mbedtls 时钟源 (裸机无 RTC, 接 mini 毫秒时钟)                              */
/* -------------------------------------------------------------------------- */
/**
 * @brief mbedtls time 回调 (MBEDTLS_PLATFORM_TIME_ALT)
 * @param[out] t 可选输出, 不为 NULL 时顺便把当前时间写进去
 * @return mbedtls_time_t 当前时间 (秒, 自开机起)
 * @note mbedtls 只用它记录会话建立的时刻, 不需要真实日期时间;
 *       不开这个, lwIP 里会话复用的代码会编译失败。
 */
static mbedtls_time_t tls_mbedtls_time(mbedtls_time_t* t)
{
    mbedtls_time_t now = (mbedtls_time_t)(mini_time_ms() / 1000U);
    if (t != NULL)
        *t = now;
    return now;
}

/* -------------------------------------------------------------------------- */
/* altcp 回调                                                                 */
/* -------------------------------------------------------------------------- */
/**
 * @brief TLS 客户端错误回调 (链路异常, lwIP 已释放控制块)
 * @param[in] arg 回调上下文 (tls_client_context)
 * @param[in] err lwIP 错误码
 */
static void tls_client_error_callback(void* arg, err_t err)
{
    struct tls_client_context* ctx = (struct tls_client_context*)arg;
    if (ctx != NULL)
    {
        MT_LOG_ERROR(s_kTag, "err occurred: %d\r\n", err);
        ctx->pcb = NULL; /* lwIP 内部已释放控制块, 此处不调用 altcp_close */
        ctx->is_connected = false;
    }
}

/**
 * @brief TLS 客户端接收回调 (对端数据已解密)
 * @param[in] arg     回调上下文 (tls_client_context)
 * @param[in] conn    altcp 控制块
 * @param[in] pbuf_in 解密后的数据链 (NULL 表示对端正常关闭)
 * @param[in] err     接收错误码
 * @return err_t ERR_OK 处理成功
 */
static err_t tls_client_recv_callback(void* arg, struct altcp_pcb* conn, struct pbuf* pbuf_in, err_t err)
{
    struct tls_client_context* ctx = (struct tls_client_context*)arg;

    if (ctx == NULL || ctx->pcb != conn)
    {
        if (pbuf_in != NULL)
            pbuf_free(pbuf_in);
        return ERR_VAL;
    }

    /* 对端关闭 (FIN / close_notify 已透传) */
    if (pbuf_in == NULL)
    {
        MT_LOG_INFO(s_kTag, "server disconnected\r\n");
        ctx->is_connected = false;
        if (ctx->pcb != NULL)
        {
            altcp_arg(ctx->pcb, NULL);
            ctx->pcb = NULL; /* altcp_close 内部会释放外层控制块 */
        }
        if (altcp_close(conn) != ERR_OK)
            altcp_abort(conn); /* 关闭失败(有未发数据)时中止, 防止控制块悬挂泄漏 */
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        MT_LOG_ERROR(s_kTag, "recv callback error: %d\r\n", err);
        pbuf_free(pbuf_in);
        return err;
    }

    struct pbuf* pbuf_walk = pbuf_in;
    uint16_t     total_bytes = 0;

    while (pbuf_walk != NULL)
    {
        if (pbuf_walk->len > 0)
        {
            uint16_t written = 0;
            MINI_IGNORE_RESULT(fifo_uni_write_block(&ctx->rx_fifo, (const uint8_t*)pbuf_walk->payload, pbuf_walk->len, &written));
            total_bytes = (uint16_t)(total_bytes + written);
            if (written < pbuf_walk->len)
                MT_LOG_WARN(s_kTag, "RX FIFO full, dropped %u bytes\r\n", (unsigned int)(pbuf_walk->len - written));
        }
        pbuf_walk = pbuf_walk->next;
    }

    /* 已入 FIFO 的字节才确认窗口; 溢出丢弃部分不确认 (与对端形成背压) */
    if (total_bytes > 0)
        altcp_recved(conn, total_bytes);

    pbuf_free(pbuf_in);
    return ERR_OK;
}

/**
 * @brief TLS 客户端发送回调 (数据已被对端确认, 继续推 TX FIFO)
 * @param[in] arg  回调上下文 (tls_client_context)
 * @param[in] conn altcp 控制块
 * @param[in] len  本次确认的应用层字节数
 * @return err_t ERR_OK
 */
static err_t tls_client_sent_callback(void* arg, struct altcp_pcb* conn, uint16_t len)
{
    MINI_IGNORE_RESULT(conn);
    MINI_IGNORE_RESULT(len);
    struct tls_client_context* ctx = (struct tls_client_context*)arg;
    if (ctx != NULL)
        MINI_IGNORE_RESULT(tls_client_poll_send(ctx));
    return ERR_OK;
}

/**
 * @brief TLS 客户端连接回调 (TCP + TLS 握手全部完成才会进来)
 * @param[in] arg  回调上下文 (tls_client_context)
 * @param[in] conn altcp 控制块
 * @param[in] err  连接错误码
 * @return err_t ERR_OK
 */
static err_t tls_client_connected_callback(void* arg, struct altcp_pcb* conn, err_t err)
{
    struct tls_client_context* ctx = (struct tls_client_context*)arg;
    if (ctx == NULL || err != ERR_OK)
    {
        MT_LOG_ERROR(s_kTag, "connected callback error: %d\r\n", err);
        if (ctx != NULL)
            ctx->is_connected = false;
        return err;
    }

    MT_LOG_INFO(s_kTag, "tls connected to %s:%u\r\n", ctx->server_ip, ctx->port);
    ctx->is_connected = true;

    /* 握手完成后才注册数据回调; TX FIFO 里有预压数据时立即触发推送 */
    altcp_recv(conn, tls_client_recv_callback);
    altcp_sent(conn, tls_client_sent_callback);
    MINI_IGNORE_RESULT(tls_client_poll_send(ctx));
    return ERR_OK;
}

/* -------------------------------------------------------------------------- */
/* 收发                                                                       */
/* -------------------------------------------------------------------------- */
int tls_client_poll_send(struct tls_client_context* ctx)
{
    if (ctx == NULL || !ctx->is_connected || ctx->pcb == NULL)
        return ERR_VAL;

    uint16_t snd_buf_avail = altcp_sndbuf(ctx->pcb);
    if (snd_buf_avail == 0)
        return ERR_MEM;

    uint8_t  temp_buf[256];
    uint16_t chunk = (uint16_t)((snd_buf_avail < sizeof(temp_buf)) ? snd_buf_avail : sizeof(temp_buf));

    uint16_t read_bytes = 0;
    /* FIFO 空时 read_block 返回 BUFF_ERR_EMPTY, read_bytes 保持 0 */
    MINI_IGNORE_RESULT(fifo_uni_read_block(&ctx->tx_fifo, temp_buf, chunk, &read_bytes));
    if (read_bytes == 0)
        return ERR_OK;

    /* 每次写满一块再交给 altcp, 它内部会把数据加密后发出 */
    err_t err = altcp_write(ctx->pcb, temp_buf, read_bytes, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK)
    {
        MT_LOG_WARN(s_kTag, "altcp_write failed: %d, retry later\r\n", err);
        return err;
    }

    MINI_IGNORE_RESULT(altcp_output(ctx->pcb));
    return ERR_OK;
}

int tls_client_send(struct tls_client_context* ctx, const void* data, uint16_t len, uint16_t* sent_len)
{
    if (ctx == NULL || data == NULL || len == 0)
        return ERR_ARG;

    if (!ctx->is_connected || ctx->pcb == NULL)
        return ERR_CONN;

    uint16_t written = 0;
    MINI_IGNORE_RESULT(fifo_uni_write_block(&ctx->tx_fifo, (const uint8_t*)data, len, &written));

    MINI_IGNORE_RESULT(tls_client_poll_send(ctx));

    if (sent_len != NULL)
        *sent_len = written;
    return ERR_OK;
}

int tls_client_read(struct tls_client_context* ctx, void* buf, uint16_t len, uint16_t* recv_len)
{
    if (ctx == NULL || buf == NULL || len == 0 || recv_len == NULL)
        return ERR_ARG;

    uint16_t read_bytes = 0;
    MINI_IGNORE_RESULT(fifo_uni_read_block(&ctx->rx_fifo, (uint8_t*)buf, len, &read_bytes));
    *recv_len = read_bytes;
    return ERR_OK;
}

/* -------------------------------------------------------------------------- */
/* 建连 / 断连                                                                */
/* -------------------------------------------------------------------------- */
int tls_client_init_and_connect(struct tls_client_context* ctx, const char* server_ip, uint16_t port, const uint8_t* ca_cert, uint32_t ca_len)
{
    ip_addr_t dest_ip;
    err_t     ret;

    if (ctx == NULL || server_ip == NULL || port == 0)
        return ERR_ARG;

    ctx->server_ip = server_ip;
    ctx->port = port;
    ctx->is_connected = false;
    ctx->pcb = NULL;
    ctx->tls_config = NULL;

    MINI_IGNORE_RESULT(fifo_uni_init(&ctx->rx_fifo, ctx->rx_buffer, 1U, TLS_CLIENT_RX_BUFFER_SIZE));
    MINI_IGNORE_RESULT(fifo_uni_init(&ctx->tx_fifo, ctx->tx_buffer, 1U, TLS_CLIENT_TX_BUFFER_SIZE));

    /* 解析 IP */
    if (!ip_addr_aton(server_ip, &dest_ip))
    {
        MT_LOG_ERROR(s_kTag, "invalid IP: %s\r\n", server_ip);
        return ERR_ARG;
    }

    /* 注册时钟源 (重复注册无副作用, 保持每次建连自洽) */
    MINI_IGNORE_RESULT(mbedtls_platform_set_time(tls_mbedtls_time));

    /* 创建 TLS 客户端配置: CA 为 NULL 时不校验服务器证书 (仅调试) */
    ctx->tls_config = altcp_tls_create_config_client(ca_cert, (size_t)ca_len);
    if (ctx->tls_config == NULL)
    {
        MT_LOG_ERROR(s_kTag, "altcp_tls_create_config_client failed\r\n");
        return ERR_MEM;
    }

    /* 创建 altcp 控制块 (外层, 内层 TCP 由 altcp_tls 自建) */
    ctx->pcb = altcp_tls_new(ctx->tls_config, IPADDR_TYPE_V4);
    if (ctx->pcb == NULL)
    {
        MT_LOG_ERROR(s_kTag, "altcp_tls_new failed\r\n");
        altcp_tls_free_config(ctx->tls_config);
        ctx->tls_config = NULL;
        return ERR_MEM;
    }

    altcp_arg(ctx->pcb, ctx);
    altcp_err(ctx->pcb, tls_client_error_callback);

    /* connected 回调在 TLS 握手完成后才会触发 */
    ret = altcp_connect(ctx->pcb, &dest_ip, port, tls_client_connected_callback);
    if (ret != ERR_OK)
    {
        MT_LOG_ERROR(s_kTag, "altcp_connect failed: %d\r\n", ret);
        altcp_close(ctx->pcb);
        ctx->pcb = NULL;
        altcp_tls_free_config(ctx->tls_config);
        ctx->tls_config = NULL;
        return ret;
    }

    MT_LOG_INFO(s_kTag, "connecting (tls) to %s:%u...\r\n", server_ip, port);
    return ERR_OK;
}

int tls_client_disconnect(struct tls_client_context* ctx)
{
    if (ctx == NULL)
        return ERR_ARG;

    if (ctx->pcb != NULL)
    {
        struct altcp_pcb* pcb = ctx->pcb;
        altcp_arg(pcb, NULL);
        altcp_recv(pcb, NULL);
        altcp_sent(pcb, NULL);
        altcp_err(pcb, NULL);
        ctx->pcb = NULL;
        if (altcp_close(pcb) != ERR_OK)
            altcp_abort(pcb); /* 关闭失败(有未发数据)时中止, 防止控制块悬挂泄漏 */
    }

    if (ctx->tls_config != NULL)
    {
        altcp_tls_free_config(ctx->tls_config);
        ctx->tls_config = NULL;
        /* 所有配置都释放完后, 才能归还共用的随机数资源 */
        altcp_tls_free_entropy();
    }

    ctx->is_connected = false;
    MT_LOG_INFO(s_kTag, "tls client disconnected\r\n");
    return ERR_OK;
}
