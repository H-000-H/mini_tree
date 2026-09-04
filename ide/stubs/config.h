/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file config.h
 *@brief config 头文件
 *@author H-000-H
 *@details
 *   IDE-only stub — 无构建树时的 clangd 兜底默认值。
 *   角色定位（非 ESP 平台 CH32V307 / STM32 等亦共用此文件）:
 *   - 有构建树: #include "config.h" 由构建 include 顺序优先命中真实头
 *   (非 ESP: KCONFIG_GEN_DIR 由 genconfig.py 生成; ESP: 转发 sdkconfig.h),
 *   clangd 拿到真实 CONFIG_*。
 *   - 无构建树: 仅靠本文件兜底, 值须与 mini_tree/.config 尽量同步, 否则
 *   会出现 P9 (ide/stubs 与 real 头不同步 → clangd 误报) 已知问题。
 *   本文件由 CMake configure 钩子自动再生的计划见 roadmap T5。
 */

#ifndef KCONFIG_CONFIG_H
#define KCONFIG_CONFIG_H

#define CONFIG_PLATFORM_ARM_CM4F 1
#define CONFIG_CPU_CORES 1
#define CONFIG_OSAL_NULL 1
#define CONFIG_OSAL_SPINLOCK_IRQ_DISABLE 1
#define CONFIG_SYS_LOG_USE_PRINTF 1
#define CONFIG_SYSTEM 1
#define CONFIG_SYSTEM_CPP 1
#define CONFIG_USB 1
#define CONFIG_VIRQ 1
#define CONFIG_SYSTEM_WDT 1
#define CONFIG_OSAL_NULL_MAX_QUEUES 0
#define CONFIG_OSAL_NULL_QUEUE_BUF_SZ 2048
#define CONFIG_EVENT_BUS_QUEUE_LEN 64
#define CONFIG_EVENT_BUS_MAX_SUBSCRIBERS 24
#define CONFIG_EVENT_BUS_DISPATCH_STACK 2048
#define CONFIG_BOTTOM_HALF_QUEUE_DEPTH 16
#ifndef CONFIG_OSAL_MUTEX_POOL_SIZE
#define CONFIG_OSAL_MUTEX_POOL_SIZE 24
#endif
#define CONFIG_BOARD_STACK_MONITOR_MAX_TASKS 8
#define CONFIG_COMPILER_GNU_EXTENSIONS 1
#define CONFIG_COMPILER_WARN_UNUSED_RESULT 1
/* CONFIG_DEVICE_WARN_UNUSED_RESULT off by default (app-layer relaxed, mirror .config) */
#define CONFIG_BUILD_DISASM 1

#endif /* KCONFIG_CONFIG_H */
