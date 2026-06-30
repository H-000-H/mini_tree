/* SPDX-License-Identifier: Apache-2.0 */
/*
 * task_utils.h — 板级任务创建工具头文件
 *
 * 声明 board_task_entry_t 任务入口函数指针类型.
 * 声明 board_task_create: 封装 OSAL 任务创建的薄包装,
 *   透传名称/栈/优先级/入口/参数/核心, 失败返回 NULL.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

typedef void (*board_task_entry_t)(void* param);

void* board_task_create(const char* name, uint32_t stack_size,
                        uint32_t priority, board_task_entry_t entry,
                        void* param, int core_id);

#ifdef __cplusplus
}
#endif

