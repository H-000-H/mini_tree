/**
 * @file config.h
 * @author H-000-H
 * @brief config 头文件
 * @note IDE-only stub — 无构建树时的 clangd 兜底默认值。
 * @note 角色定位（非 ESP 平台 CH32V307 / STM32 等亦共用此文件）:
 * @note - 有构建树: #include "config.h" 由构建 include 顺序优先命中真实头
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef KCONFIG_CONFIG_H
#define KCONFIG_CONFIG_H

#define CONFIG_PLATFORM_ARM_CM4F 1
#define CONFIG_CPU_CORES 1
#define CONFIG_OS_BARE 1
#define CONFIG_SYS_LOG_USE_MINI_LOG 1
/* mini-log (mini-log/inc/log_config.h) — 与 .config 默认同步 */
#define CONFIG_MINI_LOG_MAX_LEN 128
#define CONFIG_MINI_LOG_RING_SIZE 1024
#define CONFIG_MINI_LOG_AUTO_FLUSH 1
#define CONFIG_MINI_LOG_COLOR_ENABLE 1
#define CONFIG_MINI_LOG_USE_FLASH 1
#define CONFIG_MINI_LOG_FLASH_RING_SIZE 512
#define CONFIG_MINI_LOG_DEFAULT_ALIGIN 4
#define CONFIG_MINI_LOG_MAGIC 4
#define CONFIG_SYSTEM 1
#define CONFIG_USB 1
#define CONFIG_VIRQ 1
#define CONFIG_SYSTEM_WDT 1
#define CONFIG_OS_BARE_MAX_QUEUES 0
#define CONFIG_OS_BARE_QUEUE_BUF_SZ 2048
#define CONFIG_EVENT_BUS_QUEUE_LEN 64
#define CONFIG_EVENT_BUS_MAX_SUBSCRIBERS 24
#define CONFIG_EVENT_BUS_DISPATCH_STACK 2048
#define CONFIG_BOTTOM_HALF_QUEUE_DEPTH 16
#ifndef CONFIG_OS_MUTEX_POOL_SIZE
#define CONFIG_OS_MUTEX_POOL_SIZE 24
#endif
#define CONFIG_BOARD_STACK_MONITOR_MAX_TASKS 8
#define CONFIG_COMPILER_GNU_EXTENSIONS 1
#define CONFIG_COMPILER_WARN_UNUSED_RESULT 1
/* CONFIG_DEVICE_WARN_UNUSED_RESULT off by default (app-layer relaxed, mirror .config) */
#define CONFIG_BUILD_DISASM 1

#endif /* KCONFIG_CONFIG_H */
