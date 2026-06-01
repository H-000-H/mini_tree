#pragma once

#include <stdio.h>

/* Kconfig 生成的配置 — 见 tools/genconfig.py */
#include "config.h"

#if defined(CONFIG_SYS_LOG_USE_OSAL)
  #include "osal.h"
  #define SYS_LOGI(tag, fmt, ...)  osal_log(OSAL_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
  #define SYS_LOGW(tag, fmt, ...)  osal_log(OSAL_LOG_WARN,  tag, fmt, ##__VA_ARGS__)
  #define SYS_LOGE(tag, fmt, ...)  osal_log(OSAL_LOG_ERROR, tag, fmt, ##__VA_ARGS__)

#elif defined(CONFIG_SYS_LOG_USE_ESP)
#include "esp_log.h"
  #define SYS_LOGI  ESP_LOGI
  #define SYS_LOGW  ESP_LOGW
  #define SYS_LOGE  ESP_LOGE

#elif defined(CONFIG_SYS_LOG_USE_PRINTF)
  #define SYS_LOGI(tag, fmt, ...)  printf("[I][%s] " fmt "\r\n", tag, ##__VA_ARGS__)
  #define SYS_LOGW(tag, fmt, ...)  printf("[W][%s] " fmt "\r\n", tag, ##__VA_ARGS__)
  #define SYS_LOGE(tag, fmt, ...)  printf("[E][%s] " fmt "\r\n", tag, ##__VA_ARGS__)

#else
  #error "SYS_LOG backend not configured — choose one in Kconfig"
#endif

/* ── 驱动日志宏 (DRV_LOG) ──
 * 原位于 osal.h, 提升至 middleware 层以消除层级倒置.
 * 依赖 production_log 的变体 (LOGE/LOGW) 推送至黑匣子环形缓冲区.
 */
#include "production_log.h"

#define DRV_LOGE(tag, fmt, ...) do { \
    osal_log(OSAL_LOG_ERROR, tag, fmt, ##__VA_ARGS__); \
    production_log_push_fmt(0, tag, fmt, ##__VA_ARGS__); \
} while(0)
#define DRV_LOGW(tag, fmt, ...) do { \
    osal_log(OSAL_LOG_WARN,  tag, fmt, ##__VA_ARGS__); \
    production_log_push_fmt(1, tag, fmt, ##__VA_ARGS__); \
} while(0)
#define DRV_LOGI(tag, fmt, ...) osal_log(OSAL_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define DRV_LOGD(tag, fmt, ...) osal_log(OSAL_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
