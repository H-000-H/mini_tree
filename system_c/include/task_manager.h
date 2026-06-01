#pragma once

#include "task_config.h"
#include "osal.h"

#ifdef __cplusplus
extern "C" {
#endif

osal_task_handle_t task_manager_create(const board_task_config_t* config,
                                       void (*entry)(void*), void* param);

osal_task_handle_t task_manager_create_task(const char* name, uint32_t stack_size,
                                            uint32_t priority, void (*entry)(void*),
                                            void* param, int core_id);

#ifdef __cplusplus
}
#endif
