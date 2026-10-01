/**
 * @file mqtt_broker.h
 * @author H-000-H
 * @brief MQTT Broker Header File (明文, 无 TLS)
 * @note 本文件为基于 tcp_server 会话表的轻量 MQTT Broker (服务端):
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "core_mqtt.h"
#include "net_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_MQTT_BROKER_MAX_SUBSCRIPTIONS
#define MQTT_BROKER_MAX_SUBSCRIPTIONS CONFIG_MQTT_BROKER_MAX_SUBSCRIPTIONS
#else
#define MQTT_BROKER_MAX_SUBSCRIPTIONS 16
#endif

#ifdef CONFIG_MQTT_BROKER_MAX_TOPIC_LEN
#define MQTT_BROKER_MAX_TOPIC_LEN CONFIG_MQTT_BROKER_MAX_TOPIC_LEN
#else
#define MQTT_BROKER_MAX_TOPIC_LEN 64
#endif

#ifdef CONFIG_MQTT_BROKER_PACKET_BUFFER_SIZE
#define MQTT_BROKER_PACKET_BUFFER_SIZE CONFIG_MQTT_BROKER_PACKET_BUFFER_SIZE
#else
#define MQTT_BROKER_PACKET_BUFFER_SIZE 1024
#endif

#ifdef CONFIG_MQTT_BROKER_KEEPALIVE_FACTOR
#define MQTT_BROKER_KEEPALIVE_FACTOR CONFIG_MQTT_BROKER_KEEPALIVE_FACTOR
#else
#define MQTT_BROKER_KEEPALIVE_FACTOR 2
#endif

/**
 * @brief 初始化并启动 MQTT Broker (底层即 tcp_server 监听指定端口)
 * @param[in] port 监听端口 (标准明文为 1883)
 * @return int NET_OK 成功; NET_ERR_INVAL 入参非法; NET_ERR_CONN 端口占用/资源不足
 */
int mqtt_broker_init(uint16_t port);

/**
 * @brief Broker 处理引擎, 需在主循环或专用任务中周期性调用
 * @note 驱动: 各会话收包累积与报文解析 (CONNECT/CONNACK/SUBSCRIBE/PUBLISH...),
 *       订阅匹配转发, keep-alive 超时踢线; 均在本函数内同步完成
 * @return int NET_OK 成功; NET_ERR_INVAL 未初始化
 */
int mqtt_broker_process(void);
#ifdef __cplusplus
}
#endif
#endif /* MQTT_BROKER_H */
