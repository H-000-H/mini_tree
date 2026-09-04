/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file mqtt_client.h
 * @brief MQTT Client Header File
 * @author H-000-H
 * @note 本文件为 coreMQTT 的薄包装层: 报文编解码/状态机/心跳/重发/订阅确认
 *       全部由 coreMQTT 负责, 传输走 transport_glue (tcp_client FIFO 通道)。
 *       本层不维护订阅表: 下行 PUBLISH 原样 (主题指针 + 长度) 交给唯一消息回调,
 *       业务侧自行按主题分流。
 *       驱动模型: 应用周期调用 mqtt_client_process() (内部即 MQTT_ProcessLoop)。
 */
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "core_mqtt.h"
#include "net_error.h"
#include "transport_glue/transport_glue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_MQTT_NET_BUFFER_SIZE
#define MQTT_NET_BUFFER_SIZE CONFIG_MQTT_NET_BUFFER_SIZE
#else
#define MQTT_NET_BUFFER_SIZE 2048
#endif

#ifdef CONFIG_MQTT_CLIENT_ID
#define MQTT_CLIENT_ID CONFIG_MQTT_CLIENT_ID
#else
#define MQTT_CLIENT_ID "mini_node"
#endif

/**
 * @brief MQTT 下行消息回调函数类型
 * @param[in] topic          主题字符串 (不保证 '\0' 结尾, 回调返回后不可再引用)
 * @param[in] topic_length   主题长度
 * @param[in] payload        消息载荷 (回调返回后不可再引用)
 * @param[in] payload_length 消息长度
 * @return int 处理结果
 */
typedef int (*mqtt_message_callback_t)(const char* topic, uint16_t topic_length, const uint8_t* payload, uint16_t payload_length);

/**
 * @brief MQTT 遗嘱消息选项
 */
struct mqtt_will_options
{
    const char* topic;   /**< 遗嘱消息的 Topic */
    const char* message; /**< 遗嘱消息的 Payload */
    uint8_t     qos;     /**< 遗嘱消息的 QoS 等级 */
    bool        retain;  /**< 遗嘱消息是否保留 */
};

/**
 * @brief MQTT 客户端控制块
 */
struct mqtt_client_context
{
    /**< coreMQTT 与传输层 (静态嵌入, 零堆) */
    MQTTContext_t             mqtt_context;                                /**< coreMQTT 上下文 */
    TransportInterface_t      transport_interface;                         /**< 传输接口 (指向下方 network_context 与胶水函数) */
    struct NetworkContext     network_context;                             /**< 胶水层网络上下文 */
    struct tcp_client_context tcp_client;                                  /**< 底层 TCP 通道 (fifo 收发) */
    MQTTFixedBuffer_t         network_buffer;                              /**< coreMQTT 网络缓冲描述 */
    uint8_t                   network_buffer_memory[MQTT_NET_BUFFER_SIZE]; /**< 收发共用静态缓冲, 须 >=
                                                                              单条最大报文 */

    /**< 连接配置 */
    const char*              broker_ip;          /**< Broker IP 地址字符串 (点分十进制) */
    uint16_t                 port;               /**< 端口 (标准非加密为 1883) */
    const char*              client_id;          /**< 客户端唯一标识符 (NULL 时用 MQTT_CLIENT_ID) */
    const char*              username;           /**< 用户名 (可选, 无则为 NULL) */
    const char*              password;           /**< 密码 (可选, 无则为 NULL) */
    uint16_t                 keep_alive_seconds; /**< 心跳周期 (秒), 0 表示禁用保活 */
    struct mqtt_will_options will_options;       /**< 遗嘱消息选项 */

    /**< 运行时连接状态 */
    volatile bool is_mqtt_connected;    /**< MQTT 握手(CONNACK)是否完成 */
    bool          connect_requested;    /**< do_connect 已发起, 等待建连/握手流程走完 */
    bool          connack_pending;      /**< CONNECT 已发出, 等待 CONNACK (防重复握手) */
    uint32_t      tcp_connect_start_ms; /**< TCP 建连发起时间戳 (超时兜底) */

    /**< 下行消息回调 (订阅命中后的 PUBLISH 全部经此分发, 可为 NULL) */
    mqtt_message_callback_t message_callback;
};

/**
 * @brief 初始化 MQTT 客户端上下文 (初始化 coreMQTT 与传输层绑定)
 * @param[in] context MQTT 客户端控制块
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法/库初始化失败
 */
int mqtt_client_init(struct mqtt_client_context* context);

/**
 * @brief 设置下行消息回调
 * @param[in] context  MQTT 客户端控制块
 * @param[in] callback 消息回调 (NULL 表示不接收下行消息)
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int mqtt_client_set_message_callback(struct mqtt_client_context* context, mqtt_message_callback_t callback);

/**
 * @brief 发起 MQTT 连接 (异步: TCP 握手在回调上下文完成, MQTT 握手由 process 驱动)
 * @param[in] context MQTT 客户端控制块
 * @return int NET_OK 已发起; NET_ERR_INVAL 配置缺失; NET_ERR_STATE 已在连接中;
 *             NET_ERR_CONN TCP 发起失败
 */
int mqtt_client_do_connect(struct mqtt_client_context* context);

/**
 * @brief 断开 MQTT 连接 (发 DISCONNECT 报文并关闭 TCP)
 * @param[in] context MQTT 客户端控制块
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int mqtt_client_disconnect(struct mqtt_client_context* context);

/**
 * @brief 检查 MQTT 客户端是否已连接
 * @param[in] context MQTT 客户端控制块
 * @return bool 连接状态
 */
bool is_mqtt_client_connected(const struct mqtt_client_context* context);

/**
 * @brief 发布 MQTT 消息（封装 coreMQTT MQTT_Publish, QoS 0/1/2 全支持）
 * @param[in] context MQTT 客户端控制块
 * @param[in] topic   Topic 字符串
 * @param[in] payload 消息载荷
 * @param[in] length  消息长度
 * @param[in] qos     QoS 等级
 * @param[in] retain  是否保留消息
 * @return int NET_OK 成功; NET_ERR_INVAL 未连接/入参非法; NET_ERR_NOSPC 序列化缓冲不足
 */
int mqtt_client_publish(struct mqtt_client_context* context, const char* topic, const void* payload, uint16_t length, uint8_t qos, bool retain);

/**
 * @brief 订阅 MQTT 主题（封装 coreMQTT MQTT_Subscribe）
 * @param[in] context MQTT 客户端控制块
 * @param[in] topic   Topic 过滤串 (可带 +/# 通配符)
 * @param[in] qos     QoS 等级
 * @return int NET_OK 成功; NET_ERR_INVAL 未连接/入参非法; NET_ERR_NOSPC 序列化缓冲不足
 */
int mqtt_client_subscribe(struct mqtt_client_context* context, const char* topic, uint8_t qos);

/**
 * @brief 取消订阅 MQTT 主题（封装 coreMQTT MQTT_Unsubscribe）
 * @param[in] context MQTT 客户端控制块
 * @param[in] topic   Topic 过滤串
 * @return int NET_OK 成功; NET_ERR_INVAL 未连接/入参非法; NET_ERR_NOSPC 序列化缓冲不足
 */
int mqtt_client_unsubscribe(struct mqtt_client_context* context, const char* topic);

/**
 * @brief 协议栈处理引擎, 需在主循环或专用任务中周期性调用
 * @param[in] context MQTT 客户端控制块
 * @note 内部驱动 TCP 建连后续握手、MQTT_ProcessLoop 收包解析、
 *       心跳保活与重发均由 coreMQTT 在此函数内完成
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法
 */
int mqtt_client_process(struct mqtt_client_context* context);
#ifdef __cplusplus
}
#endif
#endif /* MQTT_CLIENT_H */
