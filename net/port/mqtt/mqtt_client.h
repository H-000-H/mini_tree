/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file mqtt_client.h
 * @brief MQTT Client Header File
 * @author H-000-H
 */
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include "lwip/apps/mqtt.h"
#include "lwip/err.h"
#include "buffer.h"
#ifdef CONFIG_MQTT_MAX_TOPIC_LEN
#define MQTT_MAX_TOPIC_LEN CONFIG_MQTT_MAX_TOPIC_LEN
#else
#define MQTT_MAX_TOPIC_LEN 64
#endif

#ifdef CONFIG_MQTT_MAX_SUBSCRIPTIONS
#define MQTT_MAX_SUBSCRIPTIONS CONFIG_MQTT_MAX_SUBSCRIPTIONS
#else
#define MQTT_MAX_SUBSCRIPTIONS 8
#endif

#ifdef CONFIG_MQTT_TX_BUFFER_SIZE
#define MQTT_TX_BUFFER_SIZE CONFIG_MQTT_TX_BUFFER_SIZE
#else
#define MQTT_TX_BUFFER_SIZE 1024
#endif

#ifdef CONFIG_MQTT_RX_BUFFER_SIZE
#define MQTT_RX_BUFFER_SIZE CONFIG_MQTT_RX_BUFFER_SIZE
#else
#define MQTT_RX_BUFFER_SIZE 1024
#endif

#ifdef CONFIG_MQTT_BYTE_SIZE
#define MQTT_BYTE_SIZE CONFIG_MQTT_MQTT_BYTE_SIZE
#else
#define MQTT_BYTE_SIZE 1
#endif
/**
 * @brief MQTT 消息回调函数类型
 * @param[in] topic   Topic 过滤串
 * @param[in] payload 消息载荷
 * @param[in] len     消息长度
 * @return int    处理结果
 */
typedef int (*mqtt_msg_callback_t)(const char* topic, const uint8_t* payload, uint16_t len);

/**
 * @brief MQTT 订阅条目
 */
struct mqtt_subscription_entry
{
    char topic[MQTT_MAX_TOPIC_LEN];      /**< 订阅的 Topic 过滤串 */
    uint8_t qos;                         /**< 服务质量 (0, 1, 2) */
    mqtt_msg_callback_t on_message;      /**< 该 Topic 专属的数据处理回调 */
};

/**
 * @brief MQTT 遗嘱消息选项
 */
struct mqtt_will_options
{
    const char* topic; /**< 遗嘱消息的 Topic */
    const char* message; /**< 遗嘱消息的 Payload */
    uint8_t qos; /**< 遗嘱消息的 QoS 等级 */
    bool retain; /**< 遗嘱消息是否保留 */
};

/**
 * @brief MQTT 客户端控制块
 */
struct mqtt_client_context
{
    /**< 基础网络层上下文 */
    const char* broker_ip;               /**< Broker IP 地址或域名 */
    uint16_t port;                       /**< 端口 (标准非加密为 1883, TLS 为 8883) */
    struct tcp_pcb* pcb;                 /**< 底层 lwIP TCP 控制块 */
    volatile bool is_tcp_connected;      /**< TCP 层是否建立连接 */
    volatile bool is_mqtt_connected;     /**< MQTT 握手(CONNACK)是否完成 */

    /**< MQTT 协议认证与鉴权配置 */
    const char* client_id;               /**< 客户端唯一标识符 (必填) */
    const char* username;                /**< 用户名 (可选，无则为 NULL) */
    const char* password;                /**< 密码 (可选，无则为 NULL) */
    uint16_t keep_alive_sec;             /**< 心跳周期 (秒) */

    /**< 运行时协议状态与计时器 */
    uint16_t packet_id_counter;          /**< 报文标识符自增计数器 (QoS > 0 时使用) */
    uint32_t last_ping_timestamp;        /**< 上次发送 PINGREQ 的时间戳 (滴答定时) */
    uint32_t last_rx_timestamp;          /**< 上次接收报文时间 (用于保活检测) */

    /**< 订阅路由表 (用于分发下行数据) */
    struct mqtt_subscription_entry sub_table[MQTT_MAX_SUBSCRIPTIONS];
    uint8_t sub_count;                   /**< 当前已订阅数量 */
    mqtt_msg_callback_t default_cb;      /**< 未命中主题时的兜底回调 */

    /**< 报文环形缓冲与物理 Buffer (处理分包/粘包) */
    struct fifo_uni_spsc rx_fifo;        /**< 接收 FIFO */
    struct fifo_uni_spsc tx_fifo;        /**< 发送 FIFO */
    uint8_t tx_buffer[MQTT_TX_BUFFER_SIZE];
    uint8_t rx_buffer[MQTT_RX_BUFFER_SIZE];
};

/**
 * @brief 初始化 MQTT 客户端上下文及缓冲区
 * @param[in] ctx MQTT 客户端控制块
 * @return int 初始化结果
 */
int mqtt_client_init(struct mqtt_client_context* ctx);

/**
 * @brief 发起 MQTT 连接（封装 lwIP mqtt_client_connect）
 * @param[in] ctx MQTT 客户端控制块
 * @return int 连接结果
 */
int mqtt_client_do_connect(struct mqtt_client_context* ctx);

/**
 * @brief 断开 MQTT 连接（封装 lwIP mqtt_client_disconnect）
 * @param[in] ctx MQTT 客户端控制块
 * @return int 断开结果
 */
int mqtt_client_disconnect(struct mqtt_client_context* ctx);

/**
 * @brief 检查 MQTT 客户端是否已连接
 * @param[in] ctx MQTT 客户端控制块
 * @return bool 连接状态
 */
bool mqtt_client_is_do_connected(const struct mqtt_client_context* ctx);

/**
 * @brief 发布 MQTT 消息（封装 lwIP mqtt_client_publish）
 * @param[in] ctx MQTT 客户端控制块
 * @param[in] topic Topic 过滤串
 * @param[in] payload 消息载荷
 * @param[in] len 消息长度
 * @param[in] qos QoS 等级
 * @param[in] retain 是否保留消息
 * @return int 发布结果
 */
int mqtt_client_publish(struct mqtt_client_context* ctx,const char* topic,const void* payload,uint16_t len,uint8_t qos,bool retain);

/**
 * @brief 订阅 MQTT 主题（封装 lwIP mqtt_client_subscribe）
 * @param[in] ctx MQTT 客户端控制块
 * @param[in] topic Topic 过滤串
 * @param[in] qos QoS 等级
 * @param[in] cb 数据处理回调函数
 * @return int 订阅结果
 */
int mqtt_client_subscribe(struct mqtt_client_context* ctx,const char* topic,uint8_t qos,mqtt_msg_callback_t cb);

/**
 * @brief 取消订阅 MQTT 主题（封装 lwIP mqtt_client_unsubscribe）
 * @param[in] ctx MQTT 客户端控制块
 * @param[in] topic Topic 过滤串
 * @return int 取消订阅结果
 */
int mqtt_client_unsubscribe(struct mqtt_client_context* ctx, const char* topic);

/**
 * @brief 协议栈处理引擎，需在主循环或专用任务中周期性调用
 * @param[in] ctx MQTT 客户端控制块
 * @param[in] current_tick_ms 当前时间戳 (毫秒)
 * @note 负责解析 rx_fifo 中的报文、分发回调、处理重发及发送 PINGREQ 心跳保活
 * @return int 处理结果
 */
int mqtt_client_process(struct mqtt_client_context* ctx, uint32_t current_tick_ms);
#ifdef __cplusplus
}
#endif
#endif
