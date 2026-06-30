/* SPDX-License-Identifier: Apache-2.0 */
/*
 * task_manager (C 接口) — 任务创建便捷封装
 *
 * 包装 osal_task_create_handle, 自动订阅 TWDT (若已初始化)。
 * C++ 实现见 system_cpp/task_manager.hpp, 本头供 .c 文件调用。
 */
#pragma once

#include "task_config.h"
#include "osal.h"

#ifdef __cplusplus
extern "C" 
{
#endif

osal_task_handle_t task_manager_create(const struct board_task_config* config,
                                       void (*entry)(void*), void* param);

osal_task_handle_t task_manager_create_task(const char* name, uint32_t stack_size,
                                            uint32_t priority, void (*entry)(void*),
                                            void* param, int core_id);

#ifdef __cplusplus
}
#endif

