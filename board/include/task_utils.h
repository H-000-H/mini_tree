/**
 * @file task_utils.h
 * @author H-000-H
 * @brief 板级任务创建工具 (封装 OSAL 的薄包装)
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 板级任务入口函数类型
 * @param[in] param 任务参数
 */
typedef void (*board_task_entry_t)(void* param);

/**
 * @brief 创建板级任务 (封装统一接口任务创建的薄包装)
 * @param[in] name 任务名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 任务优先级
 * @param[in] entry 任务入口函数
 * @param[in] param 任务参数
 * @param[in] core_id 核心号 (-1=任意核心)
 * @return 任务句柄; 创建失败返回 NULL
 */
void* board_task_create(const char* name, uint32_t stack_size, uint32_t priority, board_task_entry_t entry, void* param, int core_id);

#ifdef __cplusplus
}
#endif
