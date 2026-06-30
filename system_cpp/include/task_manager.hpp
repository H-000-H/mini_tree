/* SPDX-License-Identifier: Apache-2.0 */
/*
 * TaskManager — OSAL 任务创建工具类
 *
 * 基于 board_task_config 结构创建任务, 自动订阅 TWDT 看门狗
 * 提供两类接口: create (配置结构体) 与 create_task (参数直传)
 * 头文件 task_manager.h 暴露 C 链接接口供纯 C 模块调用
 */
#pragma once

#include "task_config.h"
#include "osal.h"

class TaskManager
{
public:
    using TaskEntry = void (*)(void* param);

    static osal_task_handle_t create(const struct board_task_config& config, TaskEntry entry, void* param);

    /** 简便版: 从参数直接创建任务, 无需预先定义 struct board_task_config */
    static osal_task_handle_t create_task(const char* name, uint32_t stack_size,
                                        uint32_t priority, TaskEntry entry,
                                        void* param, int core_id);
};
