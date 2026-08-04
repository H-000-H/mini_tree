/* SPDX-License-Identifier: Apache-2.0 */
/*
 * osal_task.cpp — 裸机 (CONFIG_OSAL_NULL) 专用: osal_task_create 的 C++ 重载
 * 仅裸机需要本封装: OS 后端由 osal_*.c 的标准 C API 直通 RTOS 内核, 无需本文件;
 * 裸机 C API 恒返回 OSAL_ERR_NOTSUPP, 任务创建只能走本重载 (xtask 周期任务).
 * 编译开关: Kconfig CONFIG_OSAL_NULL_TASK_CPP (依赖 SYSTEM_CPP, 默认开启 = 走统一;
 * 关闭 = 靠 xtask 自己, 本文件不编译) — 由 CMake 按 .config 裁剪本文件;
 * 此处只需 CONFIG_OSAL_NULL 守卫 (注意: 不能在此判断 CONFIG_OSAL_NULL_TASK_CPP,
 * 它来自生成的 config.h, 而 config.h 是在本守卫内部才被包含);
 * 声明见 osal_null.h 的 __cplusplus 段 (那里有同开关守卫, 且宏已可见).
 */
#ifdef CONFIG_OSAL_NULL

#include "osal_null.h"

#include "etl/optional.h"

#include <stdint.h>

/**
 * @brief 创建任务 (C++ 重载, 句柄以 etl::optional 返回; 仅裸机构建下编译, 声明见 osal_null.h)
 * @details 裸机与 OS 后端的参数语义完全不同, 差异对照:
 * @details ┌─────────────┬───────────────────────────────┬───────────────────────────────────┐
 * @details │ 参数         │ 裸机 (CONFIG_OSAL_NULL)        │ OS (FreeRTOS / RT-Thread)         │
 * @details ├─────────────┼───────────────────────────────┼───────────────────────────────────┤
 * @details │ name        │ 仅写入 TCB 供调试              │ RTOS 任务名 (内核/调试使用)        │
 * @details │ stack_size  │ 忽略: 无动态栈, TCB 由调用方    │ 任务栈大小 (字节, 内核内部按字     │
 * @details │             │ 静态分配                       │ 换算为 words)                     │
 * @details │ period     │ 任务周期 (ms) (xtask 周期协作式  │ OS 下此位置是 priority (RTOS 任务   │
 * @details │             │ 轮询; 裸机无优先级概念)         │ 优先级), 语义完全不同             │
 * @details │ entry       │ 签名 void(*)(x_task*), 与 xtask │ 签名 void(*)(void*), 直接交给    │
 * @details │             │ 回调一致, 无需转换; 参数即 TCB  │ RTOS 内核, 参数为 param1          │
 * @details │ param1      │ 必须为调用方静态分配的 x_task*  │ 传给 entry 的唯一参数             │
 * @details │             │ TCB (为空则返回 nullopt)       │                                   │
 * @details │ param2      │ 可选 x_scheduler*, 默认        │ 无对应 (C API 无此参数)           │
 * @details │             │ &g_scheduler (全局单例调度器)  │                                   │
 * @details │ core_id     │ 忽略: 仅保持与 C API 参数列表  │ 目标核心 (>0 时 FreeRTOS 后端      │
 * @details │             │ 形状一致, 不参与调度           │ 警告并回退 Core 0)                │
 * @details └─────────────┴───────────────────────────────┴───────────────────────────────────┘
 * @details
 * @details 裸机特注:
 * @details 1. 裸机任务没有优先级与栈的概念 (无抢占, 周期协作式调度), 复杂任务必须自实现状态机;
 * @details 2. entry 的参数就是注册时的 x_task* TCB 本身, 不是 param1:
 * @details    调度器以 task->xTask_cb(task) 调用回调 (见 xtask.c x_task_run), 类型已由签名保证;
 * @details 3. 同一个 x_task* 重复注册会再次尾插链表导致重复调度, 请勿重复创建;
 * @details 4. 时基依赖 xtask 的虚拟时基中断, 未调用 xscheduler_start() 前任务不会运行;
 * @details 5. 本重载与 C API 靠 entry 签名区分: 用 void(*)(void*) 的 OS 风格入口调用会选中
 * @details    C API (裸机下恒返回 OSAL_ERR_NOTSUPP), 裸机请使用本重载的 void(*)(x_task*) 入口.
 * @param name 任务名称
 * @param stack_size 任务栈大小 (字节); 忽略
 * @param period 任务周期 (ms) (裸机无优先级概念; OS 后端对应参数名为 priority, 语义为 RTOS 优先级)
 * @param entry 任务入口函数, 签名 void (*)(x_task*), 与 xtask 回调一致, 参数为 x_task* TCB 本身
 * @param param1 调用方静态分配的 x_task* TCB (传入裸机任务控制块)
 * @param param2 可选 x_scheduler* (nullptr 时默认 &g_scheduler)
 * @param core_id 目标核心 (此处无效, 仅与 C API 参数列表匹配)
 * @return 成功返回任务句柄 (x_task 指针地址); 失败返回 etl::nullopt
 */

/**
 * @brief 任务句柄类型
 * @details 与 x_task_handle_t 类型相同, 用于返回任务句柄
 * @details 用于与 C API 的返回类型兼容
 */
using osal_task_handle_t = x_task_handle_t;

etl::optional<osal_task_handle_t> osal_task_create(const char* name, uint32_t stack_size,uint32_t period, void (*entry)(x_task*),void* param1, void* param2, int core_id)
{
    COMPAT_IGNORE_RESULT(stack_size);
    COMPAT_IGNORE_RESULT(param2);
    COMPAT_IGNORE_RESULT(core_id);
    if (param1 == nullptr || entry == nullptr)
        return etl::nullopt;

    x_task* task = static_cast<x_task*>(param1);
    if (param2 == nullptr)
        param2 = static_cast<x_scheduler*>(&g_scheduler);

    osal_task_handle_t handle = xscheduler_task_create(static_cast<x_scheduler*>(param2), task, name,entry,static_cast<unsigned int>(period));
    if (handle == 0) /* xscheduler_task_create 失败返回 0, 成功返回 x_task 指针地址 */
        return etl::nullopt;
    return etl::make_optional(handle);
}

#endif /* CONFIG_OSAL_NULL */
