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

/**
 * @brief 创建板级 OSAL 任务
 * @param name 任务名称
 * @param stack_size 栈大小 (字节)
 * @param priority 任务优先级
 * @param entry 任务入口函数
 * @param param 入口参数
 * @param core_id 绑定 CPU 核心 (-1 表示不绑定)
 * @return 成功返回任务句柄, 失败返回 NULL
 */
void* board_task_create(const char* name, uint32_t stack_size, uint32_t priority,
                        board_task_entry_t entry, void* param, int core_id)
{
    osal_task_handle_t handle = NULL;
    int ret = osal_task_create_handle(name, stack_size, priority, (osal_task_entry_t)entry, param,
                                      core_id, &handle);
    return (ret == OSAL_OK) ? (void*)handle : NULL;
}
