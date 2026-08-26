/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file event_bus.h
 *@brief event bus 头文件
 *@author H-000-H
 *@details
 *   EventBus C 接口 — 轻量发布/订阅事件总线
 *   框架只搬运事件 ID, 不解释业务语义; 用户事件基于 EVENT_USER_BASE 自定义
 *   支持 ID 区间订阅, task 与 ISR 上下文均可 post
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* -------------------------------------------------------------------------- */
/* 框架级事件 ID (框架内部使用, 不涉及任何业务语义) */
/* -------------------------------------------------------------------------- */
#define EVENT_SYS_BOOT 0x0000 /* 系统冷启动完成 */
#define EVENT_SYS_READY 0x0001 /* 所有框架任务已就绪 */
#define EVENT_SYS_FAULT 0x0002 /* 系统级故障, 进入安全状态 */
#define EVENT_SYS_DEVICE_REMOVED 0x0003 /* 设备从设备树中移除 */

/* -------------------------------------------------------------------------- */
/* 用户事件基线 */
/* 用户工程在业务代码中基于此值定义自有事件: */
/* #define EVENT_MY_FEATURE  (EVENT_USER_BASE + 0) */
/* #define EVENT_MY_TIMER    (EVENT_USER_BASE + 1) */
/* 框架只搬运事件 ID, 不解释其含义. */
/* -------------------------------------------------------------------------- */
#define EVENT_USER_BASE 0x1000u

    struct event
    {
        uint32_t id; /**< 事件 ID (框架级或用户定义) */
        uintptr_t arg; /**< 事件参数 (指针或整数值) */
    };

    /* 事件回调类型 */
    typedef void (*event_callback_t)(const struct event* event, void* user_data);

    /* -------------------------------------------------------------------------- */
    /* EventBus C API (统一返回 MINI_OK / MINI_ERR_* 错误码) */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief 初始化事件总线 (创建订阅表, 启动前调用)
     * @return MINI_OK 成功; MINI_ERR_NOMEM 资源不足
     */
    int event_bus_init(void) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 订阅 [id_min, id_max] 闭区间内的事件
     * @param[in] id_min 订阅区间下界
     * @param[in] id_max 订阅区间上界
     * @param[in] callback 事件回调 (task 上下文调用)
     * @param[in] user_data 回调私有数据
     * @return MINI_OK 成功; MINI_ERR_ISR 中断上下文调用; MINI_ERR_NOTSUPP 已封表;
     *         MINI_ERR_INVAL 参数非法/未初始化; MINI_ERR_TIMEOUT 锁超时; MINI_ERR_NOSPC 槽位已满
     */
    int event_bus_subscribe(uint32_t id_min, uint32_t id_max, event_callback_t callback,
                            void* user_data) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 发布事件 (task 上下文)
     * @param[in] id 事件 ID (框架级或用户自定义)
     * @param[in] arg 事件参数 (指针或整数值)
     * @return MINI_OK 成功; MINI_ERR_ISR 中断上下文调用; MINI_ERR_AGAIN 总线未就绪; MINI_ERR_NOSPC
     * 队列满
     */
    int event_bus_post(uint32_t id, uintptr_t arg) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 发布事件 (ISR 上下文)
     * @param[in] id 事件 ID
     * @param[in] arg 事件参数
     * @param[out] px_yield_required ISR 内是否需要请求上下文切换
     * @return MINI_OK 成功; MINI_ERR_AGAIN 总线未就绪; MINI_ERR_NOSPC 队列满
     */
    int event_bus_post_from_isr(uint32_t id, uintptr_t arg,
                                bool* px_yield_required) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 启动事件分发 (创建分发任务)
     */
    void event_bus_start(void);
    /**
     * @brief 停止事件分发
     */
    void event_bus_stop(void);
    /**
     * @brief 封存总线: 禁止后续订阅 (启动后调用, 冻结订阅表)
     */
    void event_bus_seal(void);
    /**
     * @brief 查询因队列满而丢弃的事件数
     * @return 累计丢弃事件数
     */
    size_t event_bus_dropped_count(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */
