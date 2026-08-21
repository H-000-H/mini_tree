/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_log.h
 *@brief system log 头文件
 *@author H-000-H
 *@details
 *   system_log — 系统日志宏统一入口 (OSAL / ESP-IDF / RT-Thread 三后端)
 *   根据 Kconfig CONFIG_SYS_LOG_USE_* 选择后端, 提供 SYS_LOGI/W/E 三级宏。
 */

#ifndef SYSTEM_LOG_H
#define SYSTEM_LOG_H

/* Kconfig 生成的配置 — 见 tools/genconfig.py */
#include "config.h"

#ifndef OSAL_LOG_LEVEL_T_DEFINED
#define OSAL_LOG_LEVEL_T_DEFINED
typedef enum
{
    OSAL_LOG_ERROR = 0,
    OSAL_LOG_WARN,
    OSAL_LOG_INFO,
    OSAL_LOG_DEBUG,
} osal_log_level_t;
#endif

#if defined(CONFIG_SYS_LOG_USE_OSAL) || defined(CONFIG_SYS_LOG_USE_PRINTF)
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...);
#define SYS_LOGI(tag, fmt, ...) osal_log(OSAL_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define SYS_LOGW(tag, fmt, ...) osal_log(OSAL_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define SYS_LOGE(tag, fmt, ...) osal_log(OSAL_LOG_ERROR, tag, fmt, ##__VA_ARGS__)

#elif defined(CONFIG_SYS_LOG_USE_ESP)
#include "esp_log.h"
#define SYS_LOGI ESP_LOGI
#define SYS_LOGW ESP_LOGW
#define SYS_LOGE ESP_LOGE
#define DRV_LOGE ESP_LOGE
#define DRV_LOGW ESP_LOGW
#define DRV_LOGI ESP_LOGI
#define DRV_LOGD ESP_LOGD
#define DRV_LOGV ESP_LOGD

#else
#error "SYS_LOG backend not configured — choose one in Kconfig"
#endif

#if defined(CONFIG_SYS_LOG_USE_OSAL) || defined(CONFIG_SYS_LOG_USE_PRINTF)
/* ── 驱动日志宏 (DRV_LOG) ──
 * 原位于 osal.h, 提升至 middleware 层以消除层级倒置.
 * 依赖 production_log 的变体 (LOGE/LOGW) 推送至黑匣子环形缓冲区.
 */
#include "production_log.h" /* IWYU pragma: keep */

#define DRV_LOGE(tag, fmt, ...)                                                                    \
    do                                                                                             \
    {                                                                                              \
        osal_log(OSAL_LOG_ERROR, tag, fmt, ##__VA_ARGS__);                                         \
        production_log_push_fmt(0, tag, fmt, ##__VA_ARGS__);                                       \
    } while (0)
#define DRV_LOGW(tag, fmt, ...)                                                                    \
    do                                                                                             \
    {                                                                                              \
        osal_log(OSAL_LOG_WARN, tag, fmt, ##__VA_ARGS__);                                          \
        production_log_push_fmt(1, tag, fmt, ##__VA_ARGS__);                                       \
    } while (0)
#define DRV_LOGI(tag, fmt, ...) osal_log(OSAL_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define DRV_LOGD(tag, fmt, ...) osal_log(OSAL_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define DRV_LOGV(tag, fmt, ...) osal_log(OSAL_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#endif

#endif /* SYSTEM_LOG_H */
