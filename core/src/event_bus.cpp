#include "event_bus.hpp"
#include "safe_state.h"
#include "system_log.hpp"
#include "system_wdt.hpp"
#include "compiler_compat.h"

/* SIOF (Static Initialization Order Fiasco) 防御:
 *   在 System_Pre_OS_Init (Phase 1) 完成前, 禁止所有 EventBus 操作.
 *   防止 C++ 全局构造函数在 main() 之前偷跑调用 post/subscribe.
 *   定义位于 system_init.cpp / system_init.c. */
extern bool g_system_os_initialized;

static constexpr const char* kTag = "EventBus";
static constexpr uint32_t kDispatchPrio =
#if defined(CONFIG_OSAL_FREERTOS)
    30;  /* FreeRTOS: 0=最低, 31=最高 */
#else
    1;   /* RT-Thread: 0=最高, 31=最低 */
#endif
static constexpr uint32_t kDispatchStack = 2048;
static constexpr uint32_t kStopWaitMs = 500;

EventBus::EventBus() = default;

bool EventBus::init()
{
    if (m_inited) return true;

    m_queue = osal_queue_create(kQueueLen, sizeof(Event));
    if (m_queue == nullptr)
    {
        SYS_LOGE(kTag, "FATAL: osal_queue_create failed — event bus unusable");
        return false;
    }

    osal_mutex_create_static(&m_sub_lock, m_sub_lock_storage, sizeof(m_sub_lock_storage));
    if (m_sub_lock == nullptr)
    {
        SYS_LOGE(kTag, "FATAL: mutex create failed");
        osal_queue_delete(m_queue);
        m_queue = nullptr;
        return false;
    }

    m_inited = true;
    SYS_LOGI(kTag, "event bus initialized, queue=%u slots", (unsigned)kQueueLen);
    return true;
}

EventBus& EventBus::getInstance()
{
    static EventBus bus;
    return bus;
}

bool EventBus::subscribe(uint32_t id_min, uint32_t id_max,
                         EventCallback callback, void* user_data)
{
    if (osal_in_isr()) return false;
    if (m_is_sealed) return false;
    if (callback == nullptr || m_sub_lock == nullptr || id_min > id_max)
    {
        return false;
    }

    if (osal_mutex_lock(m_sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != 0)
    {
        SYS_LOGE(kTag, "Fatal: EventBus subscribe lock timeout (possible deadlock)");
        return false;
    }

    bool ok = false;
    if (m_count < kMaxSubscribers)
    {
        m_subscribers[m_count].id_min = id_min;
        m_subscribers[m_count].id_max = id_max;
        m_subscribers[m_count].callback = callback;
        m_subscribers[m_count].user_data = user_data;
        m_count++;
        ok = true;
    }

    osal_mutex_unlock(m_sub_lock);
    return ok;
}

bool EventBus::post(uint32_t id, uintptr_t arg)
{
    if (m_queue == nullptr)
    {
        return false;
    }

    /* SIOF 防御: 系统未初始化前静默丢弃 */
    if (!g_system_os_initialized)
    {
        return false;
    }

    const Event event = {id, arg};

    /* osal_queue_send 内部自动嗅探 ISR 上下文, 无需调用者区分 */
    bool ok = osal_queue_send(m_queue, &event, 0);

    if (!ok)
    {
        __atomic_add_fetch(&m_dropped, 1, __ATOMIC_RELAXED);
        if (!osal_in_isr())
        {
            size_t cur = __atomic_load_n(&m_dropped, __ATOMIC_RELAXED);
            if ((cur % 8) == 0 && cur != 0)
            {
                SYS_LOGW(kTag, "event queue full, dropped=%u", (unsigned)cur);
            }
        }
        return false;
    }
    return true;
}

size_t EventBus::dropped_count() const
{
    return __atomic_load_n(&m_dropped, __ATOMIC_RELAXED);
}

void EventBus::dispatch_task(void* param)
{
    if (!param) return;
    EventBus* self = static_cast<EventBus*>(param);
    Event event;

    while (osal_queue_receive(self->m_queue, &event, OSAL_WAIT_FOREVER))
    {
        if (self->m_task == nullptr)
        {
            break;
        }

        system_wdt_feed();
        system_wdt_feed_rtc();

        Subscriber snapshot[kMaxSubscribers];
        size_t snapshot_count = 0;

        if (self->m_sub_lock)
        {
            if (osal_mutex_lock(self->m_sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != 0)
            {
                SYS_LOGE(kTag, "Fatal: EventBus dispatch lock timeout — safe shutdown");
                enter_safe_state("EventBus mutex deadlock");
                break;
            }
        }
        snapshot_count = self->m_count;
        for (size_t i = 0; i < snapshot_count; i++)
        {
            snapshot[i] = self->m_subscribers[i];
        }
        if (self->m_sub_lock)
        {
            osal_mutex_unlock(self->m_sub_lock);
        }

        for (size_t i = 0; i < snapshot_count; i++)
        {
            Subscriber& sub = snapshot[i];
            if (sub.callback != nullptr &&
                event.id >= sub.id_min && event.id <= sub.id_max)
            {
                sub.callback(event, sub.user_data);
            }
        }
    }

    SYS_LOGI(kTag, "dispatch task exiting");
    osal_task_self_delete();
}

void EventBus::start()
{
    if (m_task != nullptr || m_queue == nullptr) return;

    osal_task_create_handle("evt_bus", kDispatchStack, kDispatchPrio,
                            dispatch_task, this, 0, &m_task);
    system_wdt_subscribe(m_task);
    SYS_LOGI(kTag, "dispatch task started prio %u", kDispatchPrio);
}

void EventBus::stop()
{
    if (!m_task) return;

    osal_task_handle_t handle = m_task;
    m_task = nullptr;

    /* 向队列发空事件唤醒 dispatch 线程 */
    Event dummy = {EVENT_SYS_FAULT, 0};
    osal_queue_send(m_queue, &dummy, 0);

    uint32_t waited = 0;
    while (osal_task_is_running(handle) && waited < kStopWaitMs)
    {
        osal_delay_ms(10);
        waited += 10;
    }

    if (osal_task_is_running(handle))
    {
        SYS_LOGW(kTag, "dispatch task did not exit, force deleting");
        osal_task_delete(handle);
    }

    /* 销毁队列，完整逆操作 init() */
    osal_queue_delete(m_queue);
    m_queue = nullptr;
    m_inited = false;
}

void EventBus::seal()
{
    m_is_sealed = true;
}

/* ── C 兼容封装 ── */
extern "C" bool event_bus_init(void)
{
    return EventBus::getInstance().init();
}

extern "C" bool event_bus_post(uint32_t id, uintptr_t arg)
{
    return EventBus::getInstance().post(id, arg);
}

extern "C" void event_bus_start(void)
{
    EventBus::getInstance().start();
}

extern "C" void event_bus_seal(void)
{
    EventBus::getInstance().seal();
}
