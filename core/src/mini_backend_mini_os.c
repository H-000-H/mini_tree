/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file mini_backend_mini_os.c
 *@brief mini-os 后端实现 (互斥锁 / 信号量 / 队列 / 任务 / 内存 / 调度器启动)
 *@author H-000-H
 *@details 三条后端差异约定:
 *           - 优先级数字越小越优先 (同 RT-Thread, 与 FreeRTOS 相反), 故只钳位不翻转;
 *           - *_isr 系列只置 *px_yield_required, 绝不内部 yield;
 *           - 信号量原生二值, 多次 post 合并为 1。
 *         内核数据结构由启动钩子建立, tick 只在 mini_scheduler_start() 打开。
 *         TODO(backend-rename): 条件编译符号随后端符号统一改名阶段收口。
 */

#if defined(CONFIG_OS_MINI_OS)

#include "mini_backend.h"

#include "board_config.h"
#include "compiler_compat.h"
#include "config.h"
#include "err.h"
#include "hal_amp.h"
#include "memory.h"
#include "mini_slot.h"
#include "mutex.h"
#include "queue.h"
#include "redef.h"
#include "schedule.h"
#include "semaphore.h"
#include "status.h"
#include "thread.h"
#include <stdbool.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* 公共换算                                                                    */
/* -------------------------------------------------------------------------- */
/* mini-os err.h 在可见 config.h / status.h 时数值已与 MINI_ERR_* 对齐, 零转换直通;
 * 只有 AGAIN (非阻塞竞争 / 队列满空) 需对齐成 TIMEOUT */
static int mini_err_from_mini_os(mini_os_err_t err)
{
    if (err == MINI_OS_ERR_AGAIN)
        return MINI_ERR_TIMEOUT;
    return (int)err;
}

/* 内核收 tick 不收毫秒 */
MINI_STATIC_INLINE mini_os_tick_t mini_os_timeout(uint32_t timeout_ms)
{
    if (timeout_ms == MINI_WAIT_FOREVER)
        return MINI_OS_WAIT_FOREVER;
    return (mini_os_tick_t)MINI_OS_MS_TO_TICK(timeout_ms);
}

/* -------------------------------------------------------------------------- */
/* 互斥锁 (静态内嵌 mini_os_mutex_t, 带优先级继承)                             */
/* -------------------------------------------------------------------------- */
struct mini_mutex
{
    mini_os_mutex_t obj;
};

_Static_assert(sizeof(struct mini_mutex) <= MINI_MUTEX_STORAGE_SIZE, "mini_backend_mini_os: MINI_MUTEX_STORAGE_SIZE too small");

/* 递归性由创建 API 决定且运行期不可变: 非递归版对同 owner 重入返回 BUSY,
 * 会直接把 device_open (持锁后调 device_set_status 再锁一次) 打成失败 */
static mt_err_t mini_mutex_init(struct mini_mutex* mutex, bool recursive)
{
    if (!mutex)
        return MINI_ERR_INVAL;

    if (recursive)
    {
        if (mini_os_mutex_recuring_create_static(MINI_OS_NULL, &mutex->obj) == MINI_OS_NULL)
            return MINI_ERR_NOMEM;
    }
    else
    {
        if (mini_os_mutex_create_static(&mutex->obj, MINI_OS_NULL) == MINI_OS_NULL)
            return MINI_ERR_NOMEM;
    }

    return MINI_OK;
}

mt_err_t mini_mutex_create_static(mini_mutex_t** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct mini_mutex))
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    struct mini_mutex* mutex = (struct mini_mutex*)storage;
    int                rc    = mini_mutex_init(mutex, false);
    if (rc != MINI_OK)
        return rc;

    *out = (mini_mutex_t*)mutex;
    return MINI_OK;
}

mt_err_t mini_mutex_create_static_recursive(mini_mutex_t** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct mini_mutex))
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    struct mini_mutex* mutex = (struct mini_mutex*)storage;
    int                rc    = mini_mutex_init(mutex, true);
    if (rc != MINI_OK)
        return rc;

    *out = (mini_mutex_t*)mutex;
    return MINI_OK;
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

    if (mini_mutex_init(&s_mutex_pool[idx], false) != MINI_OK)
    {
        MINI_IGNORE_RESULT(mini_slot_release(&s_mutex_pool_ctrl, idx));
        return MINI_ERR_NOMEM;
    }

    *out = (mini_mutex_t*)&s_mutex_pool[idx];
    return MINI_OK;
}

/* 引导阶段判定: 调度器启动前没有可调度的线程, mini_os_thread_current() 恒为 NULL。
 * mini_os_mutex_lock 对无线程上下文一律拒绝 (连空闲锁也不放行)我不会改mini-os锁判断逻辑在内核里面
 * 是合理的如果有人想要提前使用锁可以和我一样的处理后期顶层用mini-os源语没有问题因为此时已经绑定这mutex
  */
static inline bool mini_mutex_in_boot_context(void)
{
    return mini_os_thread_current() == MINI_OS_NULL;
}

mt_err_t mini_mutex_lock(mini_mutex_t* mtx, uint32_t timeout_ms)
{
    if (!mtx)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    /* 引导阶段只有引导上下文在跑 (单线程, probe 期间更是 IRQ_DISABLE), 无需互斥;
     * 直接放行, 语义等价于其他后端"空闲锁即刻获取"。 */
    if (mini_mutex_in_boot_context())
        return MINI_OK;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    return mini_err_from_mini_os(mini_os_mutex_lock(&mutex->obj, mini_os_timeout(timeout_ms)));
}

mt_err_t mini_mutex_unlock(mini_mutex_t* mtx)
{
    if (!mtx)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    /* 与 lock 对称: 引导阶段并未真正持锁, 内核里也没有 owner 可释放 */
    if (mini_mutex_in_boot_context())
        return MINI_OK;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    if (mini_os_mutex_unlock(&mutex->obj) != MINI_OS_OK)
        return MINI_ERR_IO;
    return MINI_OK;
}

void mini_mutex_destroy(mini_mutex_t* mtx)
{
    if (!mtx || hal_is_in_isr())
        return;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    MINI_IGNORE_RESULT(mini_os_mutex_delete_static(&mutex->obj));

    /* 仅池内对象归还槽位; 静态存储的对象不在池内 */
    if (mutex >= s_mutex_pool && mutex < &s_mutex_pool[MINI_MUTEX_POOL_SIZE])
        MINI_IGNORE_RESULT(mini_slot_release(&s_mutex_pool_ctrl, (int)(mutex - s_mutex_pool)));
}

/* -------------------------------------------------------------------------- */
/* 二值信号量 (静态内嵌 mini_os_semaphore_t, max_count=1)                       */
/* -------------------------------------------------------------------------- */
struct mini_sem
{
    mini_os_semaphore_t obj;
};

_Static_assert(sizeof(struct mini_sem) <= MINI_SEM_STORAGE_SIZE, "mini_backend_mini_os: MINI_SEM_STORAGE_SIZE too small");

static mt_err_t mini_sem_init(struct mini_sem* sem)
{
    if (mini_os_binary_semaphore_create_static(MINI_OS_NULL, &sem->obj) == MINI_OS_NULL)
        return MINI_ERR_NOMEM;
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
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    struct mini_sem* obj = (struct mini_sem*)sem;
    return mini_err_from_mini_os(mini_os_semaphore_take(&obj->obj, mini_os_timeout(timeout_ms)));
}

bool mini_sem_post(mini_sem_t* sem)
{
    if (!sem || hal_is_in_isr())
        return false;

    struct mini_sem* obj = (struct mini_sem*)sem;
    return mini_os_semaphore_give(&obj->obj) == MINI_OS_OK;
}

/* 内核 give_isr 不上报是否唤醒了更高优先级线程: 成功即置 yield 标志,
 * 是否真正切换由 ISR 出口 mini_yield_from_isr() 自判就绪位图 */
bool mini_sem_post_from_isr(mini_sem_t* sem, bool* px_yield_required)
{
    if (!sem)
        return false;

    struct mini_sem* obj    = (struct mini_sem*)sem;
    bool             posted = mini_os_semaphore_give_isr(&obj->obj) == MINI_OS_OK;
    if (posted && px_yield_required != NULL)
        *px_yield_required = true;
    return posted;
}

void mini_sem_destroy(mini_sem_t* sem)
{
    if (!sem || hal_is_in_isr())
        return;

    /* 仍有等待者时返回 BUSY, 忽略 */
    struct mini_sem* obj = (struct mini_sem*)sem;
    MINI_IGNORE_RESULT(mini_os_semaphore_delete_static(&obj->obj));

    /* 仅池内对象归还槽位 */
    if (obj >= s_sem_pool && obj < &s_sem_pool[MINI_SEM_POOL_SIZE])
        MINI_IGNORE_RESULT(mini_slot_release(&s_sem_pool_ctrl, (int)(obj - s_sem_pool)));
}

/* -------------------------------------------------------------------------- */
/* 定长消息队列 (描述符与消息池由内核堆分配, 兼作网络层邮箱)                   */
/* -------------------------------------------------------------------------- */
mini_queue_t* mini_queue_create(size_t queue_len, size_t item_size)
{
    /* 内核字段位宽: max_depth = uint8_t, msg_size = uint16_t */
    if (queue_len == 0 || item_size == 0)
        return NULL;
    if (queue_len > (size_t)UINT8_MAX)
        return NULL;
    if (item_size > (size_t)UINT16_MAX)
        return NULL;

    return (mini_queue_t*)mini_os_queue_create(MINI_OS_NULL, (mini_os_uint16_t)item_size, (mini_os_uint8_t)queue_len);
}

void mini_queue_delete(mini_queue_t* queue)
{
    if (!queue)
        return;
    MINI_IGNORE_RESULT(mini_os_queue_delete((mini_os_queue_t*)queue));
}

bool mini_queue_send(mini_queue_t* queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || hal_is_in_isr())
        return false;

    return mini_os_queue_send((mini_os_queue_t*)queue, item, mini_os_timeout(timeout_ms)) == MINI_OS_OK;
}

bool mini_queue_send_from_isr(mini_queue_t* queue, const void* item, bool* px_yield_required)
{
    if (!queue)
        return false;

    bool sent = mini_os_queue_send_isr((mini_os_queue_t*)queue, item) == MINI_OS_OK;
    if (sent && px_yield_required != NULL)
        *px_yield_required = true;
    return sent;
}

bool mini_queue_receive(mini_queue_t* queue, void* item, uint32_t timeout_ms)
{
    if (!queue || hal_is_in_isr())
        return false;

    return mini_os_queue_receive((mini_os_queue_t*)queue, item, mini_os_timeout(timeout_ms)) == MINI_OS_OK;
}

bool mini_queue_receive_from_isr(mini_queue_t* queue, void* item, bool* px_yield_required)
{
    if (!queue)
        return false;

    bool received = mini_os_queue_receive_isr((mini_os_queue_t*)queue, item) == MINI_OS_OK;
    if (received && px_yield_required != NULL)
        *px_yield_required = true;
    return received;
}

/* -------------------------------------------------------------------------- */
/* 内核自举 (启动钩子, main 之前执行一次)                                       */
/* -------------------------------------------------------------------------- */
/* 只建内核数据结构与 idle 线程, **不点 tick**: mini_os_systick_init() 会真的打开
 * SysTick, 而它的优先级要到 mini_os_schedule_start() 才设成 0xFE —— 在这里点会让
 * SysTick 以复位优先级 0 (最高) 先跑一段。tick 统一由 mini_scheduler_start() 打开。
 *
 * 不能改成"首次创建任务时惰性自举": system_init.h 的时序是 pre_os_init ->
 * start_tasks (创建框架任务) -> scheduler_start, 任务先于调度器创建, 所以内核
 * 数据结构必须更早就绪。161 位于驱动池(160)与下半部池(170)之间。 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_SCHEDULER) static void mini_backend_kernel_boot(void)
{
    /* schedule_init 目前无失败路径, 忽略返回值 (构造函数无处返回错误码) */
    MINI_IGNORE_RESULT(mini_os_schedule_init());
    mini_os_thread_idle_create();
}

/* -------------------------------------------------------------------------- */
/* 调度器启动 (显式入口, 由板级 / app 按时序调用)                              */
/* -------------------------------------------------------------------------- */
mt_err_t mini_scheduler_start(void)
{
    /* 顺序不可颠倒: schedule_start 会立刻置 PendSV 并开中断, 控制权随即交给首个
     * 线程, 其后的语句可能永不执行 —— 必须先把 tick 配好。0 = 默认 tick 频率 */
    mini_os_systick_init(0u);

    return mini_err_from_mini_os(mini_os_schedule_start());
}

/* mini-os 无 suspend-all, 退化为关中断 */
void mini_sched_freeze(void) { mini_os_irq_disable(); }

/* -------------------------------------------------------------------------- */
/* 任务 (mini_os_thread_*, 动态栈)                                             */
/* -------------------------------------------------------------------------- */
/* 只钳位, 不翻转方向: mini-os 数字越小越优先 */
MINI_STATIC_INLINE uint32_t mini_clamp_task_priority(uint32_t priority)
{
    if (priority >= (uint32_t)MINI_OS_PRIORITY)
        return (uint32_t)(MINI_OS_PRIORITY - 1U);
    return priority;
}

MINI_STATIC_INLINE uint32_t mini_clamp_stack_size(uint32_t stack_bytes)
{
    if (stack_bytes < (uint32_t)MINI_OS_THREAD_MIN_STACK_SIZE)
        return (uint32_t)MINI_OS_THREAD_MIN_STACK_SIZE;
    return stack_bytes;
}

mt_err_t mini_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, mini_task_entry_t entry, void* param, int core_id,
                            mini_task_handle_t* out_handle)
{
    MINI_UNUSED_PARAM(core_id); /* mini-os 单核 */

    if (!out_handle)
        return MINI_ERR_INVAL;

    *out_handle = NULL;
    if (!entry)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    mini_os_thread_t* handle =
        mini_os_thread_create(name, mini_clamp_stack_size(stack_size), (mini_os_uint8_t)mini_clamp_task_priority(priority), entry, param);
    if (handle == MINI_OS_NULL)
        return MINI_ERR_NOMEM;

    *out_handle = (mini_task_handle_t)handle;
    return MINI_OK;
}

void mini_task_self_delete(void) { mini_os_thread_exit(MINI_OS_NULL); }

void mini_task_delete(mini_task_handle_t task)
{
    if (!task)
        return;
    MINI_IGNORE_RESULT(mini_os_thread_delete((mini_os_thread_t*)task));
}

bool mini_task_is_running(mini_task_handle_t task)
{
    if (!task)
        return false;

    mini_os_thread_state_t state;
    if (mini_os_thread_get_state((mini_os_thread_t*)task, &state) != MINI_OS_OK)
        return false;
    return state != MINI_OS_THREAD_STATE_TERMINATED && state != MINI_OS_THREAD_STATE_INVALID;
}

const char* mini_task_get_name(mini_task_handle_t task)
{
    if (!task)
        return "?";
    return ((mini_os_thread_t*)task)->thread_name;
}

uint32_t mini_task_get_stack_watermark(mini_task_handle_t task)
{
    /* mini-os 无逐线程栈高水位 API, 仅 MSP 哨兵检测 */
    MINI_UNUSED_PARAM(task);
    return 0U;
}

/* -------------------------------------------------------------------------- */
/* ISR 出口上下文切换                                                          */
/* -------------------------------------------------------------------------- */
void mini_yield_from_isr(bool yield_required)
{
    if (yield_required)
        MINI_IGNORE_RESULT(mini_os_schedule_yield_isr());
}

/* -------------------------------------------------------------------------- */
/* 内存 (走 mini-os 自带堆)                                                    */
/* -------------------------------------------------------------------------- */
void* mini_malloc(size_t size) { return mini_os_malloc(size); }

void* mini_calloc(size_t count, size_t size) { return mini_os_calloc(count, size); }

mt_err_t mini_free(void* ptr)
{
    MINI_IGNORE_RESULT(mini_os_free(ptr));
    return MINI_OK;
}

#endif /* CONFIG_OS_MINI_OS */
