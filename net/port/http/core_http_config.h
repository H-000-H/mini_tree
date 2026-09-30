/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file core_http_config.h
 * @brief coreHTTP 库配置头 (mini_tree_link_corehttp 强制要求)
 * @author H-000-H
 * @details 把库内部的日志宏接到系统日志 (SYS_LOG*) 上。
 *          调试级日志默认不输出, 需要时改 LogDebug 这一行。
 */
#ifndef CORE_HTTP_CONFIG_H
#define CORE_HTTP_CONFIG_H

#include "system_log.h"

#define COREHTTP_LOG_TAG "corehttp"

#define COREHTTP_LOG_E(...) MT_LOG_ERROR(COREHTTP_LOG_TAG, __VA_ARGS__)
#define COREHTTP_LOG_W(...) MT_LOG_WARN(COREHTTP_LOG_TAG, __VA_ARGS__)
#define COREHTTP_LOG_I(...) MT_LOG_INFO(COREHTTP_LOG_TAG, __VA_ARGS__)

#define LogError(message) COREHTTP_LOG_E message
#define LogWarn(message) COREHTTP_LOG_W message
#define LogInfo(message) COREHTTP_LOG_I message
#define LogDebug(message) /**< 调试日志默认不输出 */

#endif /* CORE_HTTP_CONFIG_H */
