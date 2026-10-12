/**
 * @file task_manager.h
 * @author H-000-H
 * @brief task manager 头文件
 * @note task_manager (C 接口) — 任务创建便捷封装
 * @note 包装 mini_task_create_handle, 自动订阅 TWDT (若已初始化)。
 * @note 实现见 system_c/src/task_manager.c。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "mini_backend.h"
#include "task_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 按配置结构创建任务 (自动订阅 TWDT 若已初始化)
 * @param[in] config 任务配置 (name/stack_size/priority/core_id)
 * @param[in] entry 任务入口函数
 * @param[in] param 任务参数
 * @return 任务句柄; 创建失败返回 NULL
 */
mini_task_handle_t task_manager_create(const struct board_task_config* config, void (*entry)(void*), void* param);

/**
 * @brief 按显式参数创建任务 (自动订阅 TWDT 若已初始化)
 * @param[in] name 任务名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 优先级
 * @param[in] entry 任务入口函数
 * @param[in] param 任务参数
 * @param[in] core_id 核心号 (-1=任意核心)
 * @return 任务句柄; 创建失败返回 NULL
 */
mini_task_handle_t task_manager_create_task(const char* name, uint32_t stack_size, uint32_t priority, void (*entry)(void*), void* param, int core_id);

#ifdef __cplusplus
}
#endif
