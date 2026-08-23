/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file core_mqtt_config.h
 * @brief coreMQTT 库配置头 (mini_tree_link_coremqtt 强制要求)
 * @author H-000-H
 * @details 库日志宏对接系统日志后端 (SYS_LOG*)。core 库日志宏调用形式为。
 *          LogDebug 映射为空 (避免热路径刷屏); 其余按库默认值运行。
 */
#ifndef CORE_MQTT_CONFIG_H
#define CORE_MQTT_CONFIG_H

#include "system_log.h"

#define COREMQTT_LOG_TAG "coremqtt"

#define COREMQTT_LOG_E(...) SYS_LOGE(COREMQTT_LOG_TAG, __VA_ARGS__)
#define COREMQTT_LOG_W(...) SYS_LOGW(COREMQTT_LOG_TAG, __VA_ARGS__)
#define COREMQTT_LOG_I(...) SYS_LOGI(COREMQTT_LOG_TAG, __VA_ARGS__)

#define LogError(message) COREMQTT_LOG_E message
#define LogWarn(message) COREMQTT_LOG_W message
#define LogInfo(message) COREMQTT_LOG_I message
#define LogDebug(message) /**< 调试日志静默, 需要时改映射到 osal_log DEBUG */

#endif /* CORE_MQTT_CONFIG_H */
