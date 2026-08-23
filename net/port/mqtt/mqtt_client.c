/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file mqtt_client.c
 * @brief MQTT Client Implementation
 * @details mqtt报文:[固定头字节1][剩余长度(本函数编码，1‑4字节)][可变头][有效负载payload]
 */
#include "mqtt_client.h"

#include "compiler_compat.h"
#include "lwip/tcp.h"

static const char* k_tag = "mqtt_client";

#define MQTT_MSG_TYPE_CONNECT 0x10 /**< CONNECT */
#define MQTT_MSG_TYPE_CONNACK 0x20 /**< CONNACK */
#define MQTT_MSG_TYPE_PUBLISH 0x30 /**< PUBLISH */
#define MQTT_MSG_TYPE_PUBACK 0x40 /**< PUBACK */
#define MQTT_MSG_TYPE_SUBSCRIBE 0x80 /**< SUBSCRIBE */
#define MQTT_MSG_TYPE_SUBACK 0x90 /**< SUBACK */
#define MQTT_MSG_TYPE_PINGREQ 0xC0 /**< PINGREQ */
#define MQTT_MSG_TYPE_PINGRESP 0xD0 /**< PINGRESP */
#define MQTT_MSG_TYPE_DISCONNECT 0xE0 /**< DISCONNECT */

#define MQTT_MESSAGE_MAX_LENGTH 0x0FFFFFFF /**< MQTT消息最大长度4字节可变长 每个的bit7写死 4个就是28 所以最大2^28-1 = 268435456= 0xFFFFFF */

/**
 * @brief MQTT UTF‑8字符串编码，2字节大端长度前缀 + 字符内容
 * @param[out] buf      输出缓冲区，存放编码后的MQTT字符串，调用者保证空间充足
 * @param[in]  str      输入以'\0'结尾的UTF‑8字符串
 * @param[out] recv_size     输出参数，返回编码完成总字节数(2字节前缀+字符串字节)
 * @return int          ERR_OK 成功；ERR_VAL 参数非法(NULL/字符串超长)
 */
static int mqtt_encode_string(uint8_t* buf, const char* str, uint16_t* recv_size)
{
    if (str == NULL || buf == NULL || recv_size == NULL)
        return ERR_VAL;

    size_t slen = strlen(str);
    /** 字符串长度超出范围 MQTT 最大65535 */
    if (slen > UINT16_MAX)
        return ERR_VAL;

    uint16_t len = (uint16_t)slen;
    buf[0] = (uint8_t)(len >> 8);
    buf[1] = (uint8_t)(len & 0xFF);
    memcpy(&buf[2], str, len);

    *recv_size = len + 2U;

    return ERR_OK;
}

/**
 * @brief MQTT 剩余长度编码
 * @param[out] buf      输出缓冲区，存放编码后的剩余长度，调用者保证空间充足
 * @param[in]  length   输入剩余长度
 * @param[out] recv_bytes 输出参数，返回编码完成总字节数
 * @return int          ERR_OK 成功；ERR_VAL 参数非法(NULL)
 * @details - 剩余长度编码规则：每个字节的最高位为1，表示还有后续字节；最低7位为剩余长度的1‑128的倍数
            - 把普通数字，编码成 MQTT 协议特有的 Base128 可变字节格式的「剩余长度」，输出到缓冲区，
            - 并返回该字段实际占用多少字节，用于组装 MQTT 数据包的报文头
 */
static int mqtt_encode_remaining_len(uint8_t* buf, uint32_t length, uint8_t* recv_bytes)
{
    if (buf == NULL || recv_bytes == NULL || length > MQTT_MESSAGE_MAX_LENGTH)
        return ERR_VAL;

    uint8_t bytes = 0;
    do
    {
        uint8_t encoded_byte = length % 128;
        length /= 128;
        if (length > 0)
            encoded_byte |= 0x80;
        buf[bytes++] = encoded_byte;
    } while (length > 0);
    *recv_bytes = bytes;
    return ERR_OK;
}

/**
 * @brief 发送原始数据
 * @param[in] ctx   MQTT 客户端上下文
 * @param[in] data  数据指针
 * @param[in] len   数据长度
 * @return bool     true 成功；false 失败
 */
static bool mqtt_send_raw(struct mqtt_client_context* ctx, uint8_t* data, uint16_t len)
{
    if (ctx == NULL || data == NULL)
        return false;
    if (len == 0)
        SYS_LOGW(k_tag, "mqtt_send_raw len is 0");
    uint16_t rd = 0;
    COMPAT_IGNORE_RESULT(fifo_uni_read_block(&ctx->tx_fifo, data, len, &rd));
    tcp_write(ctx->pcb, data, len, TCP_WRITE_FLAG_COPY);
    tcp_output(ctx->pcb);
    return true;
}

int mqtt_client_init(struct mqtt_client_context* ctx)
{
    if (!ctx)
        return ERR_VAL;

    ctx->is_tcp_connected = false;
    ctx->is_mqtt_connected = false;
    ctx->sub_count = 0;
    ctx->packet_id_counter = 1; /** < 包ID计数器，初始值为1 */
    ctx->last_ping_timestamp = 0;
    ctx->last_rx_timestamp = 0;

    COMPAT_IGNORE_RESULT(fifo_uni_init(&ctx->tx_fifo, ctx->tx_buffer, MQTT_BYTE_SIZE, MQTT_TX_BUFFER_SIZE));
    COMPAT_IGNORE_RESULT(fifo_uni_init(&ctx->rx_fifo, ctx->rx_buffer, MQTT_BYTE_SIZE, MQTT_RX_BUFFER_SIZE));
    return ERR_OK;
}

/**
 * @brief 发送 MQTT CONNECT 包
 * @param[in] ctx   MQTT 客户端上下文
 * @return int      ERR_OK 成功；其他值失败
 */
static int mqtt_send_connect_packet(struct mqtt_client_context* ctx)
{
    if (!ctx)
        return ERR_VAL;
    return ERR_OK;
}
