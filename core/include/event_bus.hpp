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
 * 宿主工程在业务代码中基于此值定义自有事件:
 *   #define EVENT_MY_FEATURE  (EVENT_USER_BASE + 0)
 *   #define EVENT_MY_TIMER    (EVENT_USER_BASE + 1)
 * 框架只搬运事件 ID, 不解释其含义.
 */
#define EVENT_USER_BASE  0x1000u

#ifdef __cplusplus
extern "C" {
#endif

struct Event
{
    uint32_t id;            /* 事件 ID (框架级或用户定义) */
    uintptr_t arg;
};

/* ── C 兼容事件投递 (供 .c 文件使用) ── */
bool event_bus_init(void);
bool event_bus_post(uint32_t id, uintptr_t arg);
void event_bus_start(void);
void event_bus_seal(void);

#ifdef __cplusplus
}

/* ── C++ 事件回调类型 ── */
using EventCallback = void (*)(const Event& event, void* user_data);

class EventBus
{
public:
    static EventBus& getInstance();

    bool init();

    /** 订阅事件范围 [id_min, id_max] (含两端).
     *  单事件订阅: subscribe(id, id, cb, ud) */
    bool subscribe(uint32_t id_min, uint32_t id_max,
                   EventCallback callback, void* user_data = nullptr);

    bool post(uint32_t id, uintptr_t arg = 0);
    size_t dropped_count() const;
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
     *  长时间工作应转发到宿主专用任务 (设置标志位、发信号量、入工作队列). */
    void start();
    void stop();

    /** 封表: 禁止运行时动态订阅.
     *  在 System_Start_Tasks (Phase 2) 末尾调用, 此后 subscribe() 全部失败.
     *  确保 ISR 中 post() 遍历的订阅者数组是只读静态表, 绝无读写踩踏. */
    void seal();

private:
    EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    struct Subscriber
    {
        uint32_t id_min = EVENT_SYS_BOOT;
        uint32_t id_max = EVENT_SYS_BOOT;
        EventCallback callback = nullptr;
        void* user_data = nullptr;
    };

    static constexpr size_t kMaxSubscribers = 24;
    static constexpr size_t kQueueLen = 64;

    Subscriber m_subscribers[kMaxSubscribers] = {};
    size_t m_count = 0;
    bool m_inited = false;
    bool m_is_sealed = false;

    osal_queue_handle_t m_queue = nullptr;
    void* m_task = nullptr;
    size_t m_dropped = 0;

    osal_mutex_t* m_sub_lock = nullptr;
    uint8_t m_sub_lock_storage[OSAL_MUTEX_STORAGE_SIZE];

    static void dispatch_task(void* param);
};

#endif /* __cplusplus */
