/**
 * @file tcp_client.c
 * @author H-000-H
 * @brief TCP 客户端实现文件
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#include "tcp_client.h"

#include "compiler_compat.h"
#include "lwip/err.h"
#include "system_log.h"

static const char *const tag = "tcp_client";
_Static_assert((TCP_CLIENT_TX_BUFFER_SIZE & (TCP_CLIENT_TX_BUFFER_SIZE - 1U)) == 0U,
	       "TCP_CLIENT_TX_BUFFER_SIZE must be power of 2");
_Static_assert((TCP_CLIENT_RX_BUFFER_SIZE & (TCP_CLIENT_RX_BUFFER_SIZE - 1U)) == 0U,
	       "TCP_CLIENT_RX_BUFFER_SIZE must be power of 2");

#if NO_SYS == 0
/* RTOS 模式: 应用线程与 lwIP sent 回调都可能进 poll_send, 用互斥锁串行化 */
#define TCP_CLIENT_TX_LOCK(ctx)                                        \
	do {                                                               \
		if (sys_mutex_valid(&(ctx)->tx_lock))                      \
			sys_mutex_lock(&(ctx)->tx_lock);                     \
	} while (0)
#define TCP_CLIENT_TX_UNLOCK(ctx)                                      \
	do {                                                               \
		if (sys_mutex_valid(&(ctx)->tx_lock))                      \
			sys_mutex_unlock(&(ctx)->tx_lock);                   \
	} while (0)
#else
/* 裸机单线程: 全部回调和应用在同一上下文, 无竞争 */
#define TCP_CLIENT_TX_LOCK(ctx)   ((void)0)
#define TCP_CLIENT_TX_UNLOCK(ctx) ((void)0)
#endif

static int tcp_client_poll_send_locked(struct tcp_client_context *ctx)
{
	if (ctx == NULL || !ctx->is_connected || ctx->pcb == NULL)
		return ERR_VAL;

	uint16_t snd_buf_avail = tcp_sndbuf(ctx->pcb);

	if (snd_buf_avail == 0)
		return ERR_MEM;

	/* 先把上次没交给 lwIP 的数据发出去: FIFO 已经消费过, 丢了就断流了 */
	if (ctx->tx_staging_len == 0) {
		uint16_t chunk = (snd_buf_avail < sizeof(ctx->tx_staging))
					 ? snd_buf_avail
					 : (uint16_t)sizeof(ctx->tx_staging);
		uint16_t read_bytes = 0;

		/* FIFO 空时 read_block 返回 BUFF_ERR_EMPTY, read_bytes 保持 0 */
		MINI_IGNORE_RESULT(fifo_uni_read_block(&ctx->tx_fifo, ctx->tx_staging, chunk,
						       &read_bytes));
		if (read_bytes == 0)
			return ERR_OK; /* 没有待发数据, 属于正常情况, 不是错误 */

		ctx->tx_staging_len = read_bytes;
	}

	err_t err = tcp_write(ctx->pcb, ctx->tx_staging, ctx->tx_staging_len,
			      TCP_WRITE_FLAG_COPY);
	if (err != ERR_OK) {
		/* 数据留在 tx_staging, 等发送窗口释放后由 sent 回调重试 */
		MT_LOG_WARN(tag, "tcp_write failed: %d, keep %u bytes\r\n", err,
			    (unsigned int)ctx->tx_staging_len);
		return ERR_MEM;
	}

	ctx->tx_staging_len = 0;
	return ERR_OK;
}

/**
 * @brief 触发刷新 TX FIFO 中排队的数据到网卡
 * @note NO_SYS=0 (RT-Thread 等) 时这里会先取 tx_lock, 避免应用线程和 lwIP
 *       sent 回调同时改动 tx_fifo / tx_staging
 */
int tcp_client_poll_send(struct tcp_client_context *ctx)
{
	if (ctx == NULL)
		return ERR_ARG;

	int ret;

	TCP_CLIENT_TX_LOCK(ctx);
	ret = tcp_client_poll_send_locked(ctx);
	TCP_CLIENT_TX_UNLOCK(ctx);

	return ret;
}

/**
 * @brief TCP 客户端错误回调函数
 * @param[in] arg   回调函数参数
 * @param[in] err   错误码
 * @note 该函数在 TCP 连接错误时调用
 */
static void tcp_client_error_callback(void *arg, err_t err)
{
	struct tcp_client_context *ctx = (struct tcp_client_context *)arg;
	if (ctx != NULL) {
		MT_LOG_ERROR(tag, "client err occurred: %d\r\n", err);
		ctx->pcb	  = NULL; /* lwIP 内部已释放 PCB，此处不调用 tcp_close */
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
static err_t tcp_client_receive_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *current_buf,
					 err_t err)
{
	struct tcp_client_context *ctx = (struct tcp_client_context *)arg;

	if (ctx == NULL || ctx->pcb != pcb) {
		if (current_buf)
			pbuf_free(current_buf);
		return ERR_VAL;
	}

	/* 服务器断开连接 */
	if (current_buf == NULL) {
		MT_LOG_INFO(tag, "server disconnected\r\n");
		tcp_arg(pcb, NULL);
		tcp_recv(pcb, NULL);
		tcp_err(pcb, NULL);
		tcp_sent(pcb, NULL);
		tcp_close(pcb);
		ctx->pcb	  = NULL;
		ctx->is_connected = false;
		return ERR_OK;
	}

	if (err != ERR_OK) {
		MT_LOG_ERROR(tag, "tcp_client_receive_callback error: %d\r\n", err);
		pbuf_free(current_buf);
		return err;
	}

	struct pbuf *buf	 = current_buf;
	uint16_t     received_bytes = 0; /* 本次回调实际收到的全部字节 */

	while (buf) {
		if (buf->len > 0) {
			uint16_t written = 0;
			received_bytes = (uint16_t)(received_bytes + buf->len);
			MINI_IGNORE_RESULT(fifo_uni_write_block(
				&ctx->rx_fifo, (const uint8_t *)buf->payload, buf->len, &written));
			if (written < buf->len)
				MT_LOG_WARN(tag, "RX FIFO full, dropped %u bytes\r\n",
					    (unsigned int)(buf->len - written));
		}
		buf = buf->next;
	}

	/* 窗口不在这里归还: 数据只是进了 FIFO, 还没被应用消费.
	 * 归还点放在 tcp_client_read, 应用读得慢时窗口自然收缩, 对端跟着降速. */

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
static err_t tcp_client_sent_callback(void *arg, struct tcp_pcb *pcb, uint16_t len)
{
	MINI_IGNORE_RESULT(pcb);
	MINI_IGNORE_RESULT(len);
	struct tcp_client_context *ctx = (struct tcp_client_context *)arg;
	if (ctx != NULL) /* 发送窗口释放，尝试继续发 TX FIFO 里的下一批数据 */
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
static err_t tcp_client_connected_callback(void *arg, struct tcp_pcb *pcb, err_t err)
{
	struct tcp_client_context *ctx = (struct tcp_client_context *)arg;
	if (ctx == NULL || err != ERR_OK) {
		MT_LOG_ERROR(tag, "tcp_client_connected_callback error: %d\r\n", err);
		if (ctx)
			ctx->is_connected = false;
		return err;
	}
	MT_LOG_INFO(tag, "connected to %s:%u\r\n", ctx->server_ip, ctx->port);

	ctx->is_connected = true;

	/* 注册数据接收和错误回调 */
	tcp_recv(pcb, tcp_client_receive_callback);
	tcp_sent(pcb, tcp_client_sent_callback);

	tcp_client_poll_send(ctx);

	/* 连接建立后   如果 TX FIFO 里有提前压入的数据，立即触发一次发送 */
	return ERR_OK;
}

int tcp_client_init_and_connect(struct tcp_client_context *ctx, const char *server_ip,
				uint16_t port)
{
	ip_addr_t dest_ip;
	err_t	  ret;

	if (!ctx || !server_ip || port == 0)
		return ERR_ARG;
	ctx->server_ip	  = server_ip;
	ctx->port	  = port;
	ctx->is_connected = false;
	ctx->tx_staging_len = 0;

#if NO_SYS == 0
	if (!sys_mutex_valid(&ctx->tx_lock)) {
		err_t mutex_err = sys_mutex_new(&ctx->tx_lock);

		if (mutex_err != ERR_OK) {
			MT_LOG_ERROR(tag, "tx mutex create failed: %d\r\n", mutex_err);
			return mutex_err;
		}
	}
#endif

	MINI_IGNORE_RESULT(fifo_uni_init(&ctx->rx_fifo, ctx->rx_buffer, TCP_CLIENT_RX_TX_BYTE_TYPE,
					 TCP_CLIENT_RX_BUFFER_SIZE));
	MINI_IGNORE_RESULT(fifo_uni_init(&ctx->tx_fifo, ctx->tx_buffer, TCP_CLIENT_RX_TX_BYTE_TYPE,
					 TCP_CLIENT_TX_BUFFER_SIZE));

	/* 解析 IP */
	if (!ip4addr_aton(server_ip, &dest_ip)) {
		MT_LOG_ERROR(tag, "invalid IP: %s\r\n", server_ip);
		return ERR_ARG;
	}

	/* 创建 PCB */
	ctx->pcb = tcp_new();
	if (ctx->pcb == NULL) {
		MT_LOG_ERROR(tag, "tcp_new failed\r\n");
		return ERR_MEM;
	}

	/* 注入 Context 到 lwIP 回调上下文 arg */
	tcp_arg(ctx->pcb, ctx);
	tcp_err(ctx->pcb, tcp_client_error_callback);

	/* 连接服务器 */
	ret = tcp_connect(ctx->pcb, &dest_ip, port, tcp_client_connected_callback);

	if (ret != ERR_OK) {
		MT_LOG_ERROR(tag, "tcp_connect failed: %d\r\n", ret);
		tcp_close(ctx->pcb);
		ctx->pcb = NULL;
		return ret;
	}

	MT_LOG_INFO(tag, "connecting to %s:%d...\r\n", server_ip, port);
	return ERR_OK;
}

int tcp_client_send(struct tcp_client_context *ctx, const void *data, uint16_t len,
		    uint16_t *sent_len)
{
	if (!ctx || !data || len == 0)
		return ERR_ARG;

	if (!ctx->is_connected || ctx->pcb == NULL)
		return ERR_CONN;

	uint16_t written = 0;

	TCP_CLIENT_TX_LOCK(ctx);
	MINI_IGNORE_RESULT(
		fifo_uni_write_block(&ctx->tx_fifo, (const uint8_t *)data, len, &written));
	(void)tcp_client_poll_send_locked(ctx);
	TCP_CLIENT_TX_UNLOCK(ctx);

	if (sent_len)
		*sent_len = written;

	/* FIFO 满: 只收下了一部分, 调用方按 sent_len 重试剩余数据 */
	if (written < len)
		return ERR_MEM;

	return ERR_OK;
}

int tcp_client_read(struct tcp_client_context *ctx, void *buf, uint16_t len, uint16_t *recv_len)
{
	if (!ctx || !buf || len == 0 || !recv_len)
		return ERR_ARG;

	uint16_t read_bytes = 0;

	MINI_IGNORE_RESULT(fifo_uni_read_block(&ctx->rx_fifo, (uint8_t *)buf, len, &read_bytes));
	*recv_len = read_bytes;

	/* 应用真正取走数据之后才归还接收窗口, 保证 FIFO 不会先于窗口被填满 */
	if (read_bytes > 0 && ctx->pcb != NULL)
		tcp_recved(ctx->pcb, read_bytes);

	return ERR_OK;
}

int tcp_client_disconnect(struct tcp_client_context *ctx)
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
	ctx->is_connected   = false;
	ctx->tx_staging_len = 0;
	MT_LOG_INFO(tag, "client disconnected\r\n");
	return ERR_OK;
}
