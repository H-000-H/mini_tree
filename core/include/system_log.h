/**
 * @file system_log.h
 * @author H-000-H
 * @brief 系统日志宏统一入口 (MT_LOG_* / MT_DRV_LOG_*)
 * @note  后端由 Kconfig CONFIG_SYS_LOG_USE_* 选择; 各模块不要直接调用底层后端
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEM_LOG_H
#define SYSTEM_LOG_H

/* Kconfig 生成的配置 — 见 tools/genconfig.py */
#include "config.h"

#if defined(CONFIG_SYS_LOG_USE_MINI_LOG)

#include "log.h"

/* mini-log 的 MINI_LOG_x 宏不带 tag; 这里把 tag 作为前缀并入格式串,
 * 保持全仓 (tag, fmt, ...) 的既有调用约定。 */
#define MT_LOG_ERROR(tag, fmt, ...) MINI_LOG_E("[%s] " fmt, tag, ##__VA_ARGS__)
#define MT_LOG_WARN(tag, fmt, ...) MINI_LOG_W("[%s] " fmt, tag, ##__VA_ARGS__)
#define MT_LOG_INFO(tag, fmt, ...) MINI_LOG_I("[%s] " fmt, tag, ##__VA_ARGS__)

#elif defined(CONFIG_SYS_LOG_USE_ESP)

#include "esp_log.h"
#define MT_LOG_INFO ESP_LOGI
#define MT_LOG_WARN ESP_LOGW
#define MT_LOG_ERROR ESP_LOGE
#define MT_DRV_LOG_ERROR ESP_LOGE
#define MT_DRV_LOG_WARN ESP_LOGW
#define MT_DRV_LOG_INFO ESP_LOGI
#define MT_DRV_LOG_DEBUG ESP_LOGD
#define MT_DRV_LOG_VERBOSE ESP_LOGD

#else
#error "SYS_LOG backend not configured — choose one in Kconfig"
#endif

#if defined(CONFIG_SYS_LOG_USE_MINI_LOG)
/* -------------------------------------------------------------------------- */
/* 驱动日志宏 (MT_DRV_LOG_*) — 统一走 mini-log 控制台链路 */
/* -------------------------------------------------------------------------- */
#define MT_DRV_LOG_ERROR(tag, fmt, ...) MINI_LOG_E("[%s] " fmt, tag, ##__VA_ARGS__)
#define MT_DRV_LOG_WARN(tag, fmt, ...) MINI_LOG_W("[%s] " fmt, tag, ##__VA_ARGS__)
#define MT_DRV_LOG_INFO(tag, fmt, ...) MINI_LOG_I("[%s] " fmt, tag, ##__VA_ARGS__)
#define MT_DRV_LOG_DEBUG(tag, fmt, ...) MINI_LOG_D("[%s] " fmt, tag, ##__VA_ARGS__)
#define MT_DRV_LOG_VERBOSE(tag, fmt, ...) MINI_LOG_D("[%s] " fmt, tag, ##__VA_ARGS__)
#endif

#endif /* SYSTEM_LOG_H */
