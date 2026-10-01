/**
 * @file board_config.h
 * @author H-000-H
 * @brief 板级配置聚合层 (DTS + Kconfig + 默认值 fallback)
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* 配置源: board.dts/dtsi → dt_config_gen.h → config.h (Kconfig) */

#include "dt_config_gen.h"

#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef BOARD_MAX_SAFETY_PINS
#ifdef CONFIG_BOARD_MAX_SAFETY_PINS
#define BOARD_MAX_SAFETY_PINS CONFIG_BOARD_MAX_SAFETY_PINS
#else
#define BOARD_MAX_SAFETY_PINS 8
#endif
#endif

#ifndef BOARD_SAFETY_MAX_CALLBACKS
#ifdef CONFIG_BOARD_SAFETY_MAX_CALLBACKS
#define BOARD_SAFETY_MAX_CALLBACKS CONFIG_BOARD_SAFETY_MAX_CALLBACKS
#else
#define BOARD_SAFETY_MAX_CALLBACKS 4
#endif
#endif

#ifndef BOARD_STACK_MONITOR_MAX_TASKS
#ifdef CONFIG_BOARD_STACK_MONITOR_MAX_TASKS
#define BOARD_STACK_MONITOR_MAX_TASKS CONFIG_BOARD_STACK_MONITOR_MAX_TASKS
#else
#define BOARD_STACK_MONITOR_MAX_TASKS 8
#endif
#endif

#define BOARD_STACK_ALARM_RATIO_DEFAULT 15

#ifndef BOARD_SAFE_STATE_BUZZER_PIN
#define BOARD_SAFE_STATE_BUZZER_PIN 0
#endif
#ifndef BOARD_SAFE_STATE_FAULT_LED_PIN
#define BOARD_SAFE_STATE_FAULT_LED_PIN 0
#endif

#ifndef MINI_MUTEX_POOL_SIZE
#ifdef CONFIG_OS_MUTEX_POOL_SIZE
#define MINI_MUTEX_POOL_SIZE CONFIG_OS_MUTEX_POOL_SIZE
#else
#define MINI_MUTEX_POOL_SIZE 12
#endif
#endif

#ifndef MINI_MUTEX_STORAGE_SIZE
#define MINI_MUTEX_STORAGE_SIZE 128
#endif

#ifndef MINI_SEM_POOL_SIZE
#define MINI_SEM_POOL_SIZE 4
#endif

#endif /* BOARD_CONFIG_H */
