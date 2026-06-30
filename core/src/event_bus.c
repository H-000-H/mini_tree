/* SPDX-License-Identifier: Apache-2.0 */
/*
 * event_bus.c — 轻量事件通知总线
 *
 * 设计约束:
 *   - 单例, 纯 C 实现, 无全局构造函数/析构函数 (SIOF 安全)
 *   - 发布-订阅模式, 单个分派任务(FIFO 队列)
 *   - 封表后 ISR 可安全 post (遍历只读快照副本)
 *   - 回调中不得阻塞 I/O 或长时间计算
 */
#include "event_bus.h"
#include "safe_state.h"
#include "system_wdt.hpp"
#include "system_log.h"
#include "compiler_compat.h"
#include "config.h"
#include "compiler_compat_poison.h"

/* SIOF (Static Initialization Order Fiasco) 防御:
 *   在 System_Pre_OS_Init (Phase 1) 完成前, 禁止所有 EventBus 操作.
 *   防止 C++ 全局构造函数在 main() 之前偷跑调用 post/subscribe.
 *   定义位于 system_init.c / system_init.cpp. */
extern bool g_system_os_initialized;

#define K_TAG               "EventBus"
#define K_QUEUE_LEN         CONFIG_EVENT_BUS_QUEUE_LEN
#define K_MAX_SUBSCRIBERS   CONFIG_EVENT_BUS_MAX_SUBSCRIBERS

#if defined(CONFIG_OSAL_FREERTOS)
#  define K_DISPATCH_PRIO   30   /* FreeRTOS: 0=最低, 31=最高 */
#else
#  define K_DISPATCH_PRIO   1    /* RT-Thread: 0=最高, 31=最低 */
#endif

#define K_DISPATCH_STACK    CONFIG_EVENT_BUS_DISPATCH_STACK
#define K_STOP_WAIT_MS      500

/* ── 内部数据结构 ── */
struct subscriber
{
    uint32_t          id_min;
    uint32_t          id_max;
    event_callback_t  callback;
    void*             user_data;
};

struct event_bus
{
    struct subscriber   subscribers[K_MAX_SUBSCRIBERS];
    size_t              count;
    bool                inited;
    bool                is_sealed;

    osal_queue_handle_t queue;
    void*               task;
    size_t              dropped;

    struct osal_mutex*       sub_lock;
    uint8_t             sub_lock_storage[OSAL_MUTEX_STORAGE_SIZE];
};

/* ── 内部静态单例 ── */
static struct event_bus s_bus = {0};

/* ── 分派任务 (静态函数, 仅内部使用) ── */
static void event_bus_dispatch_task(void* param)
{
    (void)param;
    struct Event event;

    while (osal_queue_receive(s_bus.queue, &event, OSAL_WAIT_FOREVER))
    {
        if (s_bus.task == NULL)
        {
            break;
        }

        system_wdt_feed();
        system_wdt_feed_rtc();

        /* 快照订阅者表 — 不持锁执行回调, 避免优先级反转锁死 */
        struct subscriber snapshot[K_MAX_SUBSCRIBERS];
        size_t snapshot_count = 0;

        if (s_bus.sub_lock)
        {
            if (osal_mutex_lock(s_bus.sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != 0)
            {
                SYS_LOGE(K_TAG, "Fatal: EventBus dispatch lock timeout — safe shutdown");
                enter_safe_state("EventBus mutex deadlock");
                break;
            }
        }
        snapshot_count = s_bus.count;
        for (size_t i = 0; i < snapshot_count; i++)
        {
            snapshot[i] = s_bus.subscribers[i];
        }
        if (s_bus.sub_lock)
        {
            osal_mutex_unlock(s_bus.sub_lock);
        }

        for (size_t i = 0; i < snapshot_count; i++)
        {
            struct subscriber* sub = &snapshot[i];
            if (sub->callback != NULL &&
                event.id >= sub->id_min && event.id <= sub->id_max)
            {
                sub->callback(&event, sub->user_data);
            }
        }
    }

    SYS_LOGI(K_TAG, "dispatch task exiting");
    osal_task_self_delete();
}

/* ── 公开 API ── */

bool event_bus_init(void)
{
    if (s_bus.inited) return true;

    s_bus.queue = osal_queue_create(K_QUEUE_LEN, sizeof(struct Event));
    if (s_bus.queue == NULL)
    {
        SYS_LOGE(K_TAG, "FATAL: osal_queue_create failed — event bus unusable");
        return false;
    }

    if (osal_mutex_create_static(&s_bus.sub_lock,
                                 s_bus.sub_lock_storage,
                                 sizeof(s_bus.sub_lock_storage)) != 0
        || s_bus.sub_lock == NULL)
    {
        SYS_LOGE(K_TAG, "FATAL: mutex create failed");
        osal_queue_delete(s_bus.queue);
        s_bus.queue = NULL;
        return false;
    }

    s_bus.inited = true;
    SYS_LOGI(K_TAG, "event bus initialized, queue=%u slots", (unsigned)K_QUEUE_LEN);
    return true;
}

bool event_bus_subscribe(uint32_t id_min, uint32_t id_max,
                         event_callback_t callback, void* user_data)
{
    if (osal_in_isr())              return false;
    if (s_bus.is_sealed)            return false;
    if (callback == NULL)           return false;
    if (s_bus.sub_lock == NULL)     return false;
    if (id_min > id_max)            return false;

    if (osal_mutex_lock(s_bus.sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != 0)
    {
        SYS_LOGE(K_TAG, "Fatal: EventBus subscribe lock timeout (possible deadlock)");
        return false;
    }

    bool ok = false;
    if (s_bus.count < K_MAX_SUBSCRIBERS)
    {
        s_bus.subscribers[s_bus.count].id_min    = id_min;
        s_bus.subscribers[s_bus.count].id_max    = id_max;
        s_bus.subscribers[s_bus.count].callback  = callback;
        s_bus.subscribers[s_bus.count].user_data = user_data;
        s_bus.count++;
        ok = true;
    }

    osal_mutex_unlock(s_bus.sub_lock);
    return ok;
}

static bool event_bus_post_internal(uint32_t id, uintptr_t arg, bool from_isr,
                                    bool* px_yield_required)
{
    if (s_bus.queue == NULL)
    {
        return false;
    }

    if (!g_system_os_initialized)
    {
        return false;
    }

    const struct Event event = {id, arg};
    bool ok;

    if (from_isr)
        ok = osal_queue_send_from_isr(s_bus.queue, &event, px_yield_required);
    else
        ok = osal_queue_send(s_bus.queue, &event, 0);

    if (!ok)
    {
        __atomic_add_fetch(&s_bus.dropped, 1, __ATOMIC_RELAXED);
        if (!from_isr)
        {
            size_t cur = __atomic_load_n(&s_bus.dropped, __ATOMIC_RELAXED);
            if ((cur % 8) == 0 && cur != 0)
            {
                SYS_LOGW(K_TAG, "event queue full, dropped=%u", (unsigned)cur);
            }
        }
        return false;
    }
    return true;
}

bool event_bus_post(uint32_t id, uintptr_t arg)
{
    if (osal_in_isr())
        return false;

    return event_bus_post_internal(id, arg, false, NULL);
}

bool event_bus_post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required)
{
    return event_bus_post_internal(id, arg, true, px_yield_required);
}

size_t event_bus_dropped_count(void)
{
    return __atomic_load_n(&s_bus.dropped, __ATOMIC_RELAXED);
}

void event_bus_start(void)
{
    if (s_bus.task != NULL || s_bus.queue == NULL) return;

    if (osal_task_create_handle("evt_bus", K_DISPATCH_STACK, K_DISPATCH_PRIO,
                                event_bus_dispatch_task, NULL, 0, &s_bus.task) != 0
        || s_bus.task == NULL)
    {
        SYS_LOGW(K_TAG, "dispatch task create failed");
        return;
    }
    system_wdt_subscribe(s_bus.task);
    SYS_LOGI(K_TAG, "dispatch task started prio %lu", (unsigned long)K_DISPATCH_PRIO);
}

void event_bus_stop(void)
{
    if (!s_bus.task) return;

    void* handle = s_bus.task;
    s_bus.task = NULL;

    /* 向队列发空事件唤醒 dispatch 线程 */
    const struct Event dummy = {EVENT_SYS_FAULT, 0};
    COMPAT_IGNORE_RESULT(osal_queue_send(s_bus.queue, &dummy, 0));

    uint32_t waited = 0;
    while (osal_task_is_running(handle) && waited < K_STOP_WAIT_MS)
    {
        osal_delay_ms(10);
        waited += 10;
    }

    if (osal_task_is_running(handle))
    {
        SYS_LOGW(K_TAG, "dispatch task did not exit, force deleting");
        osal_task_delete(handle);
    }

    /* 销毁队列, 完整逆操作 init() */
    osal_queue_delete(s_bus.queue);
    s_bus.queue = NULL;
    s_bus.inited = false;
}

void event_bus_seal(void)
{
    s_bus.is_sealed = true;
}
