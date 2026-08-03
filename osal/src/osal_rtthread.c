/* SPDX-License-Identifier: Apache-2.0 */
/*
 * osal_rtthread.c — OSAL RT-Thread 后端实现
 *
 * 将 OSAL API 映射到 rt_mutex/rt_sem/rt_mq/rt_thread 等 RT-Thread 原语
 * 维护独立系统堆 s_rtt_heap (RTT_HEAP_SIZE, 板级可覆盖), 首次分配时惰性初始化
 * 优先级语义与 FreeRTOS 相反 (0=最高), 切换后端时需注意
 */
#ifdef CONFIG_OSAL_RTTHREAD

#define ALLOW_STDIO_OUTPUT

#include "board_config.h"
#include "compiler_compat.h"
#include "config.h"
#include "osal.h"
#include <rthw.h>
#include <rtthread.h>
#include <stdarg.h>
#include <stdlib.h>

#include "compiler_compat_poison.h"

/*
 * 最小堆大小 — Kconfig CONFIG_RTT_HEAP_SIZE 优先, 否则默认 32 KB.
 * 实际线程栈、IPC 对象等内存从此堆分配.
 */
#ifndef RTT_HEAP_SIZE
#ifdef CONFIG_RTT_HEAP_SIZE
#define RTT_HEAP_SIZE CONFIG_RTT_HEAP_SIZE
#else
#define RTT_HEAP_SIZE (32 * 1024)
#endif
#endif

static uint8_t s_rtt_heap[RTT_HEAP_SIZE] COMPAT_ALIGNED(4);
static volatile int s_rtt_heap_inited = 0;

/**
 * @brief 确保 RT-Thread 系统堆在首次分配前完成一次性初始化
 */
static void rtt_heap_init_once(void)
{
    if (!s_rtt_heap_inited)
    {
        rt_system_heap_init(s_rtt_heap, s_rtt_heap + sizeof(s_rtt_heap));
        s_rtt_heap_inited = 1;
    }
}

/* ── 互斥锁内部存储 ── */
struct osal_mutex
{
    osal_mutex_type_t type; /**< 互斥锁类型 */
    union
    {
        struct rt_mutex mutex; /**< RT-Thread 互斥锁 */
        struct rt_semaphore sem; /**< RT-Thread 信号量 (递归锁用) */
    } u; /**< 后端实现 (mutex 或 sem) */
};

/**
 * @brief 初始化互斥锁
 * @param m 互斥锁指针
 * @param type 互斥锁类型
 * @param name 互斥锁名称
 * @return 结果
 * @details 初始化互斥锁时, 使用 rt_mutex_init 或 rt_sem_init 初始化互斥锁
 */
static int osal_mutex_init(struct osal_mutex* m, osal_mutex_type_t type, const char* name)
{
    if (!m)
        return OSAL_ERR_INVAL;

    m->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_init(&m->u.mutex, name, RT_IPC_FLAG_PRIO) == RT_EOK ? OSAL_OK :
                                                                              OSAL_ERR_NOMEM;
    if (type == OSAL_MUTEX_PLAIN)
        return rt_sem_init(&m->u.sem, name, 1, RT_IPC_FLAG_PRIO) == RT_EOK ? OSAL_OK :
                                                                             OSAL_ERR_NOMEM;
    return OSAL_ERR_INVAL;
}

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "OSAL_MUTEX_STORAGE_SIZE too small");

/* ── ISR 上下文检测 ── */
/**
 * @brief rt_interrupt_get_nest()>0
 * @return 1 在 ISR
 */
int osal_in_isr(void) { return rt_interrupt_get_nest() > 0; }

/* ── Spinlock ── */
/**
 * @details 默认 CONFIG_OSAL_SPINLOCK_IRQ_DISABLE: 关中断临界区.
 *          CONFIG_OSAL_SPINLOCK_ATOMIC: 原子 test-and-set 忙等自旋锁 (仅适合 SMP).
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    rt_base_t level; /**< IRQ 关断级别保存 */
#else
    volatile int locked; /**< 原子锁标志 (0=空闲, 1=持有) */
#endif
};

/**
 * @brief 初始化锁
 * @param lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    lock->level = 0;
#else
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 关中断或原子锁
 * @param lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    lock->level = rt_hw_interrupt_disable();
#else
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
        ;
#endif
    return OSAL_OK;
}

/**
 * @brief 恢复中断
 * @param lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    rt_base_t level = lock->level;
    lock->level = 0;
    rt_hw_interrupt_enable(level);
#else
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/* ── 静态互斥锁池 ── */
static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t s_mutex_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 * @details 上电时通过 pre_execution 调用 osal_pool_init 初始化互斥锁池控制结构体
 */
pre_execution(150) static void osal_mutex_pool_boot_init(void)
{
    osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE);
}

/**
 * @brief 初始化池
 * @param pool 池
 * @param buffer 数组
 * @param count 数量
 * @return 0 或 INVAL
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* buffer, size_t count)
{
    if (!pool || !buffer || count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = buffer;
    pool->slot_count = count;

    for (size_t i = 0; i < count; i++)
        buffer[i] = 0;

    return 0;
}

/**
 * @brief 随机起点扫描申请
 * @param pool 池
 * @return 索引
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;

    uint32_t rand_val = COMPAT_RAND(0x43U, 0x32U, 0x43U, 0x32U);
    size_t start_idx = rand_val % pool->slot_count;

    int claimed_index = -1;
    rt_base_t level = rt_hw_interrupt_disable();
    for (size_t i = 0; i < pool->slot_count; i++)
    {
        size_t cur = (start_idx + i) % pool->slot_count;
        if (!pool->used_slots[cur])
        {
            pool->used_slots[cur] = 1;
            claimed_index = (int)cur;
            break;
        }
    }
    rt_hw_interrupt_enable(level);
    return claimed_index;
}

/**
 * @brief 释放槽
 * @param pool 池
 * @param slot_index 索引
 * @return OSAL_OK
 */
int osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return OSAL_ERR_INVAL;

    rt_base_t level = rt_hw_interrupt_disable();
    pool->used_slots[slot_index] = 0;
    rt_hw_interrupt_enable(level);
    return OSAL_OK;
}

/* ── 时间 ── */
/**
 * @brief rt_tick_get 转 ms
 * @return 毫秒
 */
uint32_t osal_time_ms(void) { return rt_tick_get() * 1000 / RT_TICK_PER_SECOND; }

/**
 * @brief rt_thread_mdelay
 * @param ms 毫秒
 */
void osal_delay_ms(uint32_t ms) { rt_thread_mdelay(ms); }

void osal_delay_us(uint32_t us)
{
    if (us == 0U)
        return;
    /* RT-Thread 常见 BSP 提供 rt_hw_us_delay；无则退化为忙等 */
#ifdef RT_USING_HW_USDELAY
    rt_hw_us_delay(us);
#else
    {
        volatile uint32_t n = us * 8U;
        while (n-- > 0U)
            ;
    }
#endif
}

/**
 * @brief rt_tick_from_millisecond
 * @param ms 毫秒
 * @return tick
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms) { return rt_tick_from_millisecond(ms); }

/**
 * @brief 超时转 tick
 * @param timeout_ms 毫秒
 * @return tick
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return RT_WAITING_FOREVER;
    return rt_tick_from_millisecond(timeout_ms);
}

/* ── 内存 ── */
/**
 * @brief 从 RT-Thread 系统堆分配并清零内存
 * @param count 元素个数
 * @param size 每个元素字节数
 * @return 成功返回指针, 失败返回 NULL
 */
void* osal_calloc(size_t count, size_t size)
{
    rtt_heap_init_once();
    return rt_calloc(count, size);
}

/**
 * @brief rt_free
 * @param ptr 指针
 * @return OSAL_OK
 */
int osal_free(void* ptr)
{
    rt_free(ptr);
    return OSAL_OK;
}

/* ── 互斥锁 ── */
/**
 * @brief 从静态池创建指定类型的 RT-Thread 互斥锁
 * @param out 输出互斥锁指针
 * @param type OSAL_MUTEX_PLAIN 或 OSAL_MUTEX_RECURSIVE
 * @return 0 成功; OSAL_ERR_INVAL/ISR/NOMEM 失败
 */
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN)
        return OSAL_ERR_INVAL;
    *out = NULL;

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0)
        return OSAL_ERR_NOMEM;

    struct osal_mutex* m = &s_mutex_pool[index];
    if (osal_mutex_init(m, type, "osal_mtx") != OSAL_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, index));
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)m;
    return 0;
}

/**
 * @brief 在调用方 storage 内创建 RT-Thread 静态互斥锁
 * @param out 输出互斥锁指针
 * @param storage 存储区
 * @param storage_size 存储区字节数
 * @param type OSAL_MUTEX_PLAIN 或 OSAL_MUTEX_RECURSIVE
 * @return 0 成功; OSAL_ERR_INVAL/ISR/NOMEM 失败
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage, size_t storage_size,
                                   osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN)
        return OSAL_ERR_INVAL;
    *out = NULL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type, "osal_static") != OSAL_OK)
        return OSAL_ERR_NOMEM;

    *out = (struct osal_mutex*)m;
    return 0;
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief detach + 释放池槽
 * @param mutex 锁
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex)
        return;
    if (osal_in_isr())
        return;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    if (m->type == OSAL_MUTEX_RECURSIVE)
        rt_mutex_detach(&m->u.mutex);
    else
        rt_sem_detach(&m->u.sem);

    for (int i = 0; i < OSAL_MUTEX_POOL_SIZE; i++)
    {
        if (&s_mutex_pool[i] == m)
        {
            COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, i));
            break;
        }
    }
}

/**
 * @brief rt_mutex_take/rt_sem_take
 * @param mutex 锁
 * @param timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    if (m->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_take(&m->u.mutex, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
    return rt_sem_take(&m->u.sem, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief rt_mutex_release/rt_sem_release
 * @param mutex 锁
 * @return OSAL_OK 或 IO
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    if (m->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_release(&m->u.mutex) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
    return rt_sem_release(&m->u.sem) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
}

/* ── 二值信号量 ── */
struct osal_sem
{
    struct rt_semaphore sem; /**< RT-Thread 信号量 */
    bool from_pool; /**< 是否来自静态池 */
    bool inited; /**< 是否已初始化 */
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE, "OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t s_sem_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 * @details 上电时通过 pre_execution 调用 osal_pool_init 初始化二值信号量池
 */
pre_execution(151) static void osal_sem_pool_boot_init(void)
{
    osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE);
}

/**
 * @brief 初始化二值信号量
 * @param sem 二值信号量指针
 * @return 结果
 * @details 初始化二值信号量时, 使用 rt_sem_init 初始化二值信号量
 */
static int osal_sem_init_binary(struct osal_sem* sem)
{
    if (!sem)
        return OSAL_ERR_INVAL;

    if (rt_sem_init(&sem->sem, "osal", 0, RT_IPC_FLAG_PRIO) != RT_EOK)
        return OSAL_ERR_NOMEM;

    sem->inited = true;
    return 0;
}

/**
 * @brief 池化 rt_sem
 * @param out 输出
 * @return 0 或错误码
 */
int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_sem* sem = &s_sem_pool[idx];
    if (osal_sem_init_binary(sem) != OSAL_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
        return OSAL_ERR_NOMEM;
    }

    sem->from_pool = true;
    *out = sem;
    return 0;
}

/**
 * @brief 静态 rt_sem
 * @param out 输出
 * @param storage 存储
 * @param storage_size 大小
 * @return 0 或错误码
 */
int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return OSAL_ERR_INVAL;

    struct osal_sem* sem = (struct osal_sem*)storage;
    if (osal_sem_init_binary(sem) != OSAL_OK)
        return OSAL_ERR_NOMEM;

    sem->from_pool = false;
    *out = sem;
    return 0;
}

/**
 * @brief rt_sem_detach
 * @param sem 信号量
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem || !sem->inited)
        return;

    rt_sem_detach(&sem->sem);
    sem->inited = false;

    if (sem->from_pool)
    {
        for (size_t i = 0; i < OSAL_SEM_POOL_SIZE; i++)
        {
            if (&s_sem_pool[i] == sem)
            {
                COMPAT_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, (int)i));
                break;
            }
        }
    }
}

/**
 * @brief rt_sem_take
 * @param sem 信号量
 * @param timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem || !sem->inited || osal_in_isr())
        return OSAL_ERR_ISR;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_sem_take(&sem->sem, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief rt_sem_release 任务态
 * @param sem 信号量
 * @return true
 */
bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem || !sem->inited || osal_in_isr())
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

/**
 * @brief rt_sem_release
 * @param sem 信号量
 * @param px_yield_required 忽略
 * @return true
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    (void)px_yield_required;

    if (!sem || !sem->inited)
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

/**
 * @brief 无 yield
 * @param yield_required 忽略
 */
void osal_yield_from_isr(bool yield_required) { (void)yield_required; }

/* ── 任务创建 (无句柄, 创建后自动启动) ── */
/**
 * @brief rt_thread_create + startup 创建并启动任务
 * @param name 线程名
 * @param stack_size 栈大小 (字节)
 * @param priority 优先级 (0=最高)
 * @param entry 入口函数
 * @param param 入口参数
 * @param core_id SMP 时绑核 ID
 * @return 0 成功; OSAL_ERR_INVAL 失败
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority,
                     osal_task_entry_t entry, void* param, int core_id)
{
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param, stack_size, priority, 10);
    if (!thread)
        return OSAL_ERR_INVAL;

#ifdef RT_USING_SMP
    if (core_id >= 0)
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
#else
    (void)core_id;
#endif

    rt_thread_startup(thread);
    return 0;
}

/* ── 任务句柄 API ── */
/**
 * @brief 创建 RT-Thread 线程并返回句柄 (已 startup)
 * @param name 线程名
 * @param stack_size 栈大小 (字节)
 * @param priority 优先级
 * @param entry 入口
 * @param param 参数
 * @param core_id 绑核 ID
 * @param out_handle 输出线程句柄
 * @return 0 成功; OSAL_ERR_INVAL 失败
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority,
                            osal_task_entry_t entry, void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle)
        return OSAL_ERR_INVAL;
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param, stack_size, priority, 10);
    if (!thread)
        return OSAL_ERR_INVAL;

#ifdef RT_USING_SMP
    if (core_id >= 0)
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
#else
    (void)core_id;
#endif

    rt_thread_startup(thread);
    *out_handle = (osal_task_handle_t)thread;
    return 0;
}

/**
 * @brief rt_thread_delete self
 */
void osal_task_self_delete(void)
{
    rt_thread_delete(rt_thread_self());
    rt_schedule();
}

/**
 * @brief rt_thread_delete
 * @param task 句柄
 */
void osal_task_delete(osal_task_handle_t task)
{
    if (!task)
        return;
    rt_thread_delete((rt_thread_t)task);
}

/**
 * @brief 线程状态非 CLOSE/INIT
 * @param task 句柄
 * @return true
 */
bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task)
        return false;
    rt_uint8_t stat = RT_SCHED_CTX((rt_thread_t)task).stat & RT_THREAD_STAT_MASK;
    return stat != RT_THREAD_CLOSE && stat != RT_THREAD_INIT;
}

/**
 * @brief rt_object 名称
 * @param task 句柄
 * @return 名称
 */
const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task)
        return "?";
    return ((struct rt_object*)((rt_thread_t)task))->name;
}

/**
 * @brief 扫描 RT-Thread 线程栈填充字节, 估算剩余空闲栈
 * @param thread RT-Thread 线程句柄
 * @return 从栈底起连续 '#' 填充字节数, 即剩余空闲栈 (字节)
 */
static uint32_t osal_rtt_stack_watermark(rt_thread_t thread)
{
    const uint8_t* stack = (const uint8_t*)thread->stack_addr;
    uint32_t size = thread->stack_size;
    uint32_t count = 0;
    for (uint32_t i = 0; i < size; i++)
        if (stack[i] == '#')
            count++;
        else
            break;
    return count; /* 剩余空闲栈 (字节) */
}

/**
 * @brief 扫描 '#' 栈填充
 * @param task 句柄
 * @return 剩余字节
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task)
        return 0;
    return osal_rtt_stack_watermark((rt_thread_t)task);
}

/* ── 队列 (基于 rt_mq 消息队列, 支持任意定长消息) ── */
#ifdef RT_USING_MESSAGEQUEUE
struct osal_queue_obj
{
    rt_mq_t mq; /**< RT-Thread 消息队列句柄 */
    size_t item_size; /**< 消息项大小 (字节) */
};

/**
 * @brief rt_mq_create (MESSAGEQUEUE 启用)
 * @param queue_len 长度
 * @param item_size 大小
 * @return 句柄或 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    rtt_heap_init_once();

    struct osal_queue_obj* q = rt_malloc(sizeof(struct osal_queue_obj));
    if (!q)
        return NULL;

    q->mq = rt_mq_create("osmq", item_size, queue_len, RT_IPC_FLAG_PRIO);
    if (!q->mq)
    {
        rt_free(q);
        return NULL;
    }
    q->item_size = item_size;
    return (osal_queue_handle_t)q;
}

/**
 * @brief rt_mq_delete + free
 * @param queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
    if (!queue)
        return;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    rt_mq_delete(q->mq);
    rt_free(q);
}

/**
 * @brief rt_mq_send_wait
 * @param queue 句柄
 * @param item 数据
 * @param timeout_ms 超时
 * @return true
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr())
        return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_send_wait(q->mq, item, q->item_size, ticks) == RT_EOK;
}

/**
 * @brief rt_mq_send ISR/快路径发送
 * @param queue 队列句柄
 * @param item 待发送数据
 * @param px_yield_required yield 标志 (RT-Thread 忽略)
 * @return true 成功
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    (void)px_yield_required;

    if (!queue || !item)
        return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    return rt_mq_send(q->mq, item, q->item_size) == RT_EOK;
}

/**
 * @brief rt_mq_recv
 * @param queue 句柄
 * @param item 缓冲
 * @param timeout_ms 超时
 * @return true
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr())
        return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_recv(q->mq, item, q->item_size, ticks) >= 0;
}

/**
 * @brief MESSAGEQUEUE 启用时 ISR 接收 stub (当前返回 false)
 * @param queue 队列句柄 (忽略)
 * @param item 接收缓冲 (忽略)
 * @param px_yield_required yield 标志 (忽略)
 * @return false
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}
#else
/**
 * @brief rt_mq_create (MESSAGEQUEUE 启用)
 * @param queue_len 长度
 * @param item_size 大小
 * @return 句柄或 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    (void)queue_len;
    (void)item_size;
    return NULL;
}

/**
 * @brief rt_mq_delete + free
 * @param queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue) { (void)queue; }

/**
 * @brief rt_mq_send_wait
 * @param queue 句柄
 * @param item 数据
 * @param timeout_ms 超时
 * @return true
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return false;
}

/**
 * @brief MESSAGEQUEUE 未启用时的 ISR 发送 stub
 * @param queue 队列句柄 (忽略)
 * @param item 数据 (忽略)
 * @param px_yield_required yield 标志 (忽略)
 * @return false
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}

/**
 * @brief rt_mq_recv
 * @param queue 句柄
 * @param item 缓冲
 * @param timeout_ms 超时
 * @return true
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return false;
}

/**
 * @brief MESSAGEQUEUE 未启用时的 ISR 接收 stub
 * @param queue 队列句柄 (忽略)
 * @param item 缓冲 (忽略)
 * @param px_yield_required yield 标志 (忽略)
 * @return false
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}
#endif /* RT_USING_MESSAGEQUEUE */

/* ── 硬件安全关断 (weak, 板级可覆盖) ── */
/**
 * @brief 弱符号硬件安全关断 (板级未覆盖时触发 trap)
 */
COMPAT_WEAK void safety_hardware_shutdown(void) { COMPAT_TRAP(); }

/* ── Panic 安全互锁 (weak, 板级可覆盖) ── */
/**
 * @brief 弱符号 Panic 安全互锁 (板级可覆盖: 喂狗、切断执行器等)
 */
COMPAT_WEAK void osal_panic_interlock(void) {}

/* ── 调度器冻结 / 中断冻结 (单向不可恢复) ── */
/**
 * @brief 进入 RT-Thread 临界区, 冻结调度器 (单向不可恢复)
 */
void osal_sched_freeze(void) { rt_enter_critical(); }

/**
 * @brief rt_hw_interrupt_disable
 */
void osal_int_freeze(void) { rt_hw_interrupt_disable(); }

/* ── 日志 ── */
/**
 * @brief 格式化输出 OSAL 日志
 * @param level 日志级别 (当前忽略)
 * @param tag 日志标签
 * @param fmt printf 格式串
 * @param ... 格式参数
 */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    (void)level;
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("[%s] ", tag ? tag : "drv");
    vprintf(fmt, args);
    my_printf_output("\n");
    va_end(args);
}

/**
 * @brief 致命日志
 * @param fmt 格式
 * @param ... 参数
 */
void osal_log_fatal(const char* fmt, ...)
{
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[FATAL ERROR] ");
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

/**
 * @brief 断言日志
 * @param file 文件
 * @param line 行
 * @param fmt 格式
 * @param ... 参数
 */
void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
{
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[CRITICAL_ASSERT FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_RTTHREAD */
