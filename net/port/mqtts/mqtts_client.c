/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file mqtts_client.c
 * @brief MQTT over TLS Client Implementation (coreMQTT + lwIP altcp_tls)
 * @author H-000-H
 * @details 报文编解码/状态机/心跳/重发/订阅确认由 coreMQTT 负责, 加密/握手/
 *          记录分帧由 lwIP altcp_tls 负责 (经 tls_client 直连封装); 本文件只做:
 *          1. 上下文与 coreMQTT 的初始化绑定;
 *          2. TLS 建连 -> MQTT 握手的两段式连接驱动;
 *          3. 下行 PUBLISH 原样转发给唯一消息回调。
 *          注: 加密通道不经 transport_glue, send/recv 直接适配到 tls_client。
 */
#include "mqtts_client.h"

#include "compiler_compat.h"
#include "lwip/err.h"
#include "mini_time.h"
#include "system_log.h"
#include <string.h>

static const char* const s_kTag = "mqtts_client";

#define MQTTS_CONNACK_TIMEOUT_MS 10000 /**< CONNECT 发出后等待 CONNACK 的超时时间 */

#ifdef CONFIG_TLS_HANDSHAKE_TIMEOUT_MS
#define MQTTS_TLS_CONNECT_TIMEOUT_MS CONFIG_TLS_HANDSHAKE_TIMEOUT_MS
#else
#define MQTTS_TLS_CONNECT_TIMEOUT_MS 20000 /**< TCP + TLS 握手等待上限 */
#endif

/**
 * @brief QoS 数值转 coreMQTT 枚举 (越界截断到 QoS2)
 * @param[in] qos QoS 数值 (0/1/2)
 * @return MQTTQoS_t 对应枚举
 */
static MQTTQoS_t mqtts_qos_from_u8(uint8_t qos)
{
    if (qos == 0U)
        return MQTTQoS0;
    if (qos == 1U)
        return MQTTQoS1;
    return MQTTQoS2;
}

/**
 * @brief coreMQTT 时间源 (毫秒)
 * @return uint32_t 当前时间戳
 */
static uint32_t mqtts_get_time_ms(void) { return mini_time_ms(); }

/* -------------------------------------------------------------------------- */
/* 传输适配 (altcp_tls, 不经 transport_glue)                                  */
/* -------------------------------------------------------------------------- */
/**
 * @brief coreMQTT 发送适配: 数据写入加密通道 (TX FIFO -> altcp_write)
 * @param[in] net_ctx coreMQTT 网络上下文 (实际为 mqtts_network_context)
 * @param[in] buf 待发送数据
 * @param[in] len 数据长度
 * @return int32_t 实际入队字节数; 负值为错误; 0 表示缓冲满可重试
 */
static int32_t mqtts_transport_send(NetworkContext_t* net_ctx, const void* buf, size_t len)
{
    struct mqtts_network_context* ctx = (struct mqtts_network_context*)(void*)net_ctx;

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
 * @brief coreMQTT 接收适配: 从解密后的接收缓冲读取 (非阻塞)
 * @param[in]  net_ctx coreMQTT 网络上下文 (实际为 mqtts_network_context)
 * @param[out] buf     目标缓冲
 * @param[in]  len     期望读取长度
 * @return int32_t 实际读取字节数; 负值为错误; 0 表示暂无数据可重试
 */
static int32_t mqtts_transport_recv(NetworkContext_t* net_ctx, void* buf, size_t len)
{
    struct mqtts_network_context* ctx = (struct mqtts_network_context*)(void*)net_ctx;

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
/* coreMQTT 事件回调                                                          */
/* -------------------------------------------------------------------------- */
/**
 * @brief coreMQTT 事件回调: 下行 PUBLISH 转发消息回调, 其余事件记日志
 * @param[in] mqtt_context      coreMQTT 上下文 (经 container_of 还原包装层上下文)
 * @param[in] packet_info       报文信息 (类型等)
 * @param[in] deserialized_info 解析结果 (包标识/发布信息等)
 * @param[in] reason_code       MQTT5 原因码信息 (本层不处理)
 * @param[in] send_props_buffer 应答报文属性构造器 (本层不附加属性)
 * @param[in] get_props_buffer  接收报文属性解析器 (本层不解析属性)
 * @return bool true 事件已处理
 */
static bool mqtts_event_callback(MQTTContext_t* mqtt_context, MQTTPacketInfo_t* packet_info, MQTTDeserializedInfo_t* deserialized_info,
                                 MQTTSuccessFailReasonCode_t* reason_code, MQTTPropBuilder_t* send_props_buffer, MQTTPropBuilder_t* get_props_buffer)
{
    MINI_IGNORE_RESULT(reason_code);
    MINI_IGNORE_RESULT(send_props_buffer);
    MINI_IGNORE_RESULT(get_props_buffer);

    struct mqtts_client_context* context = container_of(mqtt_context, struct mqtts_client_context, mqtt_context);

    if (deserialized_info->deserializationResult != MQTTSuccess)
    {
        MT_LOG_ERROR(s_kTag, "packet deserialize failed: %d", (int)deserialized_info->deserializationResult);
        return true;
    }

    switch (packet_info->type)
    {
    case MQTT_PACKET_TYPE_PUBLISH:
    {
        const MQTTPublishInfo_t* publish_info = deserialized_info->pPublishInfo;
        if (publish_info == NULL)
            break;

        /* QoS1/2 的 PUBACK/PUBREC 由 coreMQTT 自动回复, 这里只转发业务数据 */
        if (context->message_callback != NULL)
        {
            uint16_t payload_length = (publish_info->payloadLength > UINT16_MAX) ? UINT16_MAX : (uint16_t)publish_info->payloadLength;
            context->message_callback(publish_info->pTopicName, publish_info->topicNameLength, (const uint8_t*)publish_info->pPayload,
                                      payload_length);
        }
        break;
    }

    case MQTT_PACKET_TYPE_SUBACK:
    case MQTT_PACKET_TYPE_UNSUBACK:
    case MQTT_PACKET_TYPE_PUBACK:
    case MQTT_PACKET_TYPE_PUBREC:
    case MQTT_PACKET_TYPE_PUBREL:
    case MQTT_PACKET_TYPE_PUBCOMP:
    {
        /* 请求完成确认: 仅记录包标识, 不跟踪在途请求 */
        MT_LOG_INFO(s_kTag, "ack type 0x%02X, pid %u", (unsigned)packet_info->type, (unsigned)deserialized_info->packetIdentifier);
        break;
    }

    case MQTT_PACKET_TYPE_DISCONNECT:
    {
        MT_LOG_WARN(s_kTag, "broker sent DISCONNECT");
        context->is_mqtt_connected = false;
        break;
    }

    case MQTT_PACKET_TYPE_PINGRESP:
    {
        break;
    }

    default:
    {
        MT_LOG_WARN(s_kTag, "unhandled packet type 0x%02X", (unsigned)packet_info->type);
        break;
    }
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* 连接管理                                                                   */
/* -------------------------------------------------------------------------- */
int mqtts_client_init(struct mqtts_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    memset(context, 0, sizeof(*context));

    /* 传输层绑定: altcp_tls 适配函数 + 加密通道 */
    context->network_context.tls_client = &context->tls_client;
    context->transport_interface.recv = mqtts_transport_recv;
    context->transport_interface.send = mqtts_transport_send;
    context->transport_interface.pNetworkContext = (NetworkContext_t*)(void*)&context->network_context;

    /* coreMQTT 收发共用一块静态网络缓冲 */
    context->network_buffer.pBuffer = context->network_buffer_memory;
    context->network_buffer.size = sizeof(context->network_buffer_memory);

    MQTTStatus_t status =
        MQTT_Init(&context->mqtt_context, &context->transport_interface, mqtts_get_time_ms, mqtts_event_callback, &context->network_buffer);
    if (status != MQTTSuccess)
    {
        MT_LOG_ERROR(s_kTag, "MQTT_Init failed: %d", (int)status);
        return NET_ERR_INVAL;
    }
    return NET_OK;
}

int mqtts_client_set_message_callback(struct mqtts_client_context* context, mqtts_message_callback_t callback)
{
    if (!context)
        return NET_ERR_INVAL;
    context->message_callback = callback;
    return NET_OK;
}

/**
 * @brief 复位连接流程标志并关闭加密通道
 * @param[in,out] context MQTTS 客户端控制块
 */
static void mqtts_reset_connection(struct mqtts_client_context* context)
{
    context->is_mqtt_connected = false;
    context->connect_requested = false;
    context->connack_pending = false;
    MINI_IGNORE_RESULT(tls_client_disconnect(&context->tls_client));
}

int mqtts_client_do_connect(struct mqtts_client_context* context)
{
    if (context == NULL || context->broker_ip == NULL || context->port == 0)
        return NET_ERR_INVAL;

    if (context->connect_requested || context->is_mqtt_connected)
    {
        MT_LOG_WARN(s_kTag, "already connecting/connected");
        return NET_ERR_STATE;
    }

    int err = tls_client_init_and_connect(&context->tls_client, context->broker_ip, context->port, context->ca_cert, context->ca_len);
    if (err != ERR_OK)
    {
        MT_LOG_ERROR(s_kTag, "tls connect start failed: %d", err);
        return NET_ERR_CONN;
    }

    context->connect_requested = true;
    context->connack_pending = false;
    context->is_mqtt_connected = false;
    context->tls_connect_start_ms = mini_time_ms();

    MT_LOG_INFO(s_kTag, "connecting to %s:%u (tls)...", context->broker_ip, context->port);
    return NET_OK;
}

/**
 * @brief 执行 MQTT 握手 (TLS 通道已就绪时调用, 内部阻塞至 CONNACK 或超时)
 * @param[in,out] context MQTTS 客户端控制块
 * @return int NET_OK 握手成功; 其他值失败 (已复位连接)
 */
static int mqtts_handshake(struct mqtts_client_context* context)
{
    MQTTConnectInfo_t connect_info = {0};
    connect_info.cleanSession = true;
    connect_info.keepAliveSeconds = context->keep_alive_seconds;

    const char* client_id = (context->client_id != NULL) ? context->client_id : MQTTS_CLIENT_ID;
    connect_info.pClientIdentifier = client_id;
    connect_info.clientIdentifierLength = (uint16_t)strlen(client_id);

    if (context->username != NULL)
    {
        connect_info.pUserName = context->username;
        connect_info.userNameLength = (uint16_t)strlen(context->username);
    }
    if (context->password != NULL)
    {
        connect_info.pPassword = context->password;
        connect_info.passwordLength = (uint16_t)strlen(context->password);
    }

    /* 遗嘱消息 (可选) */
    MQTTPublishInfo_t will_info = {0};
    bool              has_will = (context->will_options.topic != NULL && context->will_options.message != NULL);
    if (has_will)
    {
        will_info.qos = mqtts_qos_from_u8(context->will_options.qos);
        will_info.retain = context->will_options.retain;
        will_info.pTopicName = context->will_options.topic;
        will_info.topicNameLength = (uint16_t)strlen(context->will_options.topic);
        will_info.pPayload = context->will_options.message;
        will_info.payloadLength = strlen(context->will_options.message);
    }

    context->connack_pending = true;
    bool         session_present = false;
    MQTTStatus_t status =
        MQTT_Connect(&context->mqtt_context, &connect_info, has_will ? &will_info : NULL, MQTTS_CONNACK_TIMEOUT_MS, &session_present, NULL, NULL);

    if (status == MQTTSuccess)
    {
        context->is_mqtt_connected = true;
        MT_LOG_INFO(s_kTag, "CONNACK ok, mqtts connected");
        return NET_OK;
    }

    MT_LOG_ERROR(s_kTag, "MQTT_Connect failed: %d", (int)status);
    mqtts_reset_connection(context);
    return NET_ERR_CONN;
}

int mqtts_client_disconnect(struct mqtts_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    /* MQTT 会话建立时先发 DISCONNECT 报文, 通知 Broker 正常下线 (遗嘱不触发) */
    if (context->is_mqtt_connected)
        MINI_IGNORE_RESULT(MQTT_Disconnect(&context->mqtt_context, NULL, NULL));

    mqtts_reset_connection(context);
    MT_LOG_INFO(s_kTag, "mqtts disconnected");
    return NET_OK;
}

bool is_mqtts_client_connected(const struct mqtts_client_context* context)
{
    return (context != NULL) && context->is_mqtt_connected && context->tls_client.is_connected;
}

/* -------------------------------------------------------------------------- */
/* 发布与订阅                                                                 */
/* -------------------------------------------------------------------------- */
int mqtts_client_publish(struct mqtts_client_context* context, const char* topic, const void* payload, uint16_t length, uint8_t qos, bool retain)
{
    if (!is_mqtts_client_connected(context) || !topic)
    {
        MT_LOG_ERROR(s_kTag, "publish rejected: not connected or topic NULL");
        return NET_ERR_INVAL;
    }

    MQTTPublishInfo_t publish_info = {0};
    publish_info.qos = mqtts_qos_from_u8(qos);
    publish_info.retain = retain;
    publish_info.pTopicName = topic;
    publish_info.topicNameLength = (uint16_t)strlen(topic);
    publish_info.pPayload = payload;
    publish_info.payloadLength = length;

    /* QoS1/2 需要报文标识符 */
    uint16_t packet_id = 0;
    if (publish_info.qos > MQTTQoS0)
        packet_id = MQTT_GetPacketId(&context->mqtt_context);

    MQTTStatus_t status = MQTT_Publish(&context->mqtt_context, &publish_info, packet_id, NULL);
    if (status != MQTTSuccess)
    {
        MT_LOG_ERROR(s_kTag, "MQTT_Publish failed: %d", (int)status);
        return NET_ERR_NOSPC;
    }
    return NET_OK;
}

int mqtts_client_subscribe(struct mqtts_client_context* context, const char* topic, uint8_t qos)
{
    if (!is_mqtts_client_connected(context) || !topic)
    {
        MT_LOG_ERROR(s_kTag, "subscribe rejected: not connected or topic NULL");
        return NET_ERR_INVAL;
    }

    size_t topic_length = strlen(topic);
    if (topic_length == 0 || topic_length > UINT16_MAX)
    {
        MT_LOG_ERROR(s_kTag, "subscribe topic len invalid: %u", (unsigned)topic_length);
        return NET_ERR_INVAL;
    }

    MQTTSubscribeInfo_t subscribe_info = {0};
    subscribe_info.qos = mqtts_qos_from_u8(qos);
    subscribe_info.pTopicFilter = topic;
    subscribe_info.topicFilterLength = (uint16_t)topic_length;

    MQTTStatus_t status = MQTT_Subscribe(&context->mqtt_context, &subscribe_info, 1U, MQTT_GetPacketId(&context->mqtt_context), NULL);
    if (status != MQTTSuccess)
    {
        MT_LOG_ERROR(s_kTag, "MQTT_Subscribe failed: %d", (int)status);
        return NET_ERR_NOSPC;
    }
    return NET_OK;
}

int mqtts_client_unsubscribe(struct mqtts_client_context* context, const char* topic)
{
    if (!is_mqtts_client_connected(context) || !topic)
    {
        MT_LOG_ERROR(s_kTag, "unsubscribe rejected: not connected or topic NULL");
        return NET_ERR_INVAL;
    }

    MQTTSubscribeInfo_t subscribe_info = {0};
    subscribe_info.qos = MQTTQoS0;
    subscribe_info.pTopicFilter = topic;
    subscribe_info.topicFilterLength = (uint16_t)strlen(topic);

    MQTTStatus_t status = MQTT_Unsubscribe(&context->mqtt_context, &subscribe_info, 1U, MQTT_GetPacketId(&context->mqtt_context), NULL);
    if (status != MQTTSuccess)
    {
        MT_LOG_ERROR(s_kTag, "MQTT_Unsubscribe failed: %d", (int)status);
        return NET_ERR_NOSPC;
    }
    return NET_OK;
}

/* -------------------------------------------------------------------------- */
/* 协议栈驱动                                                                 */
/* -------------------------------------------------------------------------- */
int mqtts_client_process(struct mqtts_client_context* context)
{
    if (!context)
        return NET_ERR_INVAL;

    /* 两段式建连: do_connect 发起加密链路, 这里等握手完成后做 MQTT 握手 */
    if (context->connect_requested && !context->connack_pending && !context->is_mqtt_connected)
    {
        if (context->tls_client.is_connected)
        {
            MINI_IGNORE_RESULT(mqtts_handshake(context));
        }
        else if ((mini_time_ms() - context->tls_connect_start_ms) >= MQTTS_TLS_CONNECT_TIMEOUT_MS)
        {
            /* TLS 握手失败 (err 回调已复位) 或超时 */
            MT_LOG_ERROR(s_kTag, "tls handshake failed/timeout");
            mqtts_reset_connection(context);
        }
    }

    /* 会话进行中: 驱动 coreMQTT 收包/心跳/重发, 并泵送加密发送队列 */
    if (context->connack_pending || context->is_mqtt_connected)
    {
        MQTTStatus_t status = MQTT_ProcessLoop(&context->mqtt_context);
        if ((status != MQTTSuccess) && (status != MQTTNeedMoreBytes))
        {
            MT_LOG_ERROR(s_kTag, "ProcessLoop error: %d, reset connection", (int)status);
            mqtts_reset_connection(context);
            return NET_OK;
        }
    }

    MINI_IGNORE_RESULT(tls_client_poll_send(&context->tls_client));

    return NET_OK;
}
