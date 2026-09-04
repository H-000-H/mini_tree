/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file osal_rtthread.c
 *@brief osal rtthread 实现
 *@author H-000-H
 *@details
 *   osal_rtthread.c — OSAL RT-Thread 后端实现
 *   将 OSAL API 映射到 rt_mutex/rt_sem/rt_mq/rt_thread 等 RT-Thread 原语
 *   维护独立系统堆 s_rtt_heap (RTT_HEAP_SIZE, 板级可覆盖), 首次分配时惰性初始化
 *   优先级语义与 FreeRTOS 相反 (0=最高), 切换后端时需注意
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
#include <stdbool.h>
#include <stdint.h>
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

static uint8_t      s_rtt_heap[RTT_HEAP_SIZE] MINI_ALIGNED(4);
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

/* -------------------------------------------------------------------------- */
/* 互斥锁内部存储 */
/* -------------------------------------------------------------------------- */
struct osal_mutex
{
    osal_mutex_type_t type; /**< 互斥锁类型 */
    union
    {
        struct rt_mutex     mutex; /**< RT-Thread 互斥锁 */
        struct rt_semaphore sem;   /**< RT-Thread 信号量 (递归锁用) */
    } u;                           /**< 后端实现 (mutex 或 sem) */
};

/**
 * @brief 初始化互斥锁
 * @param[in] m 互斥锁指针
 * @param[in] type 互斥锁类型
 * @param[in] name 互斥锁名称
 * @return 结果
 * @details 初始化互斥锁时, 使用 rt_mutex_init 或 rt_sem_init 初始化互斥锁
 */
static int osal_mutex_init(struct osal_mutex* mutex_obj, osal_mutex_type_t type, const char* name)
{
    if (!mutex_obj)
        return OSAL_ERR_INVAL;

    mutex_obj->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_init(&mutex_obj->u.mutex, name, RT_IPC_FLAG_PRIO) == RT_EOK ? OSAL_OK : OSAL_ERR_NOMEM;
    if (type == OSAL_MUTEX_PLAIN)
        return rt_sem_init(&mutex_obj->u.sem, name, 1, RT_IPC_FLAG_PRIO) == RT_EOK ? OSAL_OK : OSAL_ERR_NOMEM;
    return OSAL_ERR_INVAL;
}

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE, "OSAL_MUTEX_STORAGE_SIZE too small");

/* -------------------------------------------------------------------------- */
/* ISR 上下文检测 */
/* -------------------------------------------------------------------------- */
/**
 * @brief rt_interrupt_get_nest()>0
 * @return 1 在 ISR
 */
int osal_in_isr(void) { return rt_interrupt_get_nest() > 0; }

/* -------------------------------------------------------------------------- */
/* Spinlock (复用内核 struct rt_spinlock + rt_spin_lock_* ) */
/* -------------------------------------------------------------------------- */
/**
 * @details CONFIG_OSAL_SPINLOCK_IRQ_DISABLE (默认): 内嵌内核
 *          struct rt_spinlock 并转发到 rt_spin_lock_irqsave /
 *          rt_spin_unlock_irqrestore (UP 版实现: 关中断 + 锁调度器,
 *          见 src/cpu_up.c); SMP 时同一套 API 由 cpuport.h 的
 *          rt_hw_spinlock_t 接管, 本层无需分支。
 *          CONFIG_OSAL_SPINLOCK_ATOMIC: 内核未对外暴露裸自旋对象,
 *          用原子 test-and-set 忙等 (仅适合 SMP)。
 * @note 嵌套处理: 内层用 rt_spin_lock/rt_spin_unlock (只动调度器
 *       nest, 不碰中断), 仅最外层走 irqsave/irqrestore 并记住恢复点;
 *       两边 nest 严格配对。旧写法直接存 rt_hw_interrupt_disable()
 *       的返回值且无嵌套计数, 重入 lock 会覆盖 level 导致提前开中断。
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    struct rt_spinlock lock;  /**< RT-Thread 内核自旋锁对象 */
    rt_base_t          level; /**< 最外层加锁时保存的中断状态 */
    uint32_t           nest;  /**< 嵌套深度 */
#else
    volatile int locked; /**< 原子锁标志 (0=空闲, 1=持有) */
#endif
};

_Static_assert(sizeof(struct osal_spinlock) <= OSAL_SPINLOCK_STORAGE_SIZE, "osal_rtthread: OSAL_SPINLOCK_STORAGE_SIZE too small");

/**
 * @brief 初始化自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    rt_spin_lock_init(&lock->lock);
    lock->level = 0;
    lock->nest = 0U;
#else
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 获取自旋锁 (ISR 安全, 禁止睡眠)
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 * @warning 持锁区间内中断已屏蔽 (且调度器已锁), 禁止调用任何可能
 *          阻塞的 API (互斥锁/信号量/邮箱/延时)。
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    if (lock->nest == 0U)
        lock->level = rt_spin_lock_irqsave(&lock->lock);
    else
        rt_spin_lock(&lock->lock); /* 内层: 只加调度器锁, 中断已由最外层关闭 */
    lock->nest++;
#else
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
        ;
#endif
    return OSAL_OK;
}

/**
 * @brief 释放自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空或未加锁返回 OSAL_ERR_INVAL
 * @note 回到最外层时才恢复中断; 若持锁期间有更高优先级线程就绪,
 *       rt_exit_critical() 可能在调度器锁释放时触发一次调度。
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    if (lock->nest == 0U)
        return OSAL_ERR_INVAL; /* 未加锁却解锁 */
    lock->nest--;
    if (lock->nest == 0U)
        rt_spin_unlock_irqrestore(&lock->lock, lock->level);
    else
        rt_spin_unlock(&lock->lock);
#else
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 检查自旋锁是否被持有
 * @param[in] lock 自旋锁指针
 * @return true 已持有; false 未持有或 lock 为空
 * @note 仅作诊断用: 内核 rt_spinlock 不对外暴露持有状态,
 *       临界区模式看本层嵌套计数, 原子模式看 locked。
 */
MINI_UNUSED MINI_STATIC_INLINE bool osal_spinlock_is_locked(struct osal_spinlock* lock)
{
    if (!lock)
        return false;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    return lock->nest != 0U;
#else
    return __atomic_load_n(&lock->locked, __ATOMIC_ACQUIRE) != 0;
#endif
}

/* -------------------------------------------------------------------------- */
/* 静态互斥锁池 */
/* -------------------------------------------------------------------------- */
static struct osal_mutex             s_mutex_pool[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_mutex_used[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_mutex_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 * @details 上电时通过 mini_pre_execution 调用 osal_pool_init 初始化互斥锁池控制结构体
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void osal_mutex_pool_boot_init(void)
{
    osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE);
}

/**
 * @brief 初始化池
 * @param[in] pool 池
 * @param[in] buffer 数组
 * @param[in] count 数量
 * @return 0 或 INVAL
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* buffer, size_t count)
{
    if (!pool || !buffer || count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = buffer;
    pool->slot_count = count;

    for (size_t iter_index = 0; iter_index < count; iter_index++)
        buffer[iter_index] = 0;

    return 0;
}

/**
 * @brief 随机起点扫描申请
 * @param[in] pool 池
 * @return 索引
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;
    int       claimed_index = -1;
    rt_base_t level = rt_hw_interrupt_disable();
    for (size_t iter_index = 0; iter_index < pool->slot_count; iter_index++)
    {
        if (!pool->used_slots[iter_index])
        {
            pool->used_slots[iter_index] = 1;
            claimed_index = (int)iter_index;
            break;
        }
    }
    rt_hw_interrupt_enable(level);
    return claimed_index;
}

/**
 * @brief 释放槽
 * @param[in] pool 池
 * @param[in] slot_index 索引
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

/* -------------------------------------------------------------------------- */
/* 时间 */
/* -------------------------------------------------------------------------- */
/**
 * @brief rt_tick_get 转 ms
 * @return 毫秒
 */
uint32_t osal_time_ms(void) { return rt_tick_get() * 1000 / RT_TICK_PER_SECOND; }

/**
 * @brief rt_thread_mdelay
 * @param[in] ms 毫秒
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
        volatile uint32_t loops = us * 8U;
        while (loops-- > 0U)
            ;
    }
#endif
}

/**
 * @brief rt_tick_from_millisecond
 * @param[in] ms 毫秒
 * @return tick
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms) { return rt_tick_from_millisecond(ms); }

/**
 * @brief 超时转 tick
 * @param[in] timeout_ms 毫秒
 * @return tick
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return RT_WAITING_FOREVER;
    return rt_tick_from_millisecond(timeout_ms);
}

/* -------------------------------------------------------------------------- */
/* 内存 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 从 RT-Thread 系统堆分配并清零内存
 * @param[in] count 元素个数
 * @param[in] size 每个元素字节数
 * @return 成功返回指针, 失败返回 NULL
 */
void* osal_malloc(size_t size)
{
    rtt_heap_init_once();
    return rt_malloc(size);
}

void* osal_calloc(size_t count, size_t size)
{
    rtt_heap_init_once();
    return rt_calloc(count, size);
}

/**
 * @brief rt_free
 * @param[in] ptr 指针
 * @return OSAL_OK
 */
int osal_free(void* ptr)
{
    rt_free(ptr);
    return OSAL_OK;
}

/* -------------------------------------------------------------------------- */
/* 互斥锁 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 从静态池创建指定类型的 RT-Thread 互斥锁
 * @param[out] out 输出互斥锁指针
 * @param[in] type OSAL_MUTEX_PLAIN 或 OSAL_MUTEX_RECURSIVE
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

    struct osal_mutex* mutex_obj = &s_mutex_pool[index];
    if (osal_mutex_init(mutex_obj, type, "osal_mtx") != OSAL_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, index));
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)mutex_obj;
    return 0;
}

/**
 * @brief 在调用方 storage 内创建 RT-Thread 静态互斥锁
 * @param[out] out 输出互斥锁指针
 * @param[in] storage 存储区
 * @param[in] storage_size 存储区字节数
 * @param[in] type OSAL_MUTEX_PLAIN 或 OSAL_MUTEX_RECURSIVE
 * @return 0 成功; OSAL_ERR_INVAL/ISR/NOMEM 失败
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage, size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN)
        return OSAL_ERR_INVAL;
    *out = NULL;

    struct osal_mutex* mutex_obj = (struct osal_mutex*)storage;
    if (osal_mutex_init(mutex_obj, type, "osal_static") != OSAL_OK)
        return OSAL_ERR_NOMEM;

    *out = (struct osal_mutex*)mutex_obj;
    return 0;
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param[out] out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param[out] out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param[out] out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_recursive(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE); }

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param[out] out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param[out] out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_plain(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief RT-Thread mutex/sem 互斥锁
 * @param[out] out 等见签名
 * @return 0 或错误码
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief detach + 释放池槽
 * @param[in] mutex 锁
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex)
        return;
    if (osal_in_isr())
        return;
    struct osal_mutex* mutex_obj = (struct osal_mutex*)mutex;
    if (mutex_obj->type == OSAL_MUTEX_RECURSIVE)
        rt_mutex_detach(&mutex_obj->u.mutex);
    else
        rt_sem_detach(&mutex_obj->u.sem);

    for (int iter_index = 0; iter_index < OSAL_MUTEX_POOL_SIZE; iter_index++)
    {
        if (&s_mutex_pool[iter_index] == mutex_obj)
        {
            MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, iter_index));
            break;
        }
    }
}

/**
 * @brief rt_mutex_take/rt_sem_take
 * @param[in] mutex 锁
 * @param[in] timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    struct osal_mutex* mutex_obj = (struct osal_mutex*)mutex;
    osal_tick_t        ticks = osal_timeout_to_ticks(timeout_ms);
    if (mutex_obj->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_take(&mutex_obj->u.mutex, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
    return rt_sem_take(&mutex_obj->u.sem, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief rt_mutex_release/rt_sem_release
 * @param[in] mutex 锁
 * @return OSAL_OK 或 IO
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    struct osal_mutex* mutex_obj = (struct osal_mutex*)mutex;
    if (mutex_obj->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_release(&mutex_obj->u.mutex) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
    return rt_sem_release(&mutex_obj->u.sem) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
}

/* -------------------------------------------------------------------------- */
/* 二值信号量 */
/* -------------------------------------------------------------------------- */
struct osal_sem
{
    struct rt_semaphore sem;       /**< RT-Thread 信号量 */
    bool                from_pool; /**< 是否来自静态池 */
    bool                inited;    /**< 是否已初始化 */
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE, "OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem             s_sem_pool[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                     s_sem_used[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_sem_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 * @details 上电时通过 mini_pre_execution 调用 osal_pool_init 初始化二值信号量池
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_SEM_POOL) static void osal_sem_pool_boot_init(void)
{
    osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE);
}

/**
 * @brief 初始化二值信号量
 * @param[in] sem 二值信号量指针
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
 * @param[out] out 输出
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
        MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
        return OSAL_ERR_NOMEM;
    }

    sem->from_pool = true;
    *out = sem;
    return 0;
}

/**
 * @brief 静态 rt_sem
 * @param[out] out 输出
 * @param[in] storage 存储
 * @param[in] storage_size 大小
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
 * @param[in] sem 信号量
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem || !sem->inited)
        return;

    rt_sem_detach(&sem->sem);
    sem->inited = false;

    if (sem->from_pool)
    {
        for (size_t iter_index = 0; iter_index < OSAL_SEM_POOL_SIZE; iter_index++)
        {
            if (&s_sem_pool[iter_index] == sem)
            {
                MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, (int)iter_index));
                break;
            }
        }
    }
}

/**
 * @brief rt_sem_take
 * @param[in] sem 信号量
 * @param[in] timeout_ms 超时
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
 * @param[in] sem 信号量
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
 * @param[in] sem 信号量
 * @param[in] px_yield_required 忽略
 * @return true
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);

    if (!sem || !sem->inited)
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

/**
 * @brief 无 yield
 * @param[in] yield_required 忽略
 */
void osal_yield_from_isr(bool yield_required) { MINI_UNUSED_PARAM(yield_required); }

/* -------------------------------------------------------------------------- */
/* 任务创建 (无句柄, 创建后自动启动) */
/* -------------------------------------------------------------------------- */
/**
 * @brief rt_thread_create + startup 创建并启动任务
 * @param[in] name 线程名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 优先级 (0=最高)
 * @param[in] entry 入口函数
 * @param[in] param 入口参数
 * @param[in] core_id SMP 时绑核 ID
 * @return 0 成功; OSAL_ERR_INVAL 失败
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id)
{
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param, stack_size, priority, 10);
    if (!thread)
        return OSAL_ERR_INVAL;

#ifdef RT_USING_SMP
    if (core_id >= 0)
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
#else
    MINI_UNUSED_PARAM(core_id);
#endif

    rt_thread_startup(thread);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* 任务句柄 API */
/* -------------------------------------------------------------------------- */
/**
 * @brief 创建 RT-Thread 线程并返回句柄 (已 startup)
 * @param[in] name 线程名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 优先级
 * @param[in] entry 入口
 * @param[in] param 参数
 * @param[in] core_id 绑核 ID
 * @param[out] out_handle 输出线程句柄
 * @return 0 成功; OSAL_ERR_INVAL 失败
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id,
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
    MINI_UNUSED_PARAM(core_id);
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
 * @param[in] task 句柄
 */
void osal_task_delete(osal_task_handle_t task)
{
    if (!task)
        return;
    rt_thread_delete((rt_thread_t)task);
}

/**
 * @brief 启动 RT-Thread 调度器
 * @details 转发 rt_system_scheduler_start(); 应在所有 osal_task_create() 之后调用,
 *          正常情况下永不返回 (控制权交给内核调度器).
 */
void osal_scheduler_start(void) { rt_system_scheduler_start(); }

/**
 * @brief 线程状态非 CLOSE/INIT
 * @param[in] task 句柄
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
 * @param[in] task 句柄
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
 * @param[in] thread RT-Thread 线程句柄
 * @return 从栈底起连续 '#' 填充字节数, 即剩余空闲栈 (字节)
 */
static uint32_t osal_rtt_stack_watermark(rt_thread_t thread)
{
    const uint8_t* stack = (const uint8_t*)thread->stack_addr;
    uint32_t       size = thread->stack_size;
    uint32_t       count = 0;
    for (uint32_t iter_index = 0; iter_index < size; iter_index++)
        if (stack[iter_index] == '#')
            count++;
        else
            break;
    return count; /* 剩余空闲栈 (字节) */
}

/**
 * @brief 扫描 '#' 栈填充
 * @param[out] task 句柄
 * @return 剩余字节
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task)
        return 0;
    return osal_rtt_stack_watermark((rt_thread_t)task);
}

/* -------------------------------------------------------------------------- */
/* 队列 (基于 rt_mq 消息队列, 支持任意定长消息) */
/* -------------------------------------------------------------------------- */
#ifdef RT_USING_MESSAGEQUEUE
struct osal_queue_obj
{
    rt_mq_t mq;        /**< RT-Thread 消息队列句柄 */
    size_t  item_size; /**< 消息项大小 (字节) */
};

/**
 * @brief rt_mq_create (MESSAGEQUEUE 启用)
 * @param[in] queue_len 长度
 * @param[in] item_size 大小
 * @return 句柄或 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    rtt_heap_init_once();

    struct osal_queue_obj* queue_obj = rt_malloc(sizeof(struct osal_queue_obj));
    if (!queue_obj)
        return NULL;

    queue_obj->mq = rt_mq_create("osmq", item_size, queue_len, RT_IPC_FLAG_PRIO);
    if (!queue_obj->mq)
    {
        rt_free(queue_obj);
        return NULL;
    }
    queue_obj->item_size = item_size;
    return (osal_queue_handle_t)queue_obj;
}

/**
 * @brief rt_mq_delete + free
 * @param[in] queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
    if (!queue)
        return;
    struct osal_queue_obj* queue_obj = (struct osal_queue_obj*)queue;
    rt_mq_delete(queue_obj->mq);
    rt_free(queue_obj);
}

/**
 * @brief rt_mq_send_wait
 * @param[in] queue 句柄
 * @param[in] item 数据
 * @param[in] timeout_ms 超时
 * @return true
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr())
        return false;
    struct osal_queue_obj* queue_obj = (struct osal_queue_obj*)queue;
    osal_tick_t            ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_send_wait(queue_obj->mq, item, queue_obj->item_size, ticks) == RT_EOK;
}

/**
 * @brief rt_mq_send ISR/快路径发送
 * @param[in] queue 队列句柄
 * @param[in] item 待发送数据
 * @param[in] px_yield_required yield 标志 (RT-Thread 忽略)
 * @return true 成功
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);

    if (!queue || !item)
        return false;
    struct osal_queue_obj* queue_obj = (struct osal_queue_obj*)queue;
    return rt_mq_send(queue_obj->mq, item, queue_obj->item_size) == RT_EOK;
}

/**
 * @brief rt_mq_recv
 * @param[out] queue 句柄
 * @param[out] item 缓冲
 * @param[in] timeout_ms 超时
 * @return true
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr())
        return false;
    struct osal_queue_obj* queue_obj = (struct osal_queue_obj*)queue;
    osal_tick_t            ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_recv(queue_obj->mq, item, queue_obj->item_size, ticks) >= 0;
}

/**
 * @brief MESSAGEQUEUE 启用时 ISR 接收 stub (当前返回 false)
 * @param[out] queue 队列句柄 (忽略)
 * @param[out] item 接收缓冲 (忽略)
 * @param[out] px_yield_required yield 标志 (忽略)
 * @return false
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
}
#else
/**
 * @brief rt_mq_create (MESSAGEQUEUE 启用)
 * @param[in] queue_len 长度
 * @param[in] item_size 大小
 * @return 句柄或 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    MINI_UNUSED_PARAM(queue_len);
    MINI_UNUSED_PARAM(item_size);
    return NULL;
}

/**
 * @brief rt_mq_delete + free
 * @param[in] queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue) { MINI_UNUSED_PARAM(queue); }

/**
 * @brief rt_mq_send_wait
 * @param[in] queue 句柄
 * @param[in] item 数据
 * @param[in] timeout_ms 超时
 * @return true
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    MINI_UNUSED_PARAM(timeout_ms);
    return false;
}

/**
 * @brief MESSAGEQUEUE 未启用时的 ISR 发送 stub
 * @param[in] queue 队列句柄 (忽略)
 * @param[in] item 数据 (忽略)
 * @param[in] px_yield_required yield 标志 (忽略)
 * @return false
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
}

/**
 * @brief rt_mq_recv
 * @param[out] queue 句柄
 * @param[out] item 缓冲
 * @param[in] timeout_ms 超时
 * @return true
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    MINI_UNUSED_PARAM(timeout_ms);
    return false;
}

/**
 * @brief MESSAGEQUEUE 未启用时的 ISR 接收 stub
 * @param[out] queue 队列句柄 (忽略)
 * @param[out] item 缓冲 (忽略)
 * @param[out] px_yield_required yield 标志 (忽略)
 * @return false
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
}
#endif /* RT_USING_MESSAGEQUEUE */

/* -------------------------------------------------------------------------- */
/* 事件集 (CONFIG_OSAL_EVENT 门控, 映射 rt_event_*) */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_OSAL_EVENT
/* Kconfig 的 CONFIG_OSAL_EVENT 会 select RTTHREAD_EVENT, 后者在 rtconfig.h
 * 里展开成 RT_USING_EVENT; 两者不一致时 src/ipc.c 的 rt_event_* 实现段
 * 整体不编译, 下面全部符号会在链接期缺失, 不如在这里直接 fail-fast。 */
#ifndef RT_USING_EVENT
#error "osal_rtthread: CONFIG_OSAL_EVENT requires CONFIG_RTTHREAD_EVENT (RT_USING_EVENT)"
#endif

/**
 * @brief OSAL 事件组对象
 * @details 内嵌内核 struct rt_event 并用 rt_event_init 做静态初始化,
 *          与其他 IPC 对象一致, 不走 rt_event_create (堆)。
 *          mode / auto_clear 必须在创建期存下来: RT-Thread 把 AND/OR/CLEAR
 *          做成了 rt_event_recv 的等待期 option 位, 而抽象层按 mini-os
 *          的创建期语义固定, 所以等待时再拼成 option。
 */
struct osal_event
{
    struct rt_event   obj;        /**< RT-Thread 事件集对象 (静态内嵌) */
    osal_event_mode_t mode;       /**< 创建期固定的 AND/OR 模式 */
    bool              auto_clear; /**< 创建期固定的自动消费标志 */
    bool              inited;     /**< rt_event_init 已成功 (防重复 detach) */
    bool              from_pool;  /**< 是否来自静态池 */
};

_Static_assert(sizeof(struct osal_event) <= OSAL_EVENT_STORAGE_SIZE, "OSAL_EVENT_STORAGE_SIZE too small");

/**
 * @brief 静态事件组池
 */
static struct osal_event             s_event_pool[OSAL_EVENT_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_event_used[OSAL_EVENT_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_event_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化事件组池
 * @details 上电时通过 mini_pre_execution 调用 osal_pool_init 初始化事件组池
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_EVENT_POOL) static void osal_event_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_event_pool_ctrl, s_event_used, OSAL_EVENT_POOL_SIZE));
}

/**
 * @brief 校验标志掩码 (非 0 且不越出 OSAL 可用位区)
 * @param[in] bits 待校验掩码
 * @return true 合法
 * @details RT-Thread 的 event->set 是全 32 位可用的, 但抽象层按四后端
 *          最小公约数 (FreeRTOS 只剩 bit0..23) 收口, 保证业务代码换后端
 *          不改行为; 见 osal.h 的 OSAL_EVENT_BITS / OSAL_EVENT_MASK。
 */
MINI_STATIC_INLINE bool osal_event_bits_valid(uint32_t bits) { return (bits != 0U) && ((bits & ~OSAL_EVENT_MASK) == 0U); }

/**
 * @brief 把创建期的 mode / auto_clear 拼成 rt_event_recv 的 option
 * @param[in] ev 事件组
 * @return RT_EVENT_FLAG_AND 或 RT_EVENT_FLAG_OR, 按需叠加 RT_EVENT_FLAG_CLEAR
 */
MINI_STATIC_INLINE rt_uint8_t osal_rtt_event_option(const struct osal_event* ev)
{
    rt_uint8_t opt = (rt_uint8_t)((ev->mode == OSAL_EVENT_AND) ? RT_EVENT_FLAG_AND : RT_EVENT_FLAG_OR);
    if (ev->auto_clear)
        opt = (rt_uint8_t)(opt | RT_EVENT_FLAG_CLEAR);
    return opt;
}

/**
 * @brief 在调用方存储上初始化事件集
 * @param[in] ev 事件组对象
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后是否自动消费已满足的位
 * @return OSAL_OK; ev 为空或 mode 非法返回 OSAL_ERR_INVAL;
 *         内核初始化失败返回 OSAL_ERR_NOMEM
 */
static int osal_event_init(struct osal_event* ev, osal_event_mode_t mode, bool auto_clear)
{
    if (!ev)
        return OSAL_ERR_INVAL;
    if (mode != OSAL_EVENT_OR && mode != OSAL_EVENT_AND)
        return OSAL_ERR_INVAL;

    /**< RT_IPC_FLAG_PRIO: 等待者按优先级排队 (与互斥锁/信号量一致) */
    if (rt_event_init(&ev->obj, "osal", RT_IPC_FLAG_PRIO) != RT_EOK)
        return OSAL_ERR_NOMEM;

    ev->inited = true;
    ev->mode = mode;
    ev->auto_clear = auto_clear;
    return OSAL_OK;
}

/**
 * @brief 池化事件组
 * @param[out] out 输出
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后自动消费已满足的位
 * @return OSAL_OK 或错误码
 */
int osal_event_create(struct osal_event** out, osal_event_mode_t mode, bool auto_clear)
{
    if (!out)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_event_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_event* ev = &s_event_pool[idx];
    int                rc = osal_event_init(ev, mode, auto_clear);
    if (rc != OSAL_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_event_pool_ctrl, idx));
        return rc;
    }

    ev->from_pool = true;
    *out = ev;
    return OSAL_OK;
}

/**
 * @brief 静态存储事件组
 * @param[out] out 输出
 * @param[in] storage 存储
 * @param[in] storage_size 大小
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后自动消费已满足的位
 * @return OSAL_OK 或错误码
 */
int osal_event_create_static(struct osal_event** out, void* storage, size_t storage_size, osal_event_mode_t mode, bool auto_clear)
{
    if (!out || !storage || storage_size < sizeof(struct osal_event))
        return OSAL_ERR_INVAL;

    struct osal_event* ev = (struct osal_event*)storage;
    int                rc = osal_event_init(ev, mode, auto_clear);
    if (rc != OSAL_OK)
        return rc;

    ev->from_pool = false;
    *out = ev;
    return OSAL_OK;
}

/**
 * @brief 销毁事件组并归还池槽
 * @param[in] ev 事件组
 * @details rt_event_detach 内部会以 -RT_ERROR 恢复所有挂起线程后再从
 *          对象容器摘除, 因此不存在遗留等待者 (与 mini-os 后端需要
 *          自己判忙不同)。
 */
void osal_event_destroy(struct osal_event* ev)
{
    if (!ev || osal_in_isr())
        return;

    if (ev->inited)
    {
        rt_event_detach(&ev->obj);
        ev->inited = false;
    }

    if (ev->from_pool && ev >= s_event_pool && ev < &s_event_pool[OSAL_EVENT_POOL_SIZE])
    {
        int idx = (int)(ev - s_event_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_event_pool_ctrl, idx));
    }
}

/**
 * @brief 置位事件标志 (task 上下文)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0, 仅 bit0..23)
 * @return OSAL_OK 或错误码
 * @details rt_event_send 本身就是 event->set |= set 并唤醒所有已满足的
 *          等待者, 与抽象层契约一致。
 */
int osal_event_set(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !ev->inited || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    return rt_event_send(&ev->obj, (rt_uint32_t)bits) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
}

/**
 * @brief 置位事件标志 (ISR 上下文, 不内部 yield)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0, 仅 bit0..23)
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return OSAL_OK 或错误码
 * @details rt_event_send 在 ISR 里也能用: 它末尾的 rt_schedule() 会看
 *          中断嵌套计数, 在中断里只记下"需要切换", 真正的上下文切换
 *          由硬件异常返回时完成, 因此本层不需要也不应该主动 yield。
 * @note 内核不上报是否唤醒了更高优先级线程, 故 px_yield_required 保持不动;
 *       与 osal_sem_post_from_isr / osal_yield_from_isr 在本后端的空实现一致。
 */
int osal_event_set_from_isr(struct osal_event* ev, uint32_t bits, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);

    if (!ev || !ev->inited || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    return rt_event_send(&ev->obj, (rt_uint32_t)bits) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
}

/**
 * @brief 清除事件标志
 * @param[in] ev 事件组
 * @param[in] bits 要清除的掩码 (> 0, 仅 bit0..23)
 * @return OSAL_OK 或错误码
 * @details RT-Thread 没对外提供"清除指定位"的 API (只有 recv 带
 *          RT_EVENT_FLAG_CLEAR 时清除已收到的位, 以及 rt_event_control 的
 *          RESET 清全部), 因此这里沿用内核 rt_event_recv 自己的写法:
 *          在事件集自旋锁内直接改 event->set。
 *          不唤醒不阻塞, task 与 ISR 均可调用。
 */
int osal_event_clear(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !ev->inited || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    rt_base_t level = rt_spin_lock_irqsave(&ev->obj.spinlock);
    ev->obj.set &= ~(rt_uint32_t)bits;
    rt_spin_unlock_irqrestore(&ev->obj.spinlock, level);
    return OSAL_OK;
}

/**
 * @brief 读取当前事件标志 (不阻塞、不消费)
 * @param[in] ev 事件组
 * @param[out] out_bits 回传当前标志 (可为 NULL, 则仅做存在性检查)
 * @return OSAL_OK 或错误码
 * @details 在事件集自旋锁内读 set, 不唤醒不阻塞, task 与 ISR 均可调用。
 */
int osal_event_get(struct osal_event* ev, uint32_t* out_bits)
{
    if (!ev || !ev->inited)
        return OSAL_ERR_INVAL;
    if (out_bits == NULL)
        return OSAL_OK;

    rt_base_t   level = rt_spin_lock_irqsave(&ev->obj.spinlock);
    rt_uint32_t cur = ev->obj.set;
    rt_spin_unlock_irqrestore(&ev->obj.spinlock, level);

    *out_bits = (uint32_t)cur;
    return OSAL_OK;
}

/**
 * @brief 等待事件标志
 * @param[in] ev 事件组
 * @param[in] bits 等待的掩码 (> 0, 仅 bit0..23)
 * @param[in] timeout_ms 超时毫秒 (0 = 不阻塞, OSAL_WAIT_FOREVER = 永久)
 * @param[out] out_bits 回传实际已置位的相关位 (可为 NULL)
 * @return 满足 OSAL_OK; 未满足/超时 OSAL_ERR_TIMEOUT; 参数非法 OSAL_ERR_INVAL
 * @details 创建期存的 mode / auto_clear 在这里转成内核 option 位
 *          RT_EVENT_FLAG_AND/OR 与 RT_EVENT_FLAG_CLEAR, 四后端行为一致。
 *          内核成功时 *recved = event->set & set (自动清位发生在取值之后),
 *          正好是抽象层要的"实际已置位的相关位"。
 */
int osal_event_wait(struct osal_event* ev, uint32_t bits, uint32_t timeout_ms, uint32_t* out_bits)
{
    if (!ev || !ev->inited || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    rt_uint32_t recved = 0U;
    rt_err_t    rc = rt_event_recv(&ev->obj, (rt_uint32_t)bits, osal_rtt_event_option(ev), (rt_int32_t)osal_timeout_to_ticks(timeout_ms),
                                   (out_bits != NULL) ? &recved : RT_NULL);

    if (out_bits != NULL)
        *out_bits = (uint32_t)recved;

    if (rc == RT_EOK)
        return OSAL_OK;
    /**< 非阻塞未满足与有限超时到期内核都报 -RT_ETIMEOUT */
    if (rc == -RT_ETIMEOUT)
        return OSAL_ERR_TIMEOUT;
    return OSAL_ERR_IO;
}
#endif /* CONFIG_OSAL_EVENT */

/* -------------------------------------------------------------------------- */
/* 硬件安全关断 (weak, 板级可覆盖) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 弱符号硬件安全关断 (板级未覆盖时触发 trap)
 */
MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }

/* -------------------------------------------------------------------------- */
/* Panic 安全互锁 (weak, 板级可覆盖) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 弱符号 Panic 安全互锁 (板级可覆盖: 喂狗、切断执行器等)
 */
MINI_WEAK void osal_panic_interlock(void) {}

/* -------------------------------------------------------------------------- */
/* 调度器冻结 / 中断冻结 (单向不可恢复) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 进入 RT-Thread 临界区, 冻结调度器 (单向不可恢复)
 */
void osal_sched_freeze(void) { rt_enter_critical(); }

/**
 * @brief rt_hw_interrupt_disable
 */
void osal_int_freeze(void) { rt_hw_interrupt_disable(); }

/* -------------------------------------------------------------------------- */
/* 日志 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 格式化输出 OSAL 日志
 * @param[in] level 日志级别 (当前忽略)
 * @param[in] tag 日志标签
 * @param[in] fmt printf 格式串
 * @param ... 格式参数
 */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    MINI_UNUSED_PARAM(level);
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
 * @param[in] fmt 格式
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
 * @param[in] file 文件
 * @param[in] line 行
 * @param[in] fmt 格式
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
