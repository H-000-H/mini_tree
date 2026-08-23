/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file event_bus.c
 *@brief event bus 实现
 *@author H-000-H
 *@details
 *   event_bus.c — 轻量事件通知总线
 *   设计约束:
 *   - 单例, 纯 C 实现, 无全局构造函数/析构函数 (SIOF 安全)
 *   - 发布-订阅模式, 单个分派任务(FIFO 队列)
 *   - 封表后 ISR 可安全 post (遍历只读快照副本)
 *   - 回调中不得阻塞 I/O 或长时间计算
 */

#include "event_bus.h"

#include "compiler_compat.h"
#include "config.h"
#include "safe_state.h"
#include "system_log.h"
#include "system_wdt.hpp"

#include "compiler_compat_poison.h"

/* SIOF (Static Initialization Order Fiasco) 防御:
 *   在 system_pre_os_init (Phase 1) 完成前, 禁止所有 EventBus 操作.
 *   防止 C++ 全局构造函数在 main() 之前偷跑调用 post/subscribe.
 *   定义位于 system_init.c / system_init.cpp. */
extern volatile bool g_system_os_initialized;

#define K_TAG "EventBus"
#define K_QUEUE_LEN CONFIG_EVENT_BUS_QUEUE_LEN
#define K_MAX_SUBSCRIBERS CONFIG_EVENT_BUS_MAX_SUBSCRIBERS

#if defined(CONFIG_OSAL_FREERTOS)
/* FreeRTOS: 0=最低, configMAX_PRIORITIES-1=最高；ESP-IDF 默认 MAX=24 → 合法 0..24 */
#define K_DISPATCH_PRIO 24
#else
#define K_DISPATCH_PRIO 1 /* RT-Thread: 0=最高, 数值越小越高 */
#endif

#define K_DISPATCH_STACK CONFIG_EVENT_BUS_DISPATCH_STACK
#define K_STOP_WAIT_MS 500

/* ── 内部数据结构 ── */
struct subscriber
{
    uint32_t id_min; /**< 订阅起始事件 ID */
    uint32_t id_max; /**< 订阅结束事件 ID */
    event_callback_t callback; /**< 事件回调函数 */
    void* user_data; /**< 用户私有数据 */
};

struct event_bus
{
    struct subscriber subscribers[K_MAX_SUBSCRIBERS]; /**< 订阅者表 */
    size_t count; /**< 当前订阅者数量 */
    bool inited; /**< 是否已初始化 */
    bool is_sealed; /**< 是否已封禁 (不再接受新订阅) */

    osal_queue_handle_t queue; /**< 事件队列 */
    void* task; /**< 分派任务句柄 */
    size_t dropped; /**< 丢弃事件计数 */

    struct osal_mutex* sub_lock; /**< 订阅者表锁 */
    uint8_t sub_lock_storage[OSAL_MUTEX_STORAGE_SIZE]; /**< 锁存储 */
};

/* ── 内部静态单例 ── */
static struct event_bus s_bus = {0};

/* ── 分派任务 (静态函数, 仅内部使用) ── */
/**
 * @brief EventBus 后台分派任务: 从队列取事件并回调匹配订阅者
 * @param[in] param OSAL 任务入口参数 (未使用)
 */
static void event_bus_dispatch_task(void* param)
{
    COMPAT_UNUSED_PARAM(param);
    struct event event;

    while (osal_queue_receive(s_bus.queue, &event, OSAL_WAIT_FOREVER))
    {
        if (s_bus.task == NULL)
            break;

        system_wdt_feed();
        system_wdt_feed_iwdg();

        /* 快照订阅者表 — 不持锁执行回调, 避免优先级反转锁死 */
        struct subscriber snapshot[K_MAX_SUBSCRIBERS];
        size_t snapshot_count = 0;

        if (s_bus.sub_lock)
        {
            if (osal_mutex_lock(s_bus.sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != OSAL_OK)
            {
                SYS_LOGE(K_TAG, "Fatal: EventBus dispatch lock timeout — safe shutdown");
                enter_safe_state("EventBus mutex deadlock");
                break;
            }
        }
        snapshot_count = s_bus.count;
        for (size_t i = 0; i < snapshot_count; i++)
            snapshot[i] = s_bus.subscribers[i];
        if (s_bus.sub_lock)
            osal_mutex_unlock(s_bus.sub_lock);

        for (size_t i = 0; i < snapshot_count; i++)
        {
            struct subscriber* sub = &snapshot[i];
            if (sub->callback != NULL && event.id >= sub->id_min && event.id <= sub->id_max)
                sub->callback(&event, sub->user_data);
        }
    }

    SYS_LOGI(K_TAG, "dispatch task exiting");
    osal_task_self_delete();
}

/* ── 公开 API ── */

/**
 * @brief 初始化 EventBus
 * @return MINI_OK 成功; MINI_ERR_NOMEM 队列/锁创建失败
 */
int event_bus_init(void)
{
    if (s_bus.inited)
        return MINI_OK;

    s_bus.queue = osal_queue_create(K_QUEUE_LEN, sizeof(struct event));
    if (s_bus.queue == NULL)
    {
        SYS_LOGE(K_TAG, "FATAL: osal_queue_create failed — event bus unusable");
        return MINI_ERR_NOMEM;
    }

    if (osal_mutex_create_static(&s_bus.sub_lock, s_bus.sub_lock_storage,
                                 sizeof(s_bus.sub_lock_storage)) != 0 ||
        s_bus.sub_lock == NULL)
    {
        SYS_LOGE(K_TAG, "FATAL: mutex create failed");
        osal_queue_delete(s_bus.queue);
        s_bus.queue = NULL;
        return MINI_ERR_NOMEM;
    }

    s_bus.inited = true;
    SYS_LOGI(K_TAG, "event bus initialized, queue=%u slots", (unsigned)K_QUEUE_LEN);
    return MINI_OK;
}

/**
 * @brief 订阅事件 ID 区间 (封表前可用, 持锁写入订阅表)
 * @param[in] id_min 最小事件 ID (含)
 * @param[in] id_max 最大事件 ID (含)
 * @param[in] callback 匹配时回调
 * @param[in] user_data 传给 callback 的用户数据
 * @return MINI_OK 成功; MINI_ERR_ISR 中断上下文; MINI_ERR_NOTSUPP 封表;
 *         MINI_ERR_INVAL 参数非法/未初始化; MINI_ERR_TIMEOUT 锁超时; MINI_ERR_NOSPC 表满
 */
int event_bus_subscribe(uint32_t id_min, uint32_t id_max, event_callback_t callback,
                        void* user_data)
{
    if (osal_in_isr())
        return MINI_ERR_ISR;
    if (s_bus.is_sealed)
        return MINI_ERR_NOTSUPP;
    if (callback == NULL)
        return MINI_ERR_INVAL;
    if (s_bus.sub_lock == NULL)
        return MINI_ERR_INVAL;
    if (id_min > id_max)
        return MINI_ERR_INVAL;

    if (osal_mutex_lock(s_bus.sub_lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != OSAL_OK)
    {
        SYS_LOGE(K_TAG, "Fatal: EventBus subscribe lock timeout (possible deadlock)");
        return MINI_ERR_TIMEOUT;
    }

    int ret = MINI_ERR_NOSPC;
    if (s_bus.count < K_MAX_SUBSCRIBERS)
    {
        s_bus.subscribers[s_bus.count].id_min = id_min;
        s_bus.subscribers[s_bus.count].id_max = id_max;
        s_bus.subscribers[s_bus.count].callback = callback;
        s_bus.subscribers[s_bus.count].user_data = user_data;
        s_bus.count++;
        ret = MINI_OK;
    }

    osal_mutex_unlock(s_bus.sub_lock);
    return ret;
}

/**
 * @brief 事件投递内部实现 (任务态 / ISR 共用)
 * @param[in] id 事件 ID
 * @param[in] arg 事件参数
 * @param[in] from_isr 为 true 时走 ISR 安全入队路径
 * @param[in] px_yield_required ISR 路径下输出是否需要 yield (可为 NULL)
 * @return MINI_OK 入队成功; MINI_ERR_AGAIN 总线未初始化/OS 未就绪; MINI_ERR_NOSPC 队列满
 */
static int event_bus_post_internal(uint32_t id, uintptr_t arg, bool from_isr,
                                   bool* px_yield_required)
{
    if (s_bus.queue == NULL || !s_bus.inited)
        return MINI_ERR_AGAIN;

    if (!g_system_os_initialized)
        return MINI_ERR_AGAIN;

    const struct event event = {id, arg};
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
                SYS_LOGW(K_TAG, "event queue full, dropped=%u", (unsigned)cur);
        }
        return MINI_ERR_NOSPC;
    }
    return MINI_OK;
}

/**
 * @brief 任务态 post
 * @param[in] id 事件 ID
 * @param[in] arg 参数
 * @return MINI_OK 成功; MINI_ERR_ISR 中断上下文调用; 其余同 event_bus_post_internal
 */
int event_bus_post(uint32_t id, uintptr_t arg)
{
    if (osal_in_isr())
        return MINI_ERR_ISR;

    return event_bus_post_internal(id, arg, false, NULL);
}

/**
 * @brief ISR post
 * @param[in] id 事件 ID
 * @param[in] arg 参数
 * @param[in] px_yield_required yield
 * @return MINI_OK 成功; MINI_ERR_AGAIN 未就绪; MINI_ERR_NOSPC 队列满
 */
int event_bus_post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required)
{
    return event_bus_post_internal(id, arg, true, px_yield_required);
}

/**
 * @brief 丢弃计数
 * @return 次数
 */
size_t event_bus_dropped_count(void) { return __atomic_load_n(&s_bus.dropped, __ATOMIC_RELAXED); }

/**
 * @brief 启动 dispatch 任务
 */
void event_bus_start(void)
{
    if (s_bus.task != NULL || s_bus.queue == NULL)
        return;

    if (osal_task_create_handle("evt_bus", K_DISPATCH_STACK, K_DISPATCH_PRIO,
                                event_bus_dispatch_task, NULL, 0, &s_bus.task) != 0 ||
        s_bus.task == NULL)
    {
        SYS_LOGW(K_TAG, "dispatch task create failed");
        return;
    }
    COMPAT_IGNORE_RESULT(system_wdt_subscribe(s_bus.task));
    SYS_LOGI(K_TAG, "dispatch task started prio %lu", (unsigned long)K_DISPATCH_PRIO);
}

/**
 * @brief 停止并销毁
 */
void event_bus_stop(void)
{
    if (!s_bus.task)
        return;

    void* handle = s_bus.task;
    s_bus.task = NULL;

    /* 向队列发空事件唤醒 dispatch 线程 */
    const struct event dummy = {EVENT_SYS_FAULT, 0};
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

    /* 先标记未初始化, 阻止新的 post, 再销毁队列 */
    s_bus.inited = false;
    osal_queue_delete(s_bus.queue);
    s_bus.queue = NULL;
}

/**
 * @brief 封表
 */
void event_bus_seal(void) { s_bus.is_sealed = true; }
