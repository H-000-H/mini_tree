/**
 * @file mini_backend_rtthread.c
 * @author H-000-H
 * @brief RT-Thread 后端实现 (互斥锁 / 信号量 / 队列 / 任务 / 内存 / 调度器启动)
 * @note  优先级数字越小越优先 (同 mini-os, 与 FreeRTOS 相反), 故只钳位不翻转
 * @note  *_from_isr 无需上报 yield (RT-Thread 异常返回时自行调度), px_yield_required 一律忽略
 * @note  递归锁用 rt_mutex (带优先级继承), 普通锁用 count=1 的 rt_sem (无继承)——既有语义勿“顺手统一”
 * @note  内存走独立系统堆 s_rtt_heap (RTT_HEAP_SIZE, 板级可覆盖), 首次分配时惰性初始化
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#if defined(CONFIG_OS_RTTHREAD)

#include "mini_backend.h"

#include "board_config.h"
#include "compiler_compat.h"
#include "config.h"
#include "hal_amp.h"
#include "mini_slot.h"
#include <rthw.h>
#include <rtthread.h>

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* 系统堆 (线程栈 / IPC 对象由此分配)                                          */
/* -------------------------------------------------------------------------- */
#ifndef RTT_HEAP_SIZE
#ifdef CONFIG_RTT_HEAP_SIZE
#define RTT_HEAP_SIZE CONFIG_RTT_HEAP_SIZE
#else
#define RTT_HEAP_SIZE (32 * 1024)
#endif
#endif

static uint8_t      s_rtt_heap[RTT_HEAP_SIZE] MINI_ALIGNED(4);
static volatile int s_rtt_heap_inited = 0;

static void rtt_heap_init_once(void)
{
    if (!s_rtt_heap_inited)
    {
        rt_system_heap_init(s_rtt_heap, s_rtt_heap + sizeof(s_rtt_heap));
        s_rtt_heap_inited = 1;
    }
}

/* -------------------------------------------------------------------------- */
/* 公共换算                                                                    */
/* -------------------------------------------------------------------------- */
/* 内核收 tick 不收毫秒 */
MINI_STATIC_INLINE rt_int32_t mini_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == MINI_WAIT_FOREVER)
        return RT_WAITING_FOREVER;
    return rt_tick_from_millisecond(timeout_ms);
}

/* -------------------------------------------------------------------------- */
/* 互斥锁                                                                      */
/* -------------------------------------------------------------------------- */
typedef enum
{
    MINI_MUTEX_RECURSIVE = 0,
    MINI_MUTEX_PLAIN = 1,
} mini_mutex_type_t;

struct mini_mutex
{
    mini_mutex_type_t type;
    union
    {
        struct rt_mutex     mutex; /**< 递归锁: 带优先级继承 */
        struct rt_semaphore sem;   /**< 普通锁: count=1, 无优先级继承 */
    } u;
};

_Static_assert(sizeof(struct mini_mutex) <= MINI_MUTEX_STORAGE_SIZE, "mini_backend_rtthread: MINI_MUTEX_STORAGE_SIZE too small");

static mt_err_t mini_mutex_init(struct mini_mutex* mutex, mini_mutex_type_t type, const char* name)
{
    if (!mutex)
        return MINI_ERR_INVAL;

    mutex->type = type;
    if (type == MINI_MUTEX_RECURSIVE)
        return rt_mutex_init(&mutex->u.mutex, name, RT_IPC_FLAG_PRIO) == RT_EOK ? MINI_OK : MINI_ERR_NOMEM;
    if (type == MINI_MUTEX_PLAIN)
        return rt_sem_init(&mutex->u.sem, name, 1, RT_IPC_FLAG_PRIO) == RT_EOK ? MINI_OK : MINI_ERR_NOMEM;
    return MINI_ERR_INVAL;
}

static mt_err_t mini_mutex_create_static_typed(mini_mutex_t** out, void* storage, size_t storage_size, mini_mutex_type_t type, const char* name)
{
    if (!out || !storage || storage_size < sizeof(struct mini_mutex))
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    *out = NULL;
    struct mini_mutex* mutex = (struct mini_mutex*)storage;
    if (mini_mutex_init(mutex, type, name) != MINI_OK)
        return MINI_ERR_NOMEM;

    *out = (mini_mutex_t*)mutex;
    return MINI_OK;
}

mt_err_t mini_mutex_create_static(mini_mutex_t** out, void* storage, size_t storage_size)
{
    return mini_mutex_create_static_typed(out, storage, storage_size, MINI_MUTEX_PLAIN, "mini_mtx");
}

mt_err_t mini_mutex_create_static_recursive(mini_mutex_t** out, void* storage, size_t storage_size)
{
    return mini_mutex_create_static_typed(out, storage, storage_size, MINI_MUTEX_RECURSIVE, "mini_rmtx");
}

/* 池化互斥锁: 给拿不出静态存储的调用方 (lwIP sys_mutex_new) 用 */
static struct mini_mutex s_mutex_pool[MINI_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t           s_mutex_used[MINI_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static mini_slot_t       s_mutex_pool_ctrl MINI_ALIGNED(4);

mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void mini_mutex_pool_boot(void)
{
    MINI_IGNORE_RESULT(mini_slot_init(&s_mutex_pool_ctrl, s_mutex_used, MINI_MUTEX_POOL_SIZE));
}

mt_err_t mini_mutex_create(mini_mutex_t** out)
{
    if (!out)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;
    *out = NULL;

    int idx = mini_slot_claim(&s_mutex_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    if (mini_mutex_init(&s_mutex_pool[idx], MINI_MUTEX_PLAIN, "mini_mtx") != MINI_OK)
    {
        MINI_IGNORE_RESULT(mini_slot_release(&s_mutex_pool_ctrl, idx));
        return MINI_ERR_NOMEM;
    }

    *out = (mini_mutex_t*)&s_mutex_pool[idx];
    return MINI_OK;
}

mt_err_t mini_mutex_lock(mini_mutex_t* mtx, uint32_t timeout_ms)
{
    if (!mtx)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    rt_int32_t         ticks = mini_timeout_to_ticks(timeout_ms);
    if (mutex->type == MINI_MUTEX_RECURSIVE)
        return rt_mutex_take(&mutex->u.mutex, ticks) == RT_EOK ? MINI_OK : MINI_ERR_TIMEOUT;
    return rt_sem_take(&mutex->u.sem, ticks) == RT_EOK ? MINI_OK : MINI_ERR_TIMEOUT;
}

mt_err_t mini_mutex_unlock(mini_mutex_t* mtx)
{
    if (!mtx)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    if (mutex->type == MINI_MUTEX_RECURSIVE)
        return rt_mutex_release(&mutex->u.mutex) == RT_EOK ? MINI_OK : MINI_ERR_IO;
    return rt_sem_release(&mutex->u.sem) == RT_EOK ? MINI_OK : MINI_ERR_IO;
}

void mini_mutex_destroy(mini_mutex_t* mtx)
{
    if (!mtx || hal_is_in_isr())
        return;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    if (mutex->type == MINI_MUTEX_RECURSIVE)
        rt_mutex_detach(&mutex->u.mutex);
    else
        rt_sem_detach(&mutex->u.sem);

    /* 仅池内对象归还槽位; 静态存储的对象不在池内 */
    if (mutex >= s_mutex_pool && mutex < &s_mutex_pool[MINI_MUTEX_POOL_SIZE])
        MINI_IGNORE_RESULT(mini_slot_release(&s_mutex_pool_ctrl, (int)(mutex - s_mutex_pool)));
}

/* -------------------------------------------------------------------------- */
/* 二值信号量                                                                  */
/* -------------------------------------------------------------------------- */
struct mini_sem
{
    struct rt_semaphore sem;
    bool                inited;
};

_Static_assert(sizeof(struct mini_sem) <= MINI_SEM_STORAGE_SIZE, "mini_backend_rtthread: MINI_SEM_STORAGE_SIZE too small");

static mt_err_t mini_sem_init(struct mini_sem* sem)
{
    if (rt_sem_init(&sem->sem, "mini_sem", 0, RT_IPC_FLAG_PRIO) != RT_EOK)
        return MINI_ERR_NOMEM;

    sem->inited = true;
    return MINI_OK;
}

mt_err_t mini_sem_create_binary_static(mini_sem_t** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct mini_sem))
        return MINI_ERR_INVAL;

    struct mini_sem* sem = (struct mini_sem*)storage;
    if (mini_sem_init(sem) != MINI_OK)
        return MINI_ERR_NOMEM;

    *out = (mini_sem_t*)sem;
    return MINI_OK;
}

/* 池化信号量: 给拿不出静态存储的调用方 (lwIP sys_sem_new) 用 */
static struct mini_sem s_sem_pool[MINI_SEM_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t         s_sem_used[MINI_SEM_POOL_SIZE] MINI_ALIGNED(4);
static mini_slot_t     s_sem_pool_ctrl MINI_ALIGNED(4);

mini_pre_execution(MINI_PRE_EXEC_PRIO_SEM_POOL) static void mini_sem_pool_boot(void)
{
    MINI_IGNORE_RESULT(mini_slot_init(&s_sem_pool_ctrl, s_sem_used, MINI_SEM_POOL_SIZE));
}

mt_err_t mini_sem_create_binary(mini_sem_t** out)
{
    if (!out)
        return MINI_ERR_INVAL;
    *out = NULL;

    int idx = mini_slot_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    struct mini_sem* sem = &s_sem_pool[idx];
    if (mini_sem_init(sem) != MINI_OK)
    {
        MINI_IGNORE_RESULT(mini_slot_release(&s_sem_pool_ctrl, idx));
        return MINI_ERR_NOMEM;
    }

    *out = (mini_sem_t*)sem;
    return MINI_OK;
}

mt_err_t mini_sem_wait(mini_sem_t* sem, uint32_t timeout_ms)
{
    if (!sem)
        return MINI_ERR_INVAL;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->inited || hal_is_in_isr())
        return MINI_ERR_ISR;

    return rt_sem_take(&obj->sem, mini_timeout_to_ticks(timeout_ms)) == RT_EOK ? MINI_OK : MINI_ERR_TIMEOUT;
}

bool mini_sem_post(mini_sem_t* sem)
{
    if (!sem)
        return false;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->inited || hal_is_in_isr())
        return false;

    return rt_sem_release(&obj->sem) == RT_EOK;
}

/* RT-Thread 在异常返回时自行调度, 故忽略 px_yield_required */
bool mini_sem_post_from_isr(mini_sem_t* sem, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);

    if (!sem)
        return false;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->inited)
        return false;

    return rt_sem_release(&obj->sem) == RT_EOK;
}

void mini_sem_destroy(mini_sem_t* sem)
{
    if (!sem)
        return;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->inited)
        return;

    rt_sem_detach(&obj->sem);
    obj->inited = false;

    /* 仅池内对象归还槽位 */
    if (obj >= s_sem_pool && obj < &s_sem_pool[MINI_SEM_POOL_SIZE])
        MINI_IGNORE_RESULT(mini_slot_release(&s_sem_pool_ctrl, (int)(obj - s_sem_pool)));
}

/* -------------------------------------------------------------------------- */
/* 定长消息队列 (rt_mq; 未启用 RT_USING_MESSAGEQUEUE 时整组降级为空实现)        */
/* -------------------------------------------------------------------------- */
#ifdef RT_USING_MESSAGEQUEUE
struct mini_queue
{
    rt_mq_t mq;
    size_t  item_size;
};

mini_queue_t* mini_queue_create(size_t queue_len, size_t item_size)
{
    if (queue_len == 0 || item_size == 0)
        return NULL;

    rtt_heap_init_once();

    struct mini_queue* obj = rt_malloc(sizeof(struct mini_queue));
    if (!obj)
        return NULL;

    obj->mq = rt_mq_create("mini_mq", item_size, queue_len, RT_IPC_FLAG_PRIO);
    if (!obj->mq)
    {
        rt_free(obj);
        return NULL;
    }
    obj->item_size = item_size;
    return (mini_queue_t*)obj;
}

void mini_queue_delete(mini_queue_t* queue)
{
    if (!queue)
        return;
    struct mini_queue* obj = (struct mini_queue*)queue;
    rt_mq_delete(obj->mq);
    rt_free(obj);
}

bool mini_queue_send(mini_queue_t* queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || !item || hal_is_in_isr())
        return false;
    struct mini_queue* obj = (struct mini_queue*)queue;
    return rt_mq_send_wait(obj->mq, item, obj->item_size, mini_timeout_to_ticks(timeout_ms)) == RT_EOK;
}

/* ISR 快路径: rt_mq_send 非阻塞 */
bool mini_queue_send_from_isr(mini_queue_t* queue, const void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);

    if (!queue || !item)
        return false;
    struct mini_queue* obj = (struct mini_queue*)queue;
    return rt_mq_send(obj->mq, item, obj->item_size) == RT_EOK;
}

bool mini_queue_receive(mini_queue_t* queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item || hal_is_in_isr())
        return false;
    struct mini_queue* obj = (struct mini_queue*)queue;
    return rt_mq_recv(obj->mq, item, obj->item_size, mini_timeout_to_ticks(timeout_ms)) >= 0;
}

/* rt_mq 无 ISR 接收 API (且 ISR 内不应阻塞接收), 保持原实现的空实现 */
bool mini_queue_receive_from_isr(mini_queue_t* queue, void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
}
#else
/* 未启用消息队列: 整组降级, 误用返回失败而不是链接报错 (保持原实现语义) */
mini_queue_t* mini_queue_create(size_t queue_len, size_t item_size)
{
    MINI_UNUSED_PARAM(queue_len);
    MINI_UNUSED_PARAM(item_size);
    return NULL;
}

void mini_queue_delete(mini_queue_t* queue) { MINI_UNUSED_PARAM(queue); }

bool mini_queue_send(mini_queue_t* queue, const void* item, uint32_t timeout_ms)
{
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    MINI_UNUSED_PARAM(timeout_ms);
    return false;
}

bool mini_queue_send_from_isr(mini_queue_t* queue, const void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
}

bool mini_queue_receive(mini_queue_t* queue, void* item, uint32_t timeout_ms)
{
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    MINI_UNUSED_PARAM(timeout_ms);
    return false;
}

bool mini_queue_receive_from_isr(mini_queue_t* queue, void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
}
#endif /* RT_USING_MESSAGEQUEUE */

/* -------------------------------------------------------------------------- */
/* 任务 (动态栈, 创建后立即 startup)                                           */
/* -------------------------------------------------------------------------- */
/* 只钳位, 不翻转方向: RT-Thread 数字越小越优先 */
MINI_STATIC_INLINE rt_uint8_t mini_clamp_task_priority(uint32_t priority)
{
    if (priority >= (uint32_t)RT_THREAD_PRIORITY_MAX)
        return (rt_uint8_t)(RT_THREAD_PRIORITY_MAX - 1U);
    return (rt_uint8_t)priority;
}

mt_err_t mini_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, mini_task_entry_t entry, void* param, int core_id,
                            mini_task_handle_t* out_handle)
{
    if (!out_handle || !entry)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param, stack_size, mini_clamp_task_priority(priority), 10);
    if (!thread)
        return MINI_ERR_NOMEM;

#ifdef RT_USING_SMP
    if (core_id >= 0)
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
#else
    MINI_UNUSED_PARAM(core_id);
#endif

    rt_thread_startup(thread);
    *out_handle = (mini_task_handle_t)thread;
    return MINI_OK;
}

void mini_task_self_delete(void)
{
    rt_thread_delete(rt_thread_self());
    rt_schedule();
}

void mini_task_delete(mini_task_handle_t task)
{
    if (!task)
        return;
    rt_thread_delete((rt_thread_t)task);
}

bool mini_task_is_running(mini_task_handle_t task)
{
    if (!task)
        return false;
    rt_uint8_t stat = RT_SCHED_CTX((rt_thread_t)task).stat & RT_THREAD_STAT_MASK;
    return stat != RT_THREAD_CLOSE && stat != RT_THREAD_INIT;
}

const char* mini_task_get_name(mini_task_handle_t task)
{
    if (!task)
        return "?";
    return ((struct rt_object*)((rt_thread_t)task))->name;
}

/* RT-Thread 把线程栈按 '#' 填充, 从栈底起连续 '#' 字节数即剩余空闲栈 */
uint32_t mini_task_get_stack_watermark(mini_task_handle_t task)
{
    if (!task)
        return 0;

    rt_thread_t    thread = (rt_thread_t)task;
    const uint8_t* stack  = (const uint8_t*)thread->stack_addr;
    uint32_t       count  = 0;
    while (count < thread->stack_size && stack[count] == '#')
        count++;
    return count;
}

/* -------------------------------------------------------------------------- */
/* 调度器启动 / ISR 出口                                                       */
/* -------------------------------------------------------------------------- */
mt_err_t mini_scheduler_start(void)
{
    rt_system_scheduler_start();
    return MINI_OK; /* 正常情况下不返回 */
}

/* fail-fast 单向冻结: 进入 RT-Thread 调度器锁临界区 */
void mini_sched_freeze(void) { rt_enter_critical(); }

/* RT-Thread 在异常返回时自行调度, ISR 出口无需显式 yield */
void mini_yield_from_isr(bool yield_required) { MINI_UNUSED_PARAM(yield_required); }

/* -------------------------------------------------------------------------- */
/* 内存 (走 RT-Thread 系统堆)                                                  */
/* -------------------------------------------------------------------------- */
void* mini_malloc(size_t size)
{
    rtt_heap_init_once();
    return rt_malloc(size);
}

void* mini_calloc(size_t count, size_t size)
{
    rtt_heap_init_once();
    return rt_calloc(count, size);
}

mt_err_t mini_free(void* ptr)
{
    rt_free(ptr);
    return MINI_OK;
}

#endif /* CONFIG_OS_RTTHREAD */
