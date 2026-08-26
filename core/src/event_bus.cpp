/* SPDX-License-Identifier: Apache-2.0 */
/*
 * EventBus C++ 实现 — 事件分发任务与订阅管理
 *
 * 单例 + SIOF 防御 (OS 未初始化前拒绝操作); dispatch 在快照副本上回调不持锁
 * 提供 extern "C" 包装, 供 .c 文件调用同一总线实例
 */
#include "event_bus.hpp"

#include "compiler_compat.h"
#include "safe_state.h"
#include "system_log.h"
#include "system_wdt.hpp"

#include "compiler_compat_poison.h"

/* SIOF (Static Initialization Order Fiasco) 防御:
 *   在 system_pre_os_init (Phase 1) 完成前, 禁止所有 EventBus 操作.
 *   防止 C++ 全局构造函数在 main() 之前偷跑调用 post/subscribe.
 *   定义位于 system_init.cpp / system_init.c. */
extern volatile bool g_system_os_initialized;

static constexpr const char* k_tag = "EventBus";
static constexpr uint32_t kDispatchPrio =
#if defined(CONFIG_OSAL_FREERTOS)
    30; /* FreeRTOS: 0=最低, 31=最高 */
#else
    1; /* RT-Thread: 0=最高, 31=最低 */
#endif
static constexpr uint32_t kDispatchStack = 2048;
static constexpr uint32_t kStopWaitMs = 500;

EventBus::EventBus() = default;

int EventBus::init()
{
    if (m_inited)
        return MINI_OK;

    m_queue = osal_queue_create(k_queue_len, sizeof(event));
    if (m_queue == nullptr)
    {
        SYS_LOGE(k_tag, "FATAL: osal_queue_create failed — event bus unusable");
        return MINI_ERR_NOMEM;
    }

    if (osal_mutex_create_static(&m_sub_lock, m_sub_lock_storage, sizeof(m_sub_lock_storage)) !=
            OSAL_OK ||
        m_sub_lock == nullptr)
    {
        SYS_LOGE(k_tag, "FATAL: mutex create failed");
        osal_queue_delete(m_queue);
        m_queue = nullptr;
        return MINI_ERR_NOMEM;
    }

    m_inited = true;
    SYS_LOGI(k_tag, "event bus initialized, queue=%u slots", (unsigned)k_queue_len);
    return MINI_OK;
}

EventBus& EventBus::get_instance()
{
    static EventBus bus;
    return bus;
}

int EventBus::subscribe(uint32_t id_min, uint32_t id_max, EventCallback callback, void* user_data)
{
    if (osal_in_isr())
        return MINI_ERR_ISR;
    if (m_is_sealed)
        return MINI_ERR_NOTSUPP;
    if (callback == nullptr || m_sub_lock == nullptr || id_min > id_max)
        return MINI_ERR_INVAL;

    if (osal_mutex_lock(m_sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != OSAL_OK)
    {
        SYS_LOGE(k_tag, "Fatal: EventBus subscribe lock timeout (possible deadlock)");
        return MINI_ERR_TIMEOUT;
    }

    int ret = MINI_ERR_NOSPC;
    if (m_count < k_max_subscribers)
    {
        m_subscribers[m_count].id_min = id_min;
        m_subscribers[m_count].id_max = id_max;
        m_subscribers[m_count].callback = callback;
        m_subscribers[m_count].user_data = user_data;
        m_count++;
        ret = MINI_OK;
    }

    osal_mutex_unlock(m_sub_lock);
    return ret;
}

int EventBus::post(uint32_t id, uintptr_t arg)
{
    if (osal_in_isr())
        return MINI_ERR_ISR;

    return post_internal(id, arg, false, nullptr);
}

int EventBus::post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required)
{
    return post_internal(id, arg, true, px_yield_required);
}

int EventBus::post_internal(uint32_t id, uintptr_t arg, bool from_isr, bool* px_yield_required)
{
    if (m_queue == nullptr || !m_inited)
        return MINI_ERR_AGAIN;

    if (!g_system_os_initialized)
        return MINI_ERR_AGAIN;

    const event event = {id, arg};
    bool ok;

    if (from_isr)
        ok = osal_queue_send_from_isr(m_queue, &event, px_yield_required);
    else
        ok = osal_queue_send(m_queue, &event, 0);

    if (!ok)
    {
        __atomic_add_fetch(&m_dropped, 1, __ATOMIC_RELAXED);
        if (!from_isr)
        {
            size_t cur = __atomic_load_n(&m_dropped, __ATOMIC_RELAXED);
            if ((cur % 8) == 0 && cur != 0)
                SYS_LOGW(k_tag, "event queue full, dropped=%u", (unsigned)cur);
        }
        return MINI_ERR_NOSPC;
    }
    return MINI_OK;
}

size_t EventBus::dropped_count() const { return __atomic_load_n(&m_dropped, __ATOMIC_RELAXED); }

void EventBus::dispatch_task(void* param)
{
    if (!param)
        return;
    EventBus* self = static_cast<EventBus*>(param);
    event event;

    while (osal_queue_receive(self->m_queue, &event, OSAL_WAIT_FOREVER))
    {
        if (self->m_task == nullptr)
            break;

        system_wdt_feed();
        system_wdt_feed_iwdg();

        subscriber snapshot[k_max_subscribers];
        size_t snapshot_count = 0;

        if (self->m_sub_lock)
        {
            if (osal_mutex_lock(self->m_sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != OSAL_OK)
            {
                SYS_LOGE(k_tag, "Fatal: EventBus dispatch lock timeout — safe shutdown");
                enter_safe_state("EventBus mutex deadlock");
                break;
            }
        }
        snapshot_count = self->m_count;
        for (size_t index = 0; index < snapshot_count; index++)
            snapshot[index] = self->m_subscribers[index];
        if (self->m_sub_lock)
            osal_mutex_unlock(self->m_sub_lock);

        for (size_t index = 0; index < snapshot_count; index++)
        {
            subscriber& sub = snapshot[index];
            if (sub.callback != nullptr && event.id >= sub.id_min && event.id <= sub.id_max)
                sub.callback(event, sub.user_data);
        }
    }

    SYS_LOGI(k_tag, "dispatch task exiting");
    osal_task_self_delete();
}

void EventBus::start()
{
    if (m_task != nullptr || m_queue == nullptr)
        return;

    if (osal_task_create_handle("evt_bus", kDispatchStack, kDispatchPrio, dispatch_task, this, 0, &m_task) != MINI_OK)
    {
        SYS_LOGE(k_tag, "FATAL: osal_task_create_handle failed — event bus unusable");
        return;
    }
    MINI_IGNORE_RESULT(system_wdt_subscribe(m_task));
    SYS_LOGI(k_tag, "dispatch task started prio %lu", (unsigned long)kDispatchPrio);
}

void EventBus::stop()
{
    if (!m_task)
        return;

    osal_task_handle_t handle = m_task;
    m_task = nullptr;

    /* 向队列发空事件唤醒 dispatch 线程 (osal_queue_send 返回 bool) */
    event dummy = {EVENT_SYS_FAULT, 0};
    if (!osal_queue_send(m_queue, &dummy, 0))
    {
        SYS_LOGE(k_tag, "FATAL: osal_queue_send failed — event bus unusable");
        return;
    }

    uint32_t waited = 0;
    while (osal_task_is_running(handle) && waited < kStopWaitMs)
    {
        osal_delay_ms(10);
        waited += 10;
    }

    if (osal_task_is_running(handle))
    {
        SYS_LOGW(k_tag, "dispatch task did not exit, force deleting");
        osal_task_delete(handle);
    }

    /* 先标记未初始化，阻止新的 post，再销毁队列 */
    m_inited = false;
    osal_queue_delete(m_queue);
    m_queue = nullptr;
}

void EventBus::seal() { m_is_sealed = true; }

/* -------------------------------------------------------------------------- */
/* C 接口 (extern "C") */
/* -------------------------------------------------------------------------- */
extern "C" int event_bus_init(void) { return EventBus::get_instance().init(); }

extern "C" int event_bus_post(uint32_t id, uintptr_t arg)
{
    return EventBus::get_instance().post(id, arg);
}

extern "C" int event_bus_post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required)
{
    return EventBus::get_instance().post_from_isr(id, arg, px_yield_required);
}

extern "C" void event_bus_start(void) { EventBus::get_instance().start(); }

extern "C" void event_bus_seal(void) { EventBus::get_instance().seal(); }