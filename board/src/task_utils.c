/**
 * @file task_utils.c
 * @author H-000-H
 * @brief task utils 实现
 * @note task_utils.c — 板级任务创建包装实现
 * @note board_task_create 封装 mini_task_create_handle,
 * @note 透传名称/栈/优先级/入口/参数/核心, 成功返回任务句柄, 失败返回 NULL.
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "task_utils.h"

#include "mini_backend.h"

#include "compiler_compat_poison.h"

/**
 * @brief 创建板级任务
 * @param[in] name 任务名称
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 任务优先级
 * @param[in] entry 任务入口函数
 * @param[in] param 入口参数
 * @param[in] core_id 绑定 CPU 核心 (-1 表示不绑定)
 * @return 成功返回任务句柄, 失败返回 NULL
 */
void* board_task_create(const char* name, uint32_t stack_size, uint32_t priority, board_task_entry_t entry, void* param, int core_id)
{
    mini_task_handle_t handle = NULL;
    int                ret = mini_task_create_handle(name, stack_size, priority, (mini_task_entry_t)entry, param, core_id, &handle);
    return (ret == MINI_OK) ? (void*)handle : NULL;
}
