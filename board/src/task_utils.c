/* SPDX-License-Identifier: Apache-2.0 */
/*
 * task_utils.c — 板级任务创建包装实现
 *
 * board_task_create 封装 osal_task_create_handle,
 *   透传名称/栈/优先级/入口/参数/核心, 成功返回任务句柄, 失败返回 NULL.
 */
#include "task_utils.h"
#include "osal.h"
#include "compiler_compat_poison.h"

void* board_task_create(const char* name, uint32_t stack_size,
                        uint32_t priority, board_task_entry_t entry,
                        void* param, int core_id)
{
    osal_task_handle_t handle = NULL;
    int ret = osal_task_create_handle(name, stack_size, priority,
                                       (osal_task_entry_t)entry, param,
                                       core_id, &handle);
    return (ret == 0) ? (void*)handle : NULL;
}
