#include "task_utils.h"
#include "osal.h"

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
