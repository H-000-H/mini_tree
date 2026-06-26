#include "task_manager.hpp"
#include "task_manager.h"

#include "system_cfg.h"
#include "system_wdt.hpp"
#include "compiler_compat_poison.h"

static constexpr const char* kTag = "TaskManager";

osal_task_handle_t TaskManager::create(const struct board_task_config& config, TaskEntry entry, void* param)
{
    if (entry == nullptr)
    {
        SYS_LOGE(kTag, "task entry is null: %s", config.name);
        return nullptr;
    }

    osal_task_handle_t handle = nullptr;
    int ret = osal_task_create_handle(config.name, config.stack_size, config.priority,
                                       entry, param, config.core_id, &handle);
    if (ret != 0)
    {
        SYS_LOGE(kTag, "failed to create task: %s", config.name);
        return nullptr;
    }

    /* 自动订阅 TWDT (如果 TWDT 已初始化) */
    system_wdt_subscribe(handle);

    return handle;
}

osal_task_handle_t TaskManager::create_task(const char* name, uint32_t stack_size,
                                            uint32_t priority, TaskEntry entry,
                                            void* param, int core_id)
{
    struct board_task_config cfg = {};
    cfg.name = name ? name : "unknown";
    cfg.stack_size = stack_size;
    cfg.priority = priority;
    cfg.core_id = core_id;
    return create(cfg, entry, param);
}

extern "C" osal_task_handle_t task_manager_create(const struct board_task_config* config,
                                                  void (*entry)(void*), void* param)
{
    if (!config || !entry)
        return nullptr;
    return TaskManager::create(*config, entry, param);
}

extern "C" osal_task_handle_t task_manager_create_task(const char* name, uint32_t stack_size,
                                                       uint32_t priority, void (*entry)(void*),
                                                       void* param, int core_id)
{
    return TaskManager::create_task(name, stack_size, priority, entry, param, core_id);
}
