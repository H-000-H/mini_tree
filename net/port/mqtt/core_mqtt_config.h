/**
 * @file core_mqtt_config.h
 * @author H-000-H
 * @brief coreMQTT 库配置头 (mini_tree_link_coremqtt 强制要求)
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef CORE_MQTT_CONFIG_H
#define CORE_MQTT_CONFIG_H

#include "system_log.h"

#define COREMQTT_LOG_TAG "coremqtt"

#define COREMQTT_LOG_E(...) MT_LOG_ERROR(COREMQTT_LOG_TAG, __VA_ARGS__)
#define COREMQTT_LOG_W(...) MT_LOG_WARN(COREMQTT_LOG_TAG, __VA_ARGS__)
#define COREMQTT_LOG_I(...) MT_LOG_INFO(COREMQTT_LOG_TAG, __VA_ARGS__)

#define LogError(message) COREMQTT_LOG_E message
#define LogWarn(message) COREMQTT_LOG_W message
#define LogInfo(message) COREMQTT_LOG_I message
#define LogDebug(message) /**< 调试日志静默, 需要时改映射到 MINI_LOG_D */

#endif /* CORE_MQTT_CONFIG_H */
