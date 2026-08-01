/* SPDX-License-Identifier: Apache-2.0 */
/*
 * EventBus C++ 单例 — 事件分发任务与订阅管理
 *
 * 单例模式, dispatch 任务为框架内最高优先级, 确保事件队列快速排空
 * 回调在快照副本上执行不持锁, 避免优先级反转; seal() 后禁止动态订阅
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "osal.h"

/* ── 框架级事件 ID (框架内部使用, 不涉及任何业务语义) ── */
#define EVENT_SYS_BOOT   0x0000     /* 系统冷启动完成 */
#define EVENT_SYS_READY  0x0001     /* 所有框架任务已就绪 */
#define EVENT_SYS_FAULT  0x0002     /* 系统级故障, 进入安全状态 */
#define EVENT_SYS_DEVICE_REMOVED 0x0003  /* 设备从设备树中移除 */

/* ── 用户事件基线 ──
 * 用户工程在业务代码中基于此值定义自有事件:
 *   #define EVENT_MY_FEATURE  (EVENT_USER_BASE + 0)
 *   #define EVENT_MY_TIMER    (EVENT_USER_BASE + 1)
 * 框架只搬运事件 ID, 不解释其含义.
 */
#define EVENT_USER_BASE  0x1000u

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 事件对象 — 事件总线传输的基本单元 */
struct event
{
    uint32_t id;            /**< 事件 ID (框架级或用户定义) */
    uintptr_t arg;          /**< 事件附带参数 (由发送者/接收者自行解释) */
};

/* ── C 接口 (extern "C", 供 .c 文件调用) ── */
bool event_bus_init(void);            /**< 初始化事件总线 (队列 + 互斥锁) */
bool event_bus_post(uint32_t id, uintptr_t arg);  /**< 发布事件 (任务上下文) */
bool event_bus_post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required);  /**< 发布事件 (ISR 上下文) */
void event_bus_start(void);           /**< 启动事件分发任务 */
void event_bus_seal(void);            /**< 封表: 禁止运行时动态订阅 */

#ifdef __cplusplus
}

/* ── C++ 事件回调类型 ── */
using EventCallback = void (*)(const event& event, void* user_data);  /**< 事件回调函数指针 */

/**
 * @brief 事件总线 C++ 单例 — 事件分发与订阅管理
 *
 * dispatch 任务为框架内最高优先级, 确保事件队列快速排空;
 * 回调在快照副本上执行不持锁, 避免优先级反转;
 * seal() 后禁止动态订阅
 */
class EventBus
{
public:
    static EventBus& get_instance();  /**< 获取单例引用 */

    bool init();  /**< 初始化事件总线 (队列 + 互斥锁 + 订阅表) */

    /** 订阅事件范围 [id_min, id_max] (含两端).
     *  单事件订阅: subscribe(id, id, cb, ud) */
    bool subscribe(uint32_t id_min, uint32_t id_max,
                   EventCallback callback, void* user_data = nullptr);

    bool post(uint32_t id, uintptr_t arg = 0);  /**< 发布事件 (任务上下文) */
    bool post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required);  /**< 发布事件 (ISR 上下文) */
    size_t dropped_count() const;  /**< 累计丢弃事件数 (队列溢出) */
    /** 启动事件分发任务.
     *  分发任务优先级: FreeRTOS 后端 = 30, RT-Thread 后端 = 1.
     *  在两套后端语义下均为框架内最高任务优先级, 确保事件队列快速排空.
     *  订阅者回调在快照副本上执行, 不持互斥锁, 不存在优先级反转阻塞 EventBus 的场景.
     *
     *  WARNING — 回调约束:
     *  EventBus 是轻量通知总线, 回调在 dispatch 任务上下文中同步执行.
     *  一个回调卡住会阻塞后续所有事件的分发. 因此回调中不得:
     *    - 执行阻塞 I/O (SPI 传输、Flash 擦写等)
     *    - 执行长时间计算或忙等
     *    - 调用 osal_delay_ms 或任何阻塞操作
     *  长时间工作应转发到用户专用任务 (设置标志位、发信号量、入工作队列). */
    void start();   /**< 启动事件分发任务 */
    void stop();    /**< 停止事件分发任务 */

    /** 封表: 禁止运行时动态订阅.
     *  在 system_start_tasks (Phase 2) 末尾调用, 此后 subscribe() 全部失败.
     *  确保 ISR 中 post_from_isr() 遍历的订阅者数组是只读静态表, 绝无读写踩踏. */
    void seal();    /**< 封表: 禁止运行时动态订阅 */

private:
    EventBus();                              /**< 私有构造 (单例) */
    EventBus(const EventBus&) = delete;      /**< 禁止拷贝 */
    EventBus& operator=(const EventBus&) = delete;  /**< 禁止赋值 */

    /** @brief 订阅者条目 — 记录事件范围与回调 */
    struct subscriber
    {
        uint32_t id_min = EVENT_SYS_BOOT;       /**< 订阅事件范围下界 */
        uint32_t id_max = EVENT_SYS_BOOT;       /**< 订阅事件范围上界 */
        EventCallback callback = nullptr;       /**< 事件回调函数 */
        void* user_data = nullptr;              /**< 回调用户数据 */
    };

    static constexpr size_t k_max_subscribers = 24;  /**< 最大订阅者数 */
    static constexpr size_t k_queue_len = 64;        /**< 事件队列深度 */

    subscriber m_subscribers[k_max_subscribers] = {};  /**< 订阅者数组 */
    size_t m_count = 0;           /**< 当前订阅者数 */
    bool m_inited = false;        /**< 是否已初始化 */
    bool m_is_sealed = false;     /**< 是否已封表 */

    osal_queue_handle_t m_queue = nullptr;  /**< 事件队列句柄 */
    void* m_task = nullptr;                 /**< 分发任务句柄 */
    size_t m_dropped = 0;                   /**< 累计丢弃事件计数 */

    struct osal_mutex* m_sub_lock = nullptr;  /**< 订阅表互斥锁 */
    uint8_t m_sub_lock_storage[OSAL_MUTEX_STORAGE_SIZE];  /**< 互斥锁存储区 */

    static void dispatch_task(void* param);  /**< 分发任务入口 (静态) */

    /** @brief 内部发布实现 (任务/ISR 共用) */
    bool post_internal(uint32_t id, uintptr_t arg, bool from_isr,
                       bool* px_yield_required);
};

#endif /* __cplusplus */
