/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file mqtt_broker.c
 * @brief MQTT Broker Implementation (明文, 无 TLS)
 * @author H-000-H
 * @details 基于 tcp_server 会话表的轻量 Broker:
 *          1. 每会话静态累积缓冲累积入站字节, MQTT_ProcessIncomingPacketTypeAndLength
 *             增量解析固定头, 报文完整后按类型分发;
 *          2. CONNECT/SUBSCRIBE/UNSUBSCRIBE 按协议手工解析 (coreMQTT v5 公开 API
 *             无服务端反序列化); PUBLISH 用 MQTT_DeserializePublish 解析;
 *          3. 下行转发统一降为 QoS0 (无重发状态机), 经全局订阅表 +
 *             MQTT_MatchTopic 匹配; CONNACK/SUBACK/UNSUBACK/PUBACK/PINGRESP
 *             按 3.1.1 最小格式组装 (MQTT5 允许省略 0 原因码与空属性, 兼容双版本);
 *          4. keep-alive 超时 (配置倍数) 主动踢线并回收订阅。
 *          约束: QoS2 入站仅按 PUBREC/PUBCOMP 机械应答 (每条只投递一次)。
 */
#include "mqtt_broker.h"

#include "compiler_compat.h"
#include "mini_time.h"
#include "system_log.h"
#include "tcp/tcp_server.h"
#include <string.h>

static const char* const s_kTag = "mqtt_broker";

#define BROKER_MAX_CLIENTS CONFIG_TCP_SERVER_MAX_CLIENTS /**< 会话槽位上限 (同 tcp_server) */
#define BROKER_READ_CHUNK 128                            /**< 单次从会话读取的字节数 */

_Static_assert((MQTT_BROKER_PACKET_BUFFER_SIZE & (MQTT_BROKER_PACKET_BUFFER_SIZE - 1U)) == 0U,
               "CONFIG_MQTT_BROKER_PACKET_BUFFER_SIZE must be power of 2");

/* -------------------------------------------------------------------------- */
/* 静态表                                                                     */
/* -------------------------------------------------------------------------- */
/**
 * @brief 单会话 broker 侧状态 (与 tcp_server 会话槽位一一对应)
 */
struct broker_session
{
    bool     mqtt_connected;                                 /**< CONNECT/CONNACK 是否已完成 */
    uint8_t  protocol_level;                                 /**< 协议级别: 4=MQTT3.1.1, 5=MQTT5.0 */
    uint16_t keep_alive_s;                                   /**< 客户端声明的保活周期 (0=禁用) */
    uint32_t last_rx_ms;                                     /**< 最近一次收到数据的时间戳 (保活检测) */
    size_t   buf_used;                                       /**< 累积缓冲占用长度 */
    uint8_t  accumulate_buf[MQTT_BROKER_PACKET_BUFFER_SIZE]; /**< 入站报文累积缓冲 */
};

/**
 * @brief 订阅表条目 (全局, 所有客户端共享)
 */
struct broker_subscription
{
    bool    is_used;                               /**< 占用标志 */
    int     session_id;                            /**< 所属会话 ID */
    uint8_t qos;                                   /**< 授予的 QoS (0/1) */
    char    filter[MQTT_BROKER_MAX_TOPIC_LEN + 1]; /**< 主题过滤器 ('\0' 结尾副本) */
};

static bool                       s_broker_initialized = false;
static struct broker_session      s_sessions[BROKER_MAX_CLIENTS];
static struct broker_subscription s_subscriptions[MQTT_BROKER_MAX_SUBSCRIPTIONS];
static uint8_t                    s_tx_buf[MQTT_BROKER_PACKET_BUFFER_SIZE]; /**< 应答/转发组装缓冲 (单线程共享) */

/* -------------------------------------------------------------------------- */
/* 基础工具                                                                   */
/* -------------------------------------------------------------------------- */
/**
 * @brief 大端读取 16 位无符号整数
 * @param[in] p 字节指针 (至少 2 字节有效)
 * @return uint16_t 解析结果
 */
static uint16_t broker_read_u16_be(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]); }

/**
 * @brief 编码 MQTT 剩余长度为变长整数
 * @param[in]  value 剩余长度值 (< 268435456)
 * @param[out] out   输出缓冲 (至少 4 字节)
 * @return size_t 编码后字节数 (1~4)
 */
static size_t broker_encode_remaining_length(uint32_t value, uint8_t* out)
{
    size_t n = 0;

    do
    {
        uint8_t byte = (uint8_t)(value & 0x7FU);
        value >>= 7U;
        if (value > 0U)
            byte |= 0x80U;
        out[n] = byte;
        n++;
    } while ((value > 0U) && (n < 4U));

    return n;
}

/**
 * @brief 跳过一段 MQTT 属性区 (变长长度前缀 + 属性本体)
 * @param[in]     data      属性区起始指针
 * @param[in,out] off       当前解析偏移, 成功时前移越过整个属性区
 * @param[in]     len       数据总长度 (边界)
 * @return bool true 跳过成功; false 越界/编码非法
 */
static bool broker_skip_properties(const uint8_t* data, size_t* off, size_t len)
{
    uint32_t prop_len = 0;
    uint32_t multiplier = 1;
    size_t   decoded = 0;
    uint8_t  byte;

    do
    {
        if ((*off + decoded) >= len || decoded >= 4U)
            return false;
        byte = data[*off + decoded];
        prop_len += (uint32_t)(byte & 0x7FU) * multiplier;
        multiplier *= 128U;
        decoded++;
    } while ((byte & 0x80U) != 0U);

    *off += decoded;
    if ((*off + prop_len) > len)
        return false;
    *off += prop_len;
    return true;
}

/**
 * @brief 跳过 UTF-8 编码字符串 (2 字节长度 + 内容)
 * @param[in]     data 报文可变部分
 * @param[in,out] off  当前解析偏移
 * @param[in]     len  数据总长度
 * @return bool true 成功; false 越界
 */
static bool broker_skip_utf8_string(const uint8_t* data, size_t* off, size_t len)
{
    if ((*off + 2U) > len)
        return false;
    uint16_t str_len = broker_read_u16_be(&data[*off]);
    *off += 2U;
    if ((*off + str_len) > len)
        return false;
    *off += str_len;
    return true;
}

/**
 * @brief 向会话发送原始字节 (封装错误日志)
 * @param[in] session_id 会话 ID
 * @param[in] data       待发送数据
 * @param[in] len        数据长度
 * @return int 实际写入字节数, 负值为失败
 */
static int broker_send(int session_id, const void* data, uint16_t len)
{
    int ret = tcp_server_send(session_id, data, len);
    if (ret < 0)
        MT_LOG_WARN(s_kTag, "session [%d] send failed: %d", session_id, ret);
    return ret;
}

/* -------------------------------------------------------------------------- */
/* 订阅表管理                                                                 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 移除某会话的全部订阅 (断连/重置时调用)
 * @param[in] session_id 会话 ID
 */
static void broker_remove_subscriptions(int session_id)
{
    for (int i = 0; i < MQTT_BROKER_MAX_SUBSCRIPTIONS; i++)
        if (s_subscriptions[i].is_used && s_subscriptions[i].session_id == session_id)
            s_subscriptions[i].is_used = false;
}

/**
 * @brief 新增或更新订阅 (同会话同过滤器覆盖)
 * @param[in] session_id 会话 ID
 * @param[in] filter     主题过滤器 (不保证 '\0' 结尾)
 * @param[in] filter_len 过滤器长度
 * @param[in] qos        授予的 QoS
 * @return bool true 成功; false 表满或过滤器过长
 */
static bool broker_add_subscription(int session_id, const char* filter, size_t filter_len, uint8_t qos)
{
    if (filter_len == 0 || filter_len > MQTT_BROKER_MAX_TOPIC_LEN)
        return false;

    struct broker_subscription* free_slot = NULL;
    for (int i = 0; i < MQTT_BROKER_MAX_SUBSCRIPTIONS; i++)
    {
        struct broker_subscription* sub = &s_subscriptions[i];
        if (sub->is_used && sub->session_id == session_id && strlen(sub->filter) == filter_len && memcmp(sub->filter, filter, filter_len) == 0)
        {
            sub->qos = qos; /* 重复订阅: 更新授予 QoS */
            return true;
        }
        if (!sub->is_used && free_slot == NULL)
            free_slot = sub;
    }

    if (free_slot == NULL)
        return false;

    free_slot->is_used = true;
    free_slot->session_id = session_id;
    free_slot->qos = qos;
    memcpy(free_slot->filter, filter, filter_len);
    free_slot->filter[filter_len] = '\0';
    return true;
}

/* -------------------------------------------------------------------------- */
/* 会话状态                                                                   */
/* -------------------------------------------------------------------------- */
/**
 * @brief 复位会话 broker 侧状态并回收其订阅 (不动 TCP 链路)
 * @param[in,out] session 会话状态
 * @param[in]     session_id 会话 ID (用于清理订阅表)
 */
static void broker_session_reset(struct broker_session* session, int session_id)
{
    broker_remove_subscriptions(session_id);
    session->mqtt_connected = false;
    session->protocol_level = 0;
    session->keep_alive_s = 0;
    session->buf_used = 0;
}

/**
 * @brief 协议违规处理: 记录日志, 回收订阅, 断开 TCP
 * @param[in] session_id 会话 ID
 * @param[in] reason     违规原因描述 (日志用)
 */
static void broker_protocol_violation(int session_id, const char* reason)
{
    MT_LOG_ERROR(s_kTag, "session [%d] protocol violation: %s, disconnect", session_id, reason);
    broker_session_reset(&s_sessions[session_id], session_id);
    MINI_IGNORE_RESULT(close_session(session_id));
}

/* -------------------------------------------------------------------------- */
/* 应答组装与发送                                                             */
/* -------------------------------------------------------------------------- */
/**
 * @brief 发送 CONNACK (3.1.1 最小格式, MQTT5 兼容: 原因码 0 可省略属性)
 * @param[in] session_id  会话 ID
 * @param[in] reason_code 原因码 (0=接受)
 */
static void broker_send_connack(int session_id, uint8_t reason_code)
{
    uint8_t connack[4] = {0x20, 0x02, 0x00, reason_code};
    MINI_IGNORE_RESULT(broker_send(session_id, connack, sizeof(connack)));
}

/**
 * @brief 发送 2 字节体确认报文 (PUBACK/PUBREC/PUBCOMP/PINGRESP 同构)
 * @param[in] session_id 会话 ID
 * @param[in] type       报文类型字节 (如 0x40/0x50/0x70/0xD0)
 * @param[in] packet_id  报文标识符 (PINGRESP 无, 传 0 时体长为 0)
 */
static void broker_send_simple_ack(int session_id, uint8_t type, uint16_t packet_id)
{
    if (type == MQTT_PACKET_TYPE_PINGREQ) /* 复用入口回 PINGRESP: 0xD0 0x00 */
    {
        uint8_t pingresp[2] = {0xD0, 0x00};
        MINI_IGNORE_RESULT(broker_send(session_id, pingresp, sizeof(pingresp)));
        return;
    }

    uint8_t ack[4] = {type, 0x02, (uint8_t)(packet_id >> 8U), (uint8_t)(packet_id & 0xFFU)};
    MINI_IGNORE_RESULT(broker_send(session_id, ack, sizeof(ack)));
}

/**
 * @brief 发送 SUBACK (报文标识符 + 每过滤器授予码, 3.1.1 与 5.0 同构)
 * @param[in] session_id 会话 ID
 * @param[in] packet_id  对应 SUBSCRIBE 的报文标识符
 * @param[in] codes      授予码数组 (失败项为 0x80)
 * @param[in] count      过滤器个数
 */
static void broker_send_suback(int session_id, uint16_t packet_id, const uint8_t* codes, uint8_t count)
{
    uint32_t remaining = 2U + count;
    size_t   idx = 0;

    s_tx_buf[idx++] = 0x90;
    idx += broker_encode_remaining_length(remaining, &s_tx_buf[idx]);
    s_tx_buf[idx++] = (uint8_t)(packet_id >> 8U);
    s_tx_buf[idx++] = (uint8_t)(packet_id & 0xFFU);
    memcpy(&s_tx_buf[idx], codes, count);
    idx += count;

    MINI_IGNORE_RESULT(broker_send(session_id, s_tx_buf, (uint16_t)idx));
}

/**
 * @brief 发送 UNSUBACK (3.1.1: 仅报文标识符; 5.0: 追加每过滤器原因码 0x00)
 * @param[in] session_id    会话 ID
 * @param[in] protocol_level 会话协议级别 (4/5)
 * @param[in] packet_id     对应 UNSUBSCRIBE 的报文标识符
 * @param[in] count         过滤器个数
 */
static void broker_send_unsuback(int session_id, uint8_t protocol_level, uint16_t packet_id, uint8_t count)
{
    uint32_t remaining = (protocol_level >= 5U) ? (2U + count) : 2U;
    size_t   idx = 0;

    s_tx_buf[idx++] = 0xB0;
    idx += broker_encode_remaining_length(remaining, &s_tx_buf[idx]);
    s_tx_buf[idx++] = (uint8_t)(packet_id >> 8U);
    s_tx_buf[idx++] = (uint8_t)(packet_id & 0xFFU);
    if (protocol_level >= 5U)
    {
        memset(&s_tx_buf[idx], 0x00, count); /* 全部成功 */
        idx += count;
    }

    MINI_IGNORE_RESULT(broker_send(session_id, s_tx_buf, (uint16_t)idx));
}

/**
 * @brief 转发一条 QoS0 PUBLISH 到目标会话 (经 MQTT_SerializePublish 组装)
 * @param[in] session_id 目标会话 ID
 * @param[in] topic      主题 (来自发送方缓冲, 非 '\0' 结尾)
 * @param[in] topic_len  主题长度
 * @param[in] payload    载荷
 * @param[in] payload_len 载荷长度
 */
static void broker_forward_publish(int session_id, const char* topic, uint16_t topic_len, const void* payload, size_t payload_len)
{
    MQTTPublishInfo_t fwd = {0};
    fwd.qos = MQTTQoS0; /* 下行统一 QoS0: 无重发状态机, 简单可靠 */
    fwd.retain = false;
    fwd.pTopicName = topic;
    fwd.topicNameLength = topic_len;
    fwd.pPayload = payload;
    fwd.payloadLength = payload_len;

    uint32_t     remaining_length = 0;
    uint32_t     packet_size = 0;
    MQTTStatus_t status = MQTT_GetPublishPacketSize(&fwd, NULL, &remaining_length, &packet_size, sizeof(s_tx_buf));
    if (status != MQTTSuccess)
    {
        MT_LOG_WARN(s_kTag, "forward too large for tx buf: %u", (unsigned)packet_size);
        return;
    }

    MQTTFixedBuffer_t fixed_buffer = {s_tx_buf, sizeof(s_tx_buf)};
    status = MQTT_SerializePublish(&fwd, NULL, 0U, remaining_length, &fixed_buffer);
    if (status != MQTTSuccess)
    {
        MT_LOG_ERROR(s_kTag, "SerializePublish failed: %d", (int)status);
        return;
    }

    MINI_IGNORE_RESULT(broker_send(session_id, s_tx_buf, (uint16_t)packet_size));
}

/* -------------------------------------------------------------------------- */
/* 报文处理                                                                   */
/* -------------------------------------------------------------------------- */
/**
 * @brief 处理 CONNECT: 解析协议级别/保活周期, 回 CONNACK 并置会话就绪
 * @param[in] session_id 会话 ID
 * @param[in] data       报文可变部分 (固定头之后)
 * @param[in] len        可变部分长度
 */
static void broker_handle_connect(int session_id, const uint8_t* data, size_t len)
{
    struct broker_session* session = &s_sessions[session_id];
    size_t                 off = 0;

    /* 重复 CONNECT: 先复位旧状态 */
    if (session->mqtt_connected)
        broker_session_reset(session, session_id);

    /* 协议名 (长度 + "MQTT"/"MQIsdp"), 本层不校验内容 */
    if (!broker_skip_utf8_string(data, &off, len))
    {
        broker_protocol_violation(session_id, "CONNECT proto name");
        return;
    }

    /* 协议级别 + 连接标志 + 保活周期 */
    if ((off + 4U) > len)
    {
        broker_protocol_violation(session_id, "CONNECT header short");
        return;
    }
    uint8_t  protocol_level = data[off];
    uint8_t  connect_flags = data[off + 1U];
    uint16_t keep_alive = broker_read_u16_be(&data[off + 2U]);
    off += 4U;

    if (protocol_level != 4U && protocol_level != 5U)
    {
        MT_LOG_WARN(s_kTag, "session [%d] unsupported protocol level %u", session_id, protocol_level);
        broker_send_connack(session_id, 0x01); /* 不支持的协议版本 */
        MINI_IGNORE_RESULT(close_session(session_id));
        return;
    }

    /* MQTT5 CONNECT 属性 (跳过) */
    if (protocol_level >= 5U && !broker_skip_properties(data, &off, len))
    {
        broker_protocol_violation(session_id, "CONNECT properties");
        return;
    }

    /* 客户端标识符 */
    if (!broker_skip_utf8_string(data, &off, len))
    {
        broker_protocol_violation(session_id, "CONNECT client id");
        return;
    }

    /* 遗嘱: (5.0 遗嘱属性) + 遗嘱主题 + 遗嘱载荷 */
    if ((connect_flags & 0x04U) != 0U)
    {
        if (protocol_level >= 5U && !broker_skip_properties(data, &off, len))
        {
            broker_protocol_violation(session_id, "will properties");
            return;
        }
        if (!broker_skip_utf8_string(data, &off, len) || !broker_skip_utf8_string(data, &off, len))
        {
            broker_protocol_violation(session_id, "will topic/payload");
            return;
        }
    }

    /* 用户名 / 密码 (仅跳过, 本层不做鉴权) */
    if ((connect_flags & 0x80U) != 0U && !broker_skip_utf8_string(data, &off, len))
    {
        broker_protocol_violation(session_id, "CONNECT username");
        return;
    }
    if ((connect_flags & 0x40U) != 0U && !broker_skip_utf8_string(data, &off, len))
    {
        broker_protocol_violation(session_id, "CONNECT password");
        return;
    }

    session->mqtt_connected = true;
    session->protocol_level = protocol_level;
    session->keep_alive_s = keep_alive;
    MT_LOG_INFO(s_kTag, "session [%d] CONNECT ok (level %u, keepalive %us)", session_id, (unsigned)protocol_level, (unsigned)keep_alive);

    broker_send_connack(session_id, 0x00); /* 接受 */
}

/**
 * @brief 处理 SUBSCRIBE: 逐过滤器入订阅表并回 SUBACK
 * @param[in] session_id 会话 ID
 * @param[in] data       报文可变部分
 * @param[in] len        可变部分长度
 */
static void broker_handle_subscribe(int session_id, const uint8_t* data, size_t len)
{
    struct broker_session* session = &s_sessions[session_id];

    if (!session->mqtt_connected || len < 2U)
    {
        broker_protocol_violation(session_id, "SUBSCRIBE before CONNECT");
        return;
    }

    uint16_t packet_id = broker_read_u16_be(data);
    size_t   off = 2U;

    if (session->protocol_level >= 5U && !broker_skip_properties(data, &off, len))
    {
        broker_protocol_violation(session_id, "SUBSCRIBE properties");
        return;
    }

    uint8_t codes[(MQTT_BROKER_PACKET_BUFFER_SIZE > 256U) ? 64U : 32U];
    uint8_t count = 0;

    while (off < len)
    {
        if ((off + 2U) > len)
        {
            broker_protocol_violation(session_id, "SUBSCRIBE filter len");
            return;
        }
        uint16_t filter_len = broker_read_u16_be(&data[off]);
        off += 2U;
        if (filter_len == 0 || (off + filter_len + 1U) > len)
        {
            broker_protocol_violation(session_id, "SUBSCRIBE filter body");
            return;
        }
        const char* filter = (const char*)&data[off];
        off += filter_len;

        /* 选项字节: 低 2 位为请求 QoS (3.1.1 保留位也在此字节) */
        uint8_t options = data[off];
        off += 1U;
        uint8_t req_qos = options & 0x03U;

        if (count >= (uint8_t)sizeof(codes))
        {
            broker_protocol_violation(session_id, "SUBSCRIBE too many filters");
            return;
        }

        if (req_qos == 3U)
        {
            broker_protocol_violation(session_id, "SUBSCRIBE invalid qos");
            return;
        }

        uint8_t granted = (req_qos > 1U) ? 1U : req_qos; /* 本层仅支持 QoS 0/1 */
        if (!broker_add_subscription(session_id, filter, filter_len, granted))
        {
            granted = 0x80U; /* 失败 (表满/过滤器过长) */
            MT_LOG_WARN(s_kTag, "session [%d] subscription table full or filter too long", session_id);
        }
        codes[count++] = granted;
    }

    if (count == 0)
    {
        broker_protocol_violation(session_id, "SUBSCRIBE empty");
        return;
    }

    MT_LOG_INFO(s_kTag, "session [%d] subscribed %u filter(s), pid %u", session_id, (unsigned)count, (unsigned)packet_id);
    broker_send_suback(session_id, packet_id, codes, count);
}

/**
 * @brief 处理 UNSUBSCRIBE: 从订阅表移除并回 UNSUBACK
 * @param[in] session_id 会话 ID
 * @param[in] data       报文可变部分
 * @param[in] len        可变部分长度
 */
static void broker_handle_unsubscribe(int session_id, const uint8_t* data, size_t len)
{
    struct broker_session* session = &s_sessions[session_id];

    if (!session->mqtt_connected || len < 2U)
    {
        broker_protocol_violation(session_id, "UNSUBSCRIBE before CONNECT");
        return;
    }

    uint16_t packet_id = broker_read_u16_be(data);
    size_t   off = 2U;

    if (session->protocol_level >= 5U && !broker_skip_properties(data, &off, len))
    {
        broker_protocol_violation(session_id, "UNSUBSCRIBE properties");
        return;
    }

    uint8_t count = 0;
    while (off < len)
    {
        size_t filter_start = off;
        if (!broker_skip_utf8_string(data, &off, len))
        {
            broker_protocol_violation(session_id, "UNSUBSCRIBE filter");
            return;
        }
        size_t filter_len = off - filter_start - 2U;
        count++;

        /* 逐条从订阅表删除 (同会话同过滤器) */
        for (int i = 0; i < MQTT_BROKER_MAX_SUBSCRIPTIONS; i++)
        {
            struct broker_subscription* sub = &s_subscriptions[i];
            if (sub->is_used && sub->session_id == session_id && strlen(sub->filter) == filter_len &&
                memcmp(sub->filter, &data[filter_start + 2U], filter_len) == 0)
            {
                sub->is_used = false;
            }
        }
    }

    MT_LOG_INFO(s_kTag, "session [%d] unsubscribed %u filter(s), pid %u", session_id, (unsigned)count, (unsigned)packet_id);
    broker_send_unsuback(session_id, session->protocol_level, packet_id, count);
}

/**
 * @brief 处理入站 PUBLISH: 解析后按订阅表匹配转发, 并按 QoS 应答
 * @param[in] session_id 会话 ID
 * @param[in] packet     已解析固定头的报文描述 (pRemainingData 指向可变部分)
 */
static void broker_handle_publish(int session_id, MQTTPacketInfo_t* packet)
{
    struct broker_session* session = &s_sessions[session_id];

    if (!session->mqtt_connected)
    {
        broker_protocol_violation(session_id, "PUBLISH before CONNECT");
        return;
    }

    uint16_t          packet_id = 0;
    MQTTPublishInfo_t publish_info = {0};
    /* 属性不解析 (NULL 安全), 不支持主题别名 (topicAliasMax=0) */
    MQTTStatus_t status = MQTT_DeserializePublish(packet, &packet_id, &publish_info, NULL, MQTT_BROKER_PACKET_BUFFER_SIZE, 0U);
    if (status != MQTTSuccess)
    {
        broker_protocol_violation(session_id, "PUBLISH deserialize");
        return;
    }

    /* 订阅匹配转发 (下行统一 QoS0, 不区分订阅授予等级) */
    for (int i = 0; i < MQTT_BROKER_MAX_SUBSCRIPTIONS; i++)
    {
        struct broker_subscription* sub = &s_subscriptions[i];
        if (!sub->is_used || !is_session_connected(sub->session_id))
            continue;

        bool is_match = false;
        if (MQTT_MatchTopic(publish_info.pTopicName, publish_info.topicNameLength, sub->filter, strlen(sub->filter), &is_match) == MQTTSuccess &&
            is_match)
        {
            broker_forward_publish(sub->session_id, publish_info.pTopicName, publish_info.topicNameLength, publish_info.pPayload,
                                   publish_info.payloadLength);
        }
    }

    /* QoS 应答: 1 -> PUBACK; 2 -> PUBREC (机械应答, 每条仅投递一次) */
    if (publish_info.qos == MQTTQoS1)
        broker_send_simple_ack(session_id, MQTT_PACKET_TYPE_PUBACK, packet_id);
    else if (publish_info.qos == MQTTQoS2)
        broker_send_simple_ack(session_id, MQTT_PACKET_TYPE_PUBREC, packet_id);
}

/**
 * @brief 分发一条完整入站报文
 * @param[in] session_id 会话 ID
 * @param[in] packet     已解析固定头的报文描述
 */
static void broker_dispatch_packet(int session_id, MQTTPacketInfo_t* packet)
{
    uint8_t* data = packet->pRemainingData;
    size_t   len = packet->remainingLength;

    switch (packet->type)
    {
    case MQTT_PACKET_TYPE_CONNECT:
    {
        broker_handle_connect(session_id, data, len);
        break;
    }

    case MQTT_PACKET_TYPE_PUBLISH:
    {
        broker_handle_publish(session_id, packet);
        break;
    }

    case MQTT_PACKET_TYPE_SUBSCRIBE:
    {
        broker_handle_subscribe(session_id, data, len);
        break;
    }

    case MQTT_PACKET_TYPE_UNSUBSCRIBE:
    {
        broker_handle_unsubscribe(session_id, data, len);
        break;
    }

    case MQTT_PACKET_TYPE_PUBREL:
    {
        /* QoS2 释放: 回 PUBCOMP 结束流程 (载荷已在 PUBLISH 时投递) */
        if (len >= 2U)
            broker_send_simple_ack(session_id, MQTT_PACKET_TYPE_PUBCOMP, broker_read_u16_be(data));
        break;
    }

    case MQTT_PACKET_TYPE_PINGREQ:
    {
        broker_send_simple_ack(session_id, MQTT_PACKET_TYPE_PINGREQ, 0U);
        break;
    }

    case MQTT_PACKET_TYPE_DISCONNECT:
    {
        MT_LOG_INFO(s_kTag, "session [%d] DISCONNECT", session_id);
        broker_session_reset(&s_sessions[session_id], session_id);
        break;
    }

    case MQTT_PACKET_TYPE_PUBACK:
    case MQTT_PACKET_TYPE_PUBCOMP:
    {
        /* 下行仅 QoS0, 不应收到; 忽略 */
        break;
    }

    default:
    {
        broker_protocol_violation(session_id, "unexpected packet type");
        break;
    }
    }
}

/* -------------------------------------------------------------------------- */
/* 处理引擎                                                                   */
/* -------------------------------------------------------------------------- */
/**
 * @brief 单个会话的收包/解析/保活处理
 * @param[in] session_id 会话 ID
 */
static void broker_process_session(int session_id)
{
    struct broker_session* session = &s_sessions[session_id];

    if (!is_session_connected(session_id))
    {
        /* 链路已断 (对端关闭/异常): 静默回收状态与订阅 */
        if (session->mqtt_connected || session->buf_used > 0U)
            broker_session_reset(session, session_id);
        return;
    }

    /* 1. 收包: 尽量读空会话接收 FIFO (缓冲满则留待下次) */
    bool got_data = false;
    while (session->buf_used < sizeof(session->accumulate_buf))
    {
        size_t space = sizeof(session->accumulate_buf) - session->buf_used;
        size_t chunk = (space > BROKER_READ_CHUNK) ? BROKER_READ_CHUNK : space;
        int    recv_len = 0;

        if (get_tcp_data_by_session(session_id, (char*)&session->accumulate_buf[session->buf_used], (int)chunk, &recv_len) != ERR_OK || recv_len <= 0)
            break;
        session->buf_used += (size_t)recv_len;
        got_data = true;
    }

    if (got_data)
        session->last_rx_ms = mini_time_ms();

    /* 2. 解析: 循环提取累积缓冲中的完整报文 */
    while (session->buf_used > 0U)
    {
        size_t           index = session->buf_used; /* 已缓冲字节数 (库按 *pIndex 判定可用量) */
        MQTTPacketInfo_t packet = {0};
        MQTTStatus_t     status = MQTT_ProcessIncomingPacketTypeAndLength(session->accumulate_buf, &index, &packet);

        if (status == MQTTNeedMoreBytes || status == MQTTNoDataAvailable)
            break; /* 固定头未收全, 等更多数据 */
        if (status != MQTTSuccess)
        {
            broker_protocol_violation(session_id, "bad fixed header");
            return;
        }

        size_t total = packet.headerLength + packet.remainingLength;
        if (total > sizeof(session->accumulate_buf))
        {
            broker_protocol_violation(session_id, "packet larger than buffer");
            return;
        }
        if (session->buf_used < total)
            break; /* 报文主体未收全, 等更多数据 */

        packet.pRemainingData = &session->accumulate_buf[packet.headerLength];
        broker_dispatch_packet(session_id, &packet);

        /* 消费掉本条报文 (会话可能已被 violation 复位, 复位后终止) */
        if (!is_session_connected(session_id))
            return;
        size_t remain = session->buf_used - total;
        if (remain > 0U)
            memmove(session->accumulate_buf, &session->accumulate_buf[total], remain);
        session->buf_used = remain;
    }

    /* 3. 保活检测: 超时视为失联, 踢线并回收订阅 */
    if (session->mqtt_connected && session->keep_alive_s > 0U)
    {
        uint32_t timeout_ms = (uint32_t)session->keep_alive_s * 1000U * MQTT_BROKER_KEEPALIVE_FACTOR;
        if ((mini_time_ms() - session->last_rx_ms) >= timeout_ms)
        {
            MT_LOG_WARN(s_kTag, "session [%d] keep-alive timeout, disconnect", session_id);
            broker_session_reset(session, session_id);
            MINI_IGNORE_RESULT(close_session(session_id));
        }
    }
}

int mqtt_broker_init(uint16_t port)
{
    if (port == 0U)
        return NET_ERR_INVAL;

    memset(s_sessions, 0, sizeof(s_sessions));
    memset(s_subscriptions, 0, sizeof(s_subscriptions));

    int err = tcp_server_init((int)port);
    if (err != ERR_OK)
    {
        MT_LOG_ERROR(s_kTag, "tcp_server_init failed: %d", err);
        return NET_ERR_CONN;
    }

    s_broker_initialized = true;
    MT_LOG_INFO(s_kTag, "broker listening on port %u (max clients %d, subscriptions %d)", (unsigned)port, BROKER_MAX_CLIENTS,
             MQTT_BROKER_MAX_SUBSCRIPTIONS);
    return NET_OK;
}

int mqtt_broker_process(void)
{
    if (!s_broker_initialized)
        return NET_ERR_INVAL;

    for (int session_id = 0; session_id < BROKER_MAX_CLIENTS; session_id++)
        broker_process_session(session_id);

    return NET_OK;
}
