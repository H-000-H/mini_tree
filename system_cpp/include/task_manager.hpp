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
