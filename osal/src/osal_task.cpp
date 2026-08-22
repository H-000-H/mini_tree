/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file osal_task.cpp
 * @brief 裸机 (CONFIG_OSAL_NULL) 专用: osal_task_create 的 C++ 重载
 * @note  仅裸机需要本封装; OS 后端由 osal_*.c 标准 C API 直通 RTOS 内核.
 *        裸机 C API 恒返回 OSAL_ERR_NOTSUPP, 任务创建走本重载 (xtask 周期任务).
 *
 * 调度器后端互斥 (与 time_slice/task 同步):
 *   - CONFIG_XTASK_PREEMPT 未定义: 协调式分支 (period 参数为周期, 无优先级)
 *   - CONFIG_XTASK_PREEMPT 已定义: 抢占式分支 (stack_size 位复用为周期, 有 priority)
 *
 * 注意: 本文件顶部不能判断 CONFIG_OSAL_NULL_TASK_CPP —— 它来自生成的 config.h,
 */
#ifdef CONFIG_OSAL_NULL

#include "osal_null.h"
#include "etl/optional.h"
#include <stdint.h>

using osal_task_handle_t = x_task_handle_t;

#ifndef CONFIG_XTASK_PREEMPT

/**
 * @brief 创建协调式任务 (C++ 重载)
 * @param[in] name 任务名 (仅调试)
 * @param[in] stack_size 忽略 (TCB 由调用方静态分配)
 * @param[in] period 任务周期 (ms)
 * @param[in] entry 回调, 签名 void(*)(x_task*), 与 xtask 一致; 参数即 TCB
 * @param[in] param1 调用方静态分配的 x_task* TCB (必须非空)
 * @param[in] param2 忽略
 * @param[in] core_id 忽略 (保持与 C API 形状一致)
 * @return 任务句柄 (x_task*); 失败返回 etl::nullopt
 */
etl::optional<osal_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,uint32_t period, void (*entry)(x_task*),void* param1, void* param2, int core_id)
{
    COMPAT_IGNORE_RESULT(stack_size);
    COMPAT_IGNORE_RESULT(param2);
    COMPAT_IGNORE_RESULT(core_id);
    if (param1 == nullptr || entry == nullptr)
        return etl::nullopt;

    x_task* task = static_cast<x_task*>(param1);

    osal_task_handle_t handle = xscheduler_task_create(task, name, entry, static_cast<unsigned int>(period));
    if (handle == 0)
        return etl::nullopt;
    return etl::make_optional(handle);
}

#else /* CONFIG_XTASK_PREEMPT */

/**
 * @brief 创建抢占式任务 (C++ 重载)
 * @param[in] name 任务名
 * @param[in] stack_size 复用为任务周期 (ms) (裸机忽略栈)
 * @param[in] priority 优先级, 越大越优先
 * @param[in] entry 回调, 签名 void(*)(x_task*)
 * @param[in] param1 忽略 (任务池在调度器内部自分配)
 * @param[in] param2 忽略
 * @param[in] core_id 忽略
 * @return 任务句柄; 失败返回 etl::nullopt
 */
etl::optional<osal_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,uint32_t priority, void (*entry)(x_task*),void* param1, void* param2, int core_id)
{
    COMPAT_IGNORE_RESULT(param1);
    COMPAT_IGNORE_RESULT(param2);
    COMPAT_IGNORE_RESULT(core_id);
    if (name == nullptr || entry == nullptr)
        return etl::nullopt;

    osal_task_handle_t handle = x_scheduler_task_create(name, stack_size, priority, entry, nullptr);
    if (handle == 0)
        return etl::nullopt;
    return etl::make_optional(handle);
}

#endif /* CONFIG_XTASK_PREEMPT */
#endif /* CONFIG_OSAL_NULL */
