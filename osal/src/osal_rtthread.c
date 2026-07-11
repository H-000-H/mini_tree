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

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"

#include <rtthread.h>
#include <rthw.h>

#include <stdarg.h>
#include <stdlib.h>
#include "compiler_compat_poison.h"

/*
 * 最小堆大小 — 用户工程可在 board_config.h 中用 RTT_HEAP_SIZE 覆盖.
 * 实际线程栈、IPC 对象等内存从此堆分配.
 */
#ifndef RTT_HEAP_SIZE
#define RTT_HEAP_SIZE  (32 * 1024)
#endif

static uint8_t s_rtt_heap[RTT_HEAP_SIZE] COMPAT_ALIGNED(4);
static volatile int s_rtt_heap_inited = 0;

/* 确保 RT-Thread 系统堆在第一次调用前完成初始化 */
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
    osal_mutex_type_t type;
    union
    {
        struct rt_mutex     mutex;
        struct rt_semaphore sem;
    } u;
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
    if (!m) return OSAL_ERR_INVAL;

    m->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_init(&m->u.mutex, name, RT_IPC_FLAG_PRIO) == RT_EOK ? OSAL_OK : OSAL_ERR_NOMEM;
    if (type == OSAL_MUTEX_PLAIN)
        return rt_sem_init(&m->u.sem, name, 1, RT_IPC_FLAG_PRIO) == RT_EOK ? OSAL_OK : OSAL_ERR_NOMEM;
    return OSAL_ERR_INVAL;
}

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "OSAL_MUTEX_STORAGE_SIZE too small");

/* ── ISR 上下文检测 ── */
int osal_in_isr(void)
{
    return rt_interrupt_get_nest() > 0;
}

/* ── Spinlock ── */
/**
 * @details 默认 CONFIG_OSAL_SPINLOCK_IRQ_DISABLE: 关中断临界区.
 *          CONFIG_OSAL_SPINLOCK_ATOMIC: 原子 test-and-set 忙等自旋锁 (仅适合 SMP).
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    rt_base_t level;
#else
    volatile int locked;
#endif
};

/**
 * @brief 初始化自旋锁
 * @param lock 自旋锁指针
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    lock->level = 0;
#else
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 锁定自旋锁
 * @param lock 自旋锁指针
 * @details 锁定自旋锁时, 使用 rt_hw_interrupt_disable 关闭中断
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    lock->level = rt_hw_interrupt_disable();
#else
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE));
#endif
    return OSAL_OK;
}

/**
 * @brief 解锁自旋锁
 * @param lock 自旋锁指针
 * @details 解锁自旋锁时, 使用 rt_hw_interrupt_enable 恢复中断
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
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
static uint8_t           s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t       s_mutex_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 * @return void
 * @details 初始化静态互斥锁池时, 使用 osal_pool_init 初始化互斥锁池控制结构体, 上电时执行
 */
pre_execution(150)
static void osal_mutex_pool_boot_init(void)
{
    osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE);
}

/**
 * @brief 初始化槽位池
 * @param pool 槽位池结构体指针
 * @param buffer 槽位使用情况指针
 * @param count 槽位数量
 * @return 结果
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
 * @brief 申请槽位
 * @param pool 槽位池结构体指针
 * @return 结果
 * @details 申请槽位时, 先随机选择一个起始槽位, 然后从起始槽位开始遍历, 找到第一个未使用的槽位, 然后返回槽位索引
 * @details 如果遍历完所有槽位都没有找到未使用的槽位, 则返回-1
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
 * @brief 释放槽位
 * @param pool 槽位池结构体指针
 * @param slot_index 槽位索引
 * @return 结果
 * @details 释放槽位时, 直接将槽位使用情况指针设置为0
 * @details 如果槽位索引无效, 则返回
 */
int osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 ||
        (size_t)slot_index >= pool->slot_count)
        return OSAL_ERR_INVAL;

    rt_base_t level = rt_hw_interrupt_disable();
    pool->used_slots[slot_index] = 0;
    rt_hw_interrupt_enable(level);
    return OSAL_OK;
}

/* ── 时间 ── */
/**
 * @brief 获取现在时间
 * @return 时间
 * @details 获取现在时间时, 使用 rt_tick_get 获取系统滴答数, 然后转换为毫秒
 */
uint32_t osal_time_ms(void)
{
    return rt_tick_get() * 1000 / RT_TICK_PER_SECOND;
}

/**
 * @brief 延迟毫秒
 * @param ms 延迟时间
 * @return void
 * @details 延迟毫秒时, 使用 rt_thread_mdelay 延迟
 * @details 如果在中断中, 则不延迟
 */
void osal_delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

/**
 * @brief 单纯毫秒转 tick
 * @param ms 毫秒数
 * @return 滴答数
 * @details 将毫秒转换为滴答数时, 使用 rt_tick_from_millisecond 转换
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return rt_tick_from_millisecond(ms);
}

/**
 * @brief 将毫秒转换为滴答数
 * @param timeout_ms 超时时间
 * @return 滴答数
 * @details 将毫秒转换为滴答数时, 使用 rt_tick_from_millisecond 转换
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return RT_WAITING_FOREVER;
    return rt_tick_from_millisecond(timeout_ms);
}

/* ── 内存 ── */
/**
 * @brief 分配内存
 * @param count 数量
 * @param size 大小
 * @return 内存指针
 * @details 分配内存时, 先使用 rtt_heap_init_once 初始化堆, 然后使用 rt_calloc 分配内存
 */
void* osal_calloc(size_t count, size_t size)
{
    rtt_heap_init_once();
    return rt_calloc(count, size);
}

/**
 * @brief 释放内存
 * @param ptr 内存指针
 * @return void
 * @details 释放内存时, 使用 rt_free 释放内存
 */
int osal_free(void* ptr)
{
    rt_free(ptr);
    return OSAL_OK;
}

/* ── 互斥锁 ── */
/**
 * @brief 创建互斥锁
 * @param out 互斥锁指针
 * @param type 互斥锁类型
 * @return 结果
 * @details 创建互斥锁时, 使用 osal_pool_claim 申请互斥锁池中的一个槽位, 然后使用 osal_mutex_init 初始化互斥锁
 * @details 如果申请失败, 则返回 OSAL_ERR_NOMEM
 */
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out) return OSAL_ERR_INVAL;
    if (osal_in_isr()) return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return OSAL_ERR_INVAL;
    *out = NULL;

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0) return OSAL_ERR_NOMEM;

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
 * @brief 创建静态互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @param type 互斥锁类型
 * @details 需要手动分配存储空间,不占用池化资源
 * @return 结果
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,
                                 size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) return OSAL_ERR_INVAL;
    if (osal_in_isr()) return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return OSAL_ERR_INVAL;
    *out = NULL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type, "osal_static") != OSAL_OK)
        return OSAL_ERR_NOMEM;

    *out = (struct osal_mutex*)m;
    return 0;
}

/**
 * @brief 创建互斥锁
 * @param out 互斥锁指针
 * @return 结果
 * @details 创建互斥锁时, 使用 osal_mutex_create_typed 创建互斥锁
 */
int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建静态互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @return 结果
 * @details 创建静态互斥锁时, 使用 osal_mutex_create_static_typed 创建静态互斥锁
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建递归互斥锁
 * @param out 互斥锁指针
 * @return 结果
 * @details 创建递归互斥锁时, 使用 osal_mutex_create_typed 创建递归互斥锁
 */
int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 创建静态递归互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @return 结果
 * @details 创建静态递归互斥锁时, 使用 osal_mutex_create_static_typed 创建静态递归互斥锁
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 创建普通互斥锁
 * @param out 互斥锁指针
 * @return 结果
 * @details 创建普通互斥锁时, 使用 osal_mutex_create_typed 创建普通互斥锁
 */
int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建静态普通互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @return 结果
 * @details 创建静态普通互斥锁时, 使用 osal_mutex_create_static_typed 创建静态普通互斥锁
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 销毁互斥锁
 * @param mutex 互斥锁指针
 * @return void
 * @details 销毁互斥锁时, 使用 rt_mutex_detach 或 rt_sem_detach 销毁互斥锁
 * @details 如果互斥锁为空, 则返回
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex) return;
    if (osal_in_isr()) return;
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
 * @brief 锁定互斥锁
 * @param mutex 互斥锁指针
 * @param timeout_ms 超时时间
 * @return 结果
 * @details 锁定互斥锁时, 使用 rt_mutex_take 或 rt_sem_take 锁定互斥锁
 * @details 如果互斥锁为空, 则返回 OSAL_ERR_INVAL
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex) return OSAL_ERR_INVAL;
    if (osal_in_isr()) return OSAL_ERR_ISR;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    if (m->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_take(&m->u.mutex, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
    return rt_sem_take(&m->u.sem, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief 释放互斥锁
 * @param mutex 互斥锁指针
 * @return 结果
 * @details 释放互斥锁时, 使用 rt_mutex_release 或 rt_sem_release 释放互斥锁
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex) return OSAL_ERR_INVAL;
    if (osal_in_isr()) return OSAL_ERR_ISR;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    if (m->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_release(&m->u.mutex) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
    return rt_sem_release(&m->u.sem) == RT_EOK ? OSAL_OK : OSAL_ERR_IO;
}

/* ── 二值信号量 ── */
struct osal_sem
{
    struct rt_semaphore sem;
    bool                from_pool;
    bool                inited;
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE,
               "OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t       s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t   s_sem_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 * @return void
 * @details 初始化二值信号量池时, 使用 osal_pool_init 初始化二值信号量池
 */
pre_execution(151)
static void osal_sem_pool_boot_init(void)
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
 * @brief 创建二值信号量
 * @param out 二值信号量指针
 * @return 结果
 * @details 创建二值信号量时, 使用 osal_sem_init_binary 创建二值信号量
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
 * @brief 创建静态二值信号量
 * @param out 二值信号量指针
 * @param storage 静态二值信号量存储指针
 * @param storage_size 静态二值信号量存储大小
 * @return 结果
 * @details 创建静态二值信号量时, 使用 osal_sem_init_binary 创建静态二值信号量
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
 * @brief 销毁二值信号量
 * @param sem 二值信号量指针
 * @return void
 * @details 销毁二值信号量时, 使用 rt_sem_detach 销毁二值信号量
 * @details 如果二值信号量为空, 则返回
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
 * @brief 等待二值信号量
 * @param sem 二值信号量指针
 * @param timeout_ms 超时时间
 * @return 结果
 * @details 等待二值信号量时, 使用 rt_sem_take 等待二值信号量
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem || !sem->inited || osal_in_isr())
        return OSAL_ERR_ISR;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_sem_take(&sem->sem, ticks) == RT_EOK ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief 释放二值信号量
 * @param sem 二值信号量指针
 * @return 结果
 * @details 释放二值信号量时, 使用 rt_sem_release 释放二值信号量
 */
bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem || !sem->inited || osal_in_isr())
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

/**
 * @brief 从ISR上下文释放二值信号量
 * @param sem 二值信号量指针
 * @param px_yield_required 是否需要切换
 * @return 结果
 * @details 从ISR上下文释放二值信号量时, 使用 rt_sem_release 从ISR上下文释放二值信号量
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    (void)px_yield_required;

    if (!sem || !sem->inited)
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

/**
 * @brief 从ISR上下文切换
 * @param yield_required 是否需要切换
 * @return void
 * @details RT-Thread 后端暂不执行实际切换
 */
void osal_yield_from_isr(bool yield_required)
{
    (void)yield_required;
}

/* ── 任务创建 (无句柄, 创建后自动启动) ── */
/**
 * @brief 创建任务 无法拿到任务句柄
 * @param name 任务名称
 * @param stack_size 栈大小
 * @param priority 优先级
 * @param entry 任务入口
 * @param param 任务参数
 * @param core_id 核心ID
 * @return 结果
 * @details 创建任务时, 使用 rt_thread_create 创建任务, 然后调用 rt_thread_startup 启动
 */
int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id)
{
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param,
                                          stack_size, priority, 10);
    if (!thread) return OSAL_ERR_INVAL;

#ifdef RT_USING_SMP
    if (core_id >= 0)
    {
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
    }
#else
    (void)core_id;
#endif

    rt_thread_startup(thread);
    return 0;
}

/* ── 任务句柄 API ── */
/**
 * @brief 创建任务句柄 可以拿到任务句柄
 * @param name 任务名称
 * @param stack_size 栈大小
 * @param priority 优先级
 * @param entry 任务入口
 * @param param 任务参数
 * @param core_id 核心ID
 * @param out_handle 任务句柄
 * @return 结果
 * @details 创建任务句柄时, 使用 rt_thread_create 创建任务, 然后调用 rt_thread_startup 启动
 */
int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle) return OSAL_ERR_INVAL;
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param,
                                          stack_size, priority, 10);
    if (!thread) return OSAL_ERR_INVAL;

#ifdef RT_USING_SMP
    if (core_id >= 0)
    {
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
    }
#else
    (void)core_id;
#endif

    rt_thread_startup(thread);
    *out_handle = (osal_task_handle_t)thread;
    return 0;
}

/**
 * @brief 删除当前任务
 * @return void
 * @details 删除当前任务时, 使用 rt_thread_delete 删除当前任务, 然后调用 rt_schedule 触发调度
 */
void osal_task_self_delete(void)
{
    rt_thread_delete(rt_thread_self());
    rt_schedule();
}

/**
 * @brief 删除任务
 * @param task 任务句柄
 * @return void
 * @details 删除任务时, 使用 rt_thread_delete 删除任务
 */
void osal_task_delete(osal_task_handle_t task)
{
    if (!task) return;
    rt_thread_delete((rt_thread_t)task);
}

/**
 * @brief 判断任务是否运行
 * @param task 任务句柄
 * @return 是否运行
 * @details 判断任务是否运行时, 通过读取线程状态字判断任务是否运行
 */
bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task) return false;
    rt_uint8_t stat = RT_SCHED_CTX((rt_thread_t)task).stat & RT_THREAD_STAT_MASK;
    return stat != RT_THREAD_CLOSE && stat != RT_THREAD_INIT;
}

/**
 * @brief 获取任务名称
 * @param task 任务句柄
 * @return 任务名称
 * @details 获取任务名称时, 通过读取 rt_object 名称获取任务名称
 */
const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task) return "?";
    return ((struct rt_object*)((rt_thread_t)task))->name;
}

/* RTT 使用 '#' (0x23) 填充线程栈, 从栈底扫描连续填充字节即得空闲栈大小 */
static uint32_t osal_rtt_stack_watermark(rt_thread_t thread)
{
    const uint8_t* stack = (const uint8_t*)thread->stack_addr;
    uint32_t size = thread->stack_size;
    uint32_t count = 0;
    for (uint32_t i = 0; i < size; i++)
    {
        if (stack[i] == '#')
        {
            count++;
        }
        else
        {
            break;
        }
    }
    return count; /* 剩余空闲栈 (字节) */
}

/**
 * @brief 获取任务栈水位线
 * @param task 任务句柄
 * @return 栈水位线
 * @details 获取任务栈水位线时, 使用 osal_rtt_stack_watermark 获取任务栈水位线
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task) return 0;
    return osal_rtt_stack_watermark((rt_thread_t)task);
}

/* ── 队列 (基于 rt_mq 消息队列, 支持任意定长消息) ── */
#ifdef RT_USING_MESSAGEQUEUE
struct osal_queue_obj
{
    rt_mq_t mq;
    size_t   item_size;
};

/**
 * @brief 创建队列
 * @param queue_len 队列长度
 * @param item_size 队列元素大小
 * @return 队列句柄
 * @details 创建队列时, 使用 rt_mq_create 创建队列
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    rtt_heap_init_once();

    struct osal_queue_obj* q = rt_malloc(sizeof(struct osal_queue_obj));
    if (!q) return NULL;

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
 * @brief 删除队列
 * @param queue 队列句柄
 * @return void
 * @details 删除队列时, 使用 rt_mq_delete 删除队列
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
    if (!queue) return;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    rt_mq_delete(q->mq);
    rt_free(q);
}

/**
 * @brief 发送消息到队列
 * @param queue 队列句柄
 * @param item 消息
 * @param timeout_ms 超时时间
 * @return 是否成功
 * @details 发送消息到队列时, 使用 rt_mq_send_wait 发送消息到队列
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr()) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_send_wait(q->mq, item, q->item_size, ticks) == RT_EOK;
}

/**
 * @brief 从ISR上下文发送消息到队列
 * @param queue 队列句柄
 * @param item 消息
 * @param px_yield_required 是否需要切换
 * @return 是否成功
 * @details 从ISR上下文发送消息到队列时, 使用 rt_mq_send 从ISR上下文发送消息到队列
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;

    if (!queue || !item) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    return rt_mq_send(q->mq, item, q->item_size) == RT_EOK;
}

/**
 * @brief 接收消息从队列
 * @param queue 队列句柄
 * @param item 消息
 * @param timeout_ms 超时时间
 * @return 是否成功
 * @details 接收消息从队列时, 使用 rt_mq_recv 接收消息从队列
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr()) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_recv(q->mq, item, q->item_size, ticks) >= 0;
}

/**
 * @brief 从ISR上下文接收消息从队列
 * @param queue 队列句柄
 * @param item 消息
 * @param px_yield_required 是否需要切换
 * @return 是否成功
 * @details 从ISR上下文接收消息从队列时, 当前 RT-Thread 消息队列 ISR 接收接口未启用, 直接返回 false
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}
#else
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    (void)queue_len;
    (void)item_size;
    return NULL;
}

void osal_queue_delete(osal_queue_handle_t queue)
{
    (void)queue;
}

bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return false;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return false;
}

bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}
#endif /* RT_USING_MESSAGEQUEUE */

/* ── 硬件安全关断 (weak, 板级可覆盖) ── */
/**
 * @brief 硬件安全关断 就是触发断言陷入指令
 * @return void
 * @details 硬件安全关断时, 使用 COMPAT_TRAP 硬件安全关断
 */
COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

/* ── Panic 安全互锁 (weak, 板级可覆盖) ── */
/**
 * @brief 安全互锁 自己实现
 * @return void
 * @details 安全互锁时, 使用 osal_panic_interlock 安全互锁
 */
COMPAT_WEAK void osal_panic_interlock(void)
{
}

/* ── 调度器冻结 / 中断冻结 (单向不可恢复) ── */
/**
 * @brief 调度器冻结
 * @return void
 * @details 调度器冻结时, 使用 rt_enter_critical 调度器冻结
 */
void osal_sched_freeze(void)
{
    rt_enter_critical();
}

/**
 * @brief 中断冻结 就是禁用中断
 * @return void
 * @details 中断冻结时, 使用 rt_hw_interrupt_disable 中断冻结
 */
void osal_int_freeze(void)
{
    rt_hw_interrupt_disable();
}

/* ── 日志 ── */
/**
 * @brief 日志
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 日志格式
 * @return void
 * @details 日志时, 使用 my_printf_output 日志
 */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    (void)level;
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("[%s] ", tag ? tag : "drv");
    vprintf(fmt, args);
    my_printf_output("\n");
    va_end(args);
}

/**
 * @brief 致命错误日志
 * @param fmt 日志格式
 * @return void
 * @details 致命错误日志时, 使用 my_printf_output 致命错误日志
 */
void osal_log_fatal(const char* fmt, ...)
{
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[FATAL ERROR] ");
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

/**
 * @brief 严重错误日志推荐用 __FILE__ __LINE__ 宏 替代file和line参数
 * @param file 文件名
 * @param line 行号
 * @param fmt 日志格式
 * @return void
 * @details 严重错误日志时, 使用 my_printf_output 严重错误日志
 */
void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
{
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[CRITICAL_ASSERT FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_RTTHREAD */
