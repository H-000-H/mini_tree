/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file osal_null.c
 *@brief 裸机适配层 (无 RTOS) 实现
 *@author H-000-H
 *@details
 *   @details 裸机适配层 (无 RTOS) 实现 主要实现裸机适配层的函数
 *   @note 裸机任务和os的task不同 裸机任务是裸机任务
 *   不是os的task是基于侵入式链表的包装和os一样但是参数不一样而且没有优先级和栈大小而且复杂任务必须走状态机和任务切换(日常就os吧省心省力,除非内存紧张或者对效率要求极高))
 *   @note 裸机信号量互斥锁和os的semaphore不同 裸机信号量是裸机信号量
 *   不是os的信号量和互斥锁和是基于原子操作的而且没有优先级和栈大小
 *   互斥锁有两套一套关中断(一般这个)一套原子(amp才用这个(smp老老实实去上os别玩什么裸机))
 *   @note
 *   时基系统是复用xtask的时基系统因为xtask的时基就是统一的时基没有xtask的时基系统无法正常运行而且由于xtask的时基中断是虚拟中断所以修改也很容易
 *   @note
 *   当然如果不用任务模式也可以自实现时基系统只需要将xtask的时基系统替换为自实现时基系统(定时器虚拟中断帮你写好了自己修改一下数值就行),裸机这边对接osal的task是直接用不了的若你要用请在我的兼容层上自实现休息链表加运行链表
 */

#ifdef CONFIG_OSAL_NULL

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "osal_null.h"

#include "board_config.h"
#include "buffer.h"
#include "compiler_compat.h"
#include "config.h"
#include "osal.h"
#include "xtask.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_compat_poison.h"
/**
 * @brief 队列最大数量与单队列缓冲区字节大小 (Kconfig CONFIG_OSAL_NULL_* 控制)
 */
#ifndef OSAL_NULL_MAX_QUEUES
#ifdef CONFIG_OSAL_NULL_MAX_QUEUES
#define OSAL_NULL_MAX_QUEUES CONFIG_OSAL_NULL_MAX_QUEUES
#else
#define OSAL_NULL_MAX_QUEUES 0
#endif
#endif
/* 基础队列数 + EventBus 自动 +1 (EventBus 需要一个队列) */
#ifdef CONFIG_EVENT_BUS
#define OSAL_NULL_QUEUE_POOL_SIZE (OSAL_NULL_MAX_QUEUES + 1)
#else
#define OSAL_NULL_QUEUE_POOL_SIZE OSAL_NULL_MAX_QUEUES
#endif
#ifndef OSAL_NULL_QUEUE_BUF_SZ
#ifdef CONFIG_OSAL_NULL_QUEUE_BUF_SZ
#define OSAL_NULL_QUEUE_BUF_SZ CONFIG_OSAL_NULL_QUEUE_BUF_SZ
#else
#define OSAL_NULL_QUEUE_BUF_SZ 2048
#endif
#endif
#define OSAL_NULL_QUEUE_ELEM_COUNT (OSAL_NULL_QUEUE_BUF_SZ / sizeof(fifo_data_type))

/**
 * @brief 队列内部结构 — 复用 buffer.h 的 fifo_spsc 无锁环形 FIFO
 * @details 每个队列静态内嵌一个 fifo_spsc + 元素缓冲区, item_size 按
 * @details sizeof(fifo_data_type) 向下对齐, 多元素项用 fifo_write_block / fifo_read_block 原子读写
 */
struct osal_queue_obj
{
    struct fifo_spsc fifo;                                             /**<队列*/
    fifo_data_type   buf[OSAL_NULL_QUEUE_ELEM_COUNT] MINI_ALIGNED(32); /**<队列缓冲区*/
    size_t           elements_per_item;                                /**<每个队列元素包含的元素个数*/
};

#if OSAL_NULL_QUEUE_POOL_SIZE > 0
static struct osal_queue_obj         s_queues[OSAL_NULL_QUEUE_POOL_SIZE] MINI_ALIGNED(64);    /**<队列池*/
static uint8_t                       s_queue_used[OSAL_NULL_QUEUE_POOL_SIZE] MINI_ALIGNED(4); /**<队列使用情况*/
static osal_pool_t s_queue_pool_ctrl MINI_ALIGNED(4);                                         /**<队列池控制句柄*/

/** @brief 取队列池第 idx 个对象 (池为 0 时恒 NULL) */
MINI_STATIC_INLINE struct osal_queue_obj* queue_at(int idx) { return &s_queues[idx]; }
#else
MINI_STATIC_INLINE struct osal_queue_obj* queue_at(int idx)
{
    MINI_UNUSED_PARAM(idx);
    return NULL;
}
#endif /* OSAL_NULL_QUEUE_POOL_SIZE > 0 */

#if OSAL_NULL_QUEUE_POOL_SIZE > 0
/**
 * @brief 队列池初始化
 * @details 队列池初始化 主要是队列的缓冲区 队列的长度 队列的元素大小 队列的头部和尾部
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_QUEUE_POOL) static void osal_null_queue_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_queue_pool_ctrl, s_queue_used, OSAL_NULL_QUEUE_POOL_SIZE));
}
#endif /* OSAL_NULL_QUEUE_POOL_SIZE > 0 */

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) ||                            \
    defined(__ARM_ARCH_8M_MAIN__)
/**
 * @brief 执行 WFI 等待中断 (Cortex-M 低功耗忙等)
 */
MINI_STATIC_INLINE void osal_null_wfi(void) { __asm__ volatile("wfi"); }

#elif defined(__riscv)

/**
 * @brief 等待中断 低功耗指令
 */
MINI_STATIC_INLINE void osal_null_wfi(void) { __asm__ volatile("wfi"); }

#else

/**
 * @brief WFI 占位 (未知架构, 无操作)
 */
MINI_STATIC_INLINE void osal_null_wfi(void) {}

#endif

/* -------------------------------------------------------------------------- */
/* ISR 嵌套计数 (osal_null_isr_enter/exit 维护, 与 IPSR 联合判定) */
/* -------------------------------------------------------------------------- */
static volatile uint32_t s_isr_nest;

/**
 * @brief ISR 入口递增嵌套计数
 */
void osal_null_isr_enter(void) { s_isr_nest++; }

/**
 * @brief ISR 出口递减嵌套计数
 */
void osal_null_isr_exit(void)
{
    if (s_isr_nest > 0U)
        s_isr_nest--;
}

/* -------------------------------------------------------------------------- */
/* 互斥锁 (原子 CAS + 递归深度) */
/* -------------------------------------------------------------------------- */
struct osal_mutex
{
    osal_mutex_type_t type;  /**<互斥锁类型*/
    MINI_ATOMIC_UINT  lock;  /**<互斥锁状态*/
    MINI_ATOMIC_UINT  depth; /**<互斥锁深度*/
};
_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE, "osal_null: OSAL_MUTEX_STORAGE_SIZE too small");

/**
 * @brief 互斥锁初始化
 * @param[in] mutex 互斥锁结构体指针
 * @param[in] type 互斥锁类型
 * @return 结果
 * @details 互斥锁初始化时, 将互斥锁状态设置为0, 互斥锁深度设置为0
 */
static int osal_mutex_init(struct osal_mutex* mutex, osal_mutex_type_t type)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN)
        return OSAL_ERR_INVAL;

    mutex->type = type;
    MINI_ATOMIC_STORE(&mutex->lock, 0U, MINI_RELEASE);
    MINI_ATOMIC_STORE(&mutex->depth, 0U, MINI_RELEASE);
    return OSAL_OK;
}

/**
 * @brief 互斥锁池
 * @details 互斥锁池 主要是互斥锁的缓冲区 互斥锁的使用情况 互斥锁的池控制句柄
 */
static struct osal_mutex             s_mutex_pool[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_mutex_used[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_mutex_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 互斥锁池初始化
 * @details 互斥锁池初始化 主要是互斥锁的缓冲区 互斥锁的使用情况 互斥锁的池控制句柄
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void osal_null_mutex_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE));
}

/**
 * @brief 判定是否在 ISR (嵌套计数 + IPSR/mcause)
 * @return 1 在 ISR, 0 否
 */
int osal_in_isr(void)
{
    if (s_isr_nest > 0U)
        return 1;

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) ||                            \
    defined(__ARM_ARCH_8M_MAIN__) || defined(__CORTEX_M)
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    return (ipsr & 0xFFU) != 0U;
#elif defined(__riscv)
    uintptr_t mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    return (int)(mcause >> 31);
#else
    return 0;
#endif
}

/**
 * @brief 初始化槽位池
 * @param[in] pool 池
 * @param[in] used_slots 占用数组
 * @param[in] count 槽位数
 * @return OSAL_OK 或 OSAL_ERR_INVAL
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* used_slots, size_t count)
{
    if (!pool || !used_slots || count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = used_slots;
    pool->slot_count = count;

    for (size_t index = 0; index < count; index++)
        used_slots[index] = 0;

    return OSAL_OK;
}

/**
 * @brief 关中断申请空闲槽
 * @param[in] pool 池
 * @return 索引或负错误码
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;

    uint32_t irq = osal_null_irq_disable();
    int      claimed = -1;
    for (size_t index = 0; index < pool->slot_count; index++)
    {
        if (!pool->used_slots[index])
        {
            pool->used_slots[index] = 1;
            claimed = (int)index;
            break;
        }
    }
    osal_null_irq_restore(irq);
    return claimed;
}

/**
 * @brief 关中断释放槽
 * @param[in] pool 池
 * @param[in] idx 索引
 * @return OSAL_OK 或 OSAL_ERR_INVAL
 */
int osal_pool_release(osal_pool_t* pool, int idx)
{
    if (!pool || !pool->used_slots || idx < 0 || (size_t)idx >= pool->slot_count)
        return OSAL_ERR_INVAL;

    uint32_t irq = osal_null_irq_disable();
    pool->used_slots[idx] = 0;
    osal_null_irq_restore(irq);
    return OSAL_OK;
}

/**
 * @brief 查询槽占用
 * @param[in] pool 池
 * @param[in] idx 索引
 * @return true 已占用
 */
bool osal_pool_is_used(osal_pool_t* pool, int idx)
{
    if (!pool || !pool->used_slots || idx < 0 || (size_t)idx >= pool->slot_count)
        return false;
    return pool->used_slots[idx] != 0U;
}

/**
 * @brief 队列索引
 * @param[in] queue 队列句柄
 * @return 队列索引
 */
MINI_STATIC_INLINE int queue_index_of(osal_queue_handle_t queue)
{
#if OSAL_NULL_QUEUE_POOL_SIZE > 0
    if (!queue)
        return -1;
    int idx = (int)((struct osal_queue_obj*)queue - s_queues); /**< 计算出队列在静态队列池中的索引queue的地址减去全局起始地址 */
    if (idx < 0 || idx >= OSAL_NULL_QUEUE_POOL_SIZE || !s_queue_used[idx])
        return -1;
    return idx;
#else
    MINI_UNUSED_PARAM(queue);
    return -1; /* 队列池未启用 (OSAL_NULL_QUEUE_POOL_SIZE=0) */
#endif
}

/**
 * @brief 自旋锁
 * @details 默认 CONFIG_OSAL_SPINLOCK_IRQ_DISABLE: 单核下退化为关中断临界区,
 * @details  CONFIG_OSAL_SPINLOCK_ATOMIC: 与 FreeRTOS 非 ESP 后端一致,
 * @details  使用原子 test-and-set 忙等自旋锁 (仅适合 SMP).
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    uint32_t irq_saved; /**< IRQ 状态保存 */
    uint32_t nest;      /**< 嵌套计数 */
#else
    volatile int locked; /**< 原子锁标志 (0=空闲, 1=持有) */
#endif
};

/**
 * @brief 初始化自旋锁
 * @param[in] lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    lock->irq_saved = 0U;
    lock->nest = 0U;
#else
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 获取自旋锁
 * @param[in] lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    uint32_t irq = osal_null_irq_disable();
    if (lock->nest == 0U)
        lock->irq_saved = irq;
    lock->nest++;
#else
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
        ;
#endif
    return OSAL_OK;
}

/**
 * @brief 释放自旋锁
 * @param[in] lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    if (lock->nest > 0U)
    {
        lock->nest--;
        if (lock->nest == 0U)
            osal_null_irq_restore(lock->irq_saved);
    }
#else
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 检查自旋锁是否锁定
 * @param[in] lock 自旋锁指针
 * @return 是否锁定
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

/**
 * @brief xtask tick_count 毫秒时钟
 * @return 毫秒
 */
uint32_t osal_time_ms(void) { return MINI_ATOMIC_LOAD(&g_scheduler.tick_count, MINI_RELAXED); }

#define OSAL_NULL_TICK_HANG_THRESHOLD 10000U
/**
 * @brief WFI 忙等延迟 (阻塞当前执行流)
 * @param[in] ms 毫秒
 * @note  本函数是阻塞式忙等: 调用期间所有裸机任务停摆, 仅用于主循环 /
 *        非协程上下文。任务回调内的"让出式延时"请用 protothread 宏
 *        PT_DELAY(task, ms) —— 挂起当前任务到到期时刻, 其他任务继续跑,
 *        到期后从让出点恢复 (见 xtask.h)。
 */
void osal_delay_ms(uint32_t ms)
{
    if (ms == 0)
        return;

    uint32_t start = osal_time_ms();
    uint32_t last = start;
    uint32_t no_tick_count = 0U;

    while ((osal_time_ms() - start) < ms)
    {
        osal_null_wfi();

        uint32_t now = osal_time_ms();
        if (now == last)
            no_tick_count++;
        else
        {
            last = now;
            no_tick_count = 0U; // 只要时钟前进了，立刻清零重置
        }

        /**<连续冲过阈值，判定时基系统未运行或中断被意外屏蔽，退出死等以防硬死锁 */
        if (no_tick_count > OSAL_NULL_TICK_HANG_THRESHOLD)
            break;
    }
}

void osal_delay_us(uint32_t us)
{
    /* 裸机无可靠 us 时钟时做短忙等；精度依赖编译器与主频 */
    volatile uint32_t loops = us * 8U;
    while (loops-- > 0U)
        ;
}

/**
 * @brief 裸机 tick 与 ms 1:1
 * @param[in] ms 毫秒
 * @return tick
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms) { return ms; }

/**
 * @brief 超时转 tick
 * @param[in] timeout_ms 毫秒
 * @return tick
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return UINT32_MAX;
    return timeout_ms;
}

/* -------------------------------------------------------------------------- */
/* 内存 (不推荐使用) */
/* /** */
/* @brief calloc 分配 */
/* @param[in] count 数量 */
/* @param[in] size 大小 */
/* @return 指针 */
/* -------------------------------------------------------------------------- */
void* osal_calloc(size_t count, size_t size) { return calloc(count, size); }

/**
 * @brief free 释放
 * @param[in] ptr 指针
 * @return OSAL_OK
 */
int osal_free(void* ptr)
{
    free(ptr);
    return OSAL_OK;
}

/**
 * @brief 池化创建指定类型互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
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

    int idx = osal_pool_claim(&s_mutex_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    if (osal_mutex_init(&s_mutex_pool[idx], type) != OSAL_OK) /**< 初始化互斥锁 */
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, idx)); /**< 释放互斥锁 */
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)&s_mutex_pool[idx]; /**< 返回互斥锁指针 */
    return OSAL_OK;
}

/**
 * @brief storage 内创建指定类型互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage, size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN)
        return OSAL_ERR_INVAL;

    struct osal_mutex* mutex_obj = (struct osal_mutex*)storage;
    if (osal_mutex_init(mutex_obj, type) != OSAL_OK)
        return OSAL_ERR_NOMEM;
    *out = mutex_obj;
    return OSAL_OK;
}

/**
 * @brief 创建普通互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief storage 内创建普通互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建递归互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_recursive(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE); }

/**
 * @brief storage 内创建递归互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 显式创建普通互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_plain(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief storage 内显式普通互斥锁
 * @param[out] out 输出
 * @param ... 见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 销毁互斥锁并归还池槽
 * @param[in] mutex 锁
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex)
        return;
    if (osal_in_isr())
        return;

    /* 先做边界检查: 仅池内互斥锁可销毁, 静态锁 (create_static) 不属于池, 直接拒绝.
     * 必须在校验通过后才触碰 mutex 字段, 避免对池外/非法指针越界写. */
    if (mutex < s_mutex_pool || mutex >= &s_mutex_pool[OSAL_MUTEX_POOL_SIZE])
        return;

    MINI_ATOMIC_STORE(&mutex->lock, 0U, MINI_RELEASE);  /**< 释放互斥锁 */
    MINI_ATOMIC_STORE(&mutex->depth, 0U, MINI_RELEASE); /**< 释放互斥锁深度 */

    int idx = (int)(mutex - s_mutex_pool);                          /**< 计算互斥锁在静态互斥锁池中的索引 */
    MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, idx)); /**< 释放互斥锁索引 */
}

/**
 * @brief 尝试获取互斥锁
 * @param[in] mutex 互斥锁指针
 * @return 错误码
 * @details AMP 模式: 原子 CAS + depth 递增, 支持跨核竞争.
 *          普通模式: 关中断后用 if 判断, 单核下无竞争.
 */
static int osal_mutex_try_acquire(struct osal_mutex* mutex)
{
#ifdef CONFIG_AMP_MODE
    uint32_t expected = 0;
    if (MINI_ATOMIC_CAS(&mutex->lock, &expected, 1, MINI_ACQUIRE, MINI_RELAXED))
    {
        MINI_ATOMIC_STORE(&mutex->depth, 1, MINI_RELAXED);
        return OSAL_OK;
    }

    if (mutex->type == OSAL_MUTEX_RECURSIVE)
    {
        uint32_t depth = MINI_ATOMIC_LOAD(&mutex->depth, MINI_RELAXED);
        if (depth == 0U)
            return OSAL_ERR_BUSY;

        MINI_ATOMIC_STORE(&mutex->depth, depth + 1U, MINI_RELAXED);
        return OSAL_OK;
    }

    return OSAL_ERR_BUSY;
#else
    uint32_t irq = osal_null_irq_disable();
    if (mutex->lock == 0U)
    {
        mutex->lock = 1U;
        mutex->depth = 1U;
        osal_null_irq_restore(irq);
        return OSAL_OK;
    }

    if (mutex->type == OSAL_MUTEX_RECURSIVE && mutex->depth > 0U)
    {
        mutex->depth++;
        osal_null_irq_restore(irq);
        return OSAL_OK;
    }

    osal_null_irq_restore(irq);
    return OSAL_ERR_BUSY;
#endif
}

/**
 * @brief 获取互斥锁 (WFI 重试)
 * @param[in] mutex 锁
 * @param[in] timeout_ms 超时
 * @return OSAL_OK 或错误码
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    if (osal_mutex_try_acquire(mutex) == OSAL_OK)
        return OSAL_OK;

    if (timeout_ms == 0U)
        return OSAL_ERR_TIMEOUT;

    uint32_t start = 0U;
    if (timeout_ms != OSAL_WAIT_FOREVER)
        start = osal_time_ms();

    for (;;)
    {
        if (osal_mutex_try_acquire(mutex) == OSAL_OK)
            return OSAL_OK;

        if (timeout_ms != OSAL_WAIT_FOREVER && (osal_time_ms() - start) >= timeout_ms)
            return OSAL_ERR_TIMEOUT;

#ifdef CONFIG_OSAL_NULL_WFI
        osal_null_wfi();
#endif
    }
}

/**
 * @brief 释放互斥锁
 * @param[in] mutex 锁
 * @return OSAL_OK 或错误码
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

#ifdef CONFIG_AMP_MODE
    uint32_t depth = MINI_ATOMIC_LOAD(&mutex->depth, MINI_RELAXED);
    if (depth == 0U)
        return OSAL_ERR_IO;

    if (depth > 1U)
    {
        MINI_ATOMIC_STORE(&mutex->depth, depth - 1U, MINI_RELAXED);
        return OSAL_OK;
    }

    MINI_ATOMIC_STORE(&mutex->depth, 0U, MINI_RELAXED);
    MINI_ATOMIC_STORE(&mutex->lock, 0U, MINI_RELEASE);
    return OSAL_OK;
#else
    uint32_t irq = osal_null_irq_disable();
    if (mutex->depth == 0)
    {
        osal_null_irq_restore(irq);
        return OSAL_ERR_IO;
    }

    if (mutex->depth > 1)
    {
        mutex->depth--;
        osal_null_irq_restore(irq);
        return OSAL_OK;
    }

    mutex->depth = 0U;
    mutex->lock = 0U;
    osal_null_irq_restore(irq);
    return OSAL_OK;
#endif
}

/**
 * @brief 周期任务 stub: 按 period_ms 循环调用 orig_callback 以对齐 OS 周期任务路径
 * @param[in] param 周期任务包装指针 (osal_periodic_task_wrap*)
 */
__attribute__((unused)) static void osal_periodic_task_stub(void* param)
{
    struct osal_periodic_task_wrap* wrap = (struct osal_periodic_task_wrap*)param;
    if (!wrap || !wrap->orig_callback)
        return;

    /**< 模拟周期任务死循环 */
    while (1)
    {
        uint32_t start_time = osal_time_ms();

        /**< 执行os期望的回调函数 */
        wrap->orig_callback(wrap->x_task);

        /**< 计算执行耗时，精准补偿延时，防止时间漂移 */
        uint32_t cost = osal_time_ms() - start_time;
        if (cost < wrap->period_ms)
            osal_delay_ms(wrap->period_ms - cost);
        else
            /**< 如果业务执行时间已经超过了周期，直接让出 CPU 周期（Yield） */
            osal_delay_ms(1);
    }
}

/**
 * @brief 裸机不支持任务
 * @param[in] name 忽略
 * @param[in] stack_size 忽略
 * @param[in] priority 忽略
 * @param[in] entry 忽略
 * @param[in] param 忽略
 * @param[in] core_id 忽略
 * @return OSAL_ERR_NOTSUPP
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id)
{
    MINI_UNUSED_PARAM(name);
    MINI_UNUSED_PARAM(stack_size);
    MINI_UNUSED_PARAM(priority);
    MINI_UNUSED_PARAM(entry);
    MINI_UNUSED_PARAM(param);
    MINI_UNUSED_PARAM(core_id);
    return OSAL_ERR_NOTSUPP;
}

/**
 * @brief 裸机不支持带句柄任务
 * @param[in] name 忽略
 * @param[in] stack_size 忽略
 * @param[in] priority 忽略
 * @param[in] entry 忽略
 * @param[in] param 忽略
 * @param[in] core_id 忽略
 * @param[out] out_handle 输出
 * @return OSAL_ERR_NOTSUPP
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle)
        return OSAL_ERR_INVAL;
    MINI_UNUSED_PARAM(name);
    MINI_UNUSED_PARAM(stack_size);
    MINI_UNUSED_PARAM(priority);
    MINI_UNUSED_PARAM(entry);
    MINI_UNUSED_PARAM(param);
    MINI_UNUSED_PARAM(core_id);
    *out_handle = NULL;
    return OSAL_ERR_NOTSUPP;
}

/**
 * @brief 永久 WFI 占位
 */
void osal_task_self_delete(void)
{
    while (1)
        osal_null_wfi();
}

/**
 * @brief 空操作
 * @param[in] task 忽略
 */
void osal_task_delete(osal_task_handle_t task) { MINI_UNUSED_PARAM(task); }

/**
 * @brief 恒 false
 * @param[in] task 忽略
 * @return false
 */
bool osal_task_is_running(osal_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return false;
}

/**
 * @brief 返回 baremetal
 * @param[in] task 忽略
 * @return 任务名
 */
const char* osal_task_get_name(osal_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return "baremetal";
}

/**
 * @brief 无栈监控
 * @param[out] task 忽略
 * @return 0
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return 0U;
}

/* -------------------------------------------------------------------------- */
/*信号量推荐走全局不要自己定义非全局信号量,信号量基本不需要全局监督某一个资源*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 二值信号量
 */
struct osal_sem
{
    MINI_ATOMIC_UINT signaled;  /**< 信号量状态 (0=空, >0=有信号) */
    bool             from_pool; /**< 是否从池中分配 */
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE, "OSAL_SEM_STORAGE_SIZE too small");

/**
 * @brief 二值信号量池
 * @param[in] s_sem_pool 信号量池
 * @param[in] s_sem_used 信号量使用情况
 * @param[in] s_sem_pool_ctrl 信号量池控制
 */
static struct osal_sem             s_sem_pool[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                     s_sem_used[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_sem_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief null OSAL 信号量池启动初始化
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_SEM_POOL) static void osal_null_sem_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE));
}

/**
 * @brief 池化二值信号量
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_sem* sem = &s_sem_pool[idx];
    MINI_ATOMIC_STORE(&sem->signaled, 0U, MINI_RELEASE);
    sem->from_pool = true;
    *out = sem;
    return OSAL_OK;
}

/**
 * @brief 静态二值信号量
 * @param[out] out 输出
 * @param[in] storage 存储
 * @param[in] storage_size 大小
 * @return OSAL_OK 或错误码
 */
int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return OSAL_ERR_INVAL;

    struct osal_sem* sem = (struct osal_sem*)storage;
    MINI_ATOMIC_STORE(&sem->signaled, 0U, MINI_RELEASE);
    sem->from_pool = false;
    *out = sem;
    return OSAL_OK;
}

/**
 * @brief 销毁信号量
 * @param[in] sem 信号量
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem)
        return;

    /* 池内信号量: 先确认落在池内再清零并释放; 池外/非法指针直接拒绝, 避免越界写. */
    if (sem->from_pool)
    {
        if (sem < s_sem_pool || sem >= &s_sem_pool[OSAL_SEM_POOL_SIZE])
            return;

        MINI_ATOMIC_STORE(&sem->signaled, 0U, MINI_RELEASE);
        int idx = (int)(sem - s_sem_pool); /**< 计算信号量在信号量池中的索引 */
        MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
        return;
    }

    /* 静态信号量 (create_binary_static): 属于调用方存储, 仅清零不复用池槽. */
    MINI_ATOMIC_STORE(&sem->signaled, 0U, MINI_RELEASE);
}

/**
 * @brief 尝试等待二值信号量
 * @param[in] sem 信号量指针
 * @return 错误码
 */
static int osal_sem_try_wait(struct osal_sem* sem)
{
    uint32_t expected = 1U;
    if (MINI_ATOMIC_CAS(&sem->signaled, &expected, 0U, MINI_ACQUIRE, MINI_RELAXED))
        return OSAL_OK;
    return OSAL_ERR_BUSY;
}

/**
 * @brief 等待信号量
 * @param[in] sem 信号量
 * @param[in] timeout_ms 超时
 * @return OSAL_OK 或错误码
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem)
        return OSAL_ERR_INVAL;

    if (osal_sem_try_wait(sem) == OSAL_OK)
        return OSAL_OK;

    if (timeout_ms == 0U)
        return OSAL_ERR_TIMEOUT;

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (osal_sem_try_wait(sem) != OSAL_OK)
        {
#ifdef CONFIG_OSAL_NULL_WFI
            osal_null_wfi();
#endif
        }
        return OSAL_OK;
    }

    uint32_t start = osal_time_ms();
    while (osal_sem_try_wait(sem) != OSAL_OK)
    {
        if ((osal_time_ms() - start) >= timeout_ms)
            return OSAL_ERR_TIMEOUT;
#ifdef CONFIG_OSAL_NULL_WFI
        osal_null_wfi();
#endif
    }
    return OSAL_OK;
}

/**
 * @brief 触发信号量
 * @param[in] sem 信号量
 * @return true 成功
 */
bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem)
        return false;
    MINI_ATOMIC_STORE(&sem->signaled, 1U, MINI_RELEASE);
    return true;
}

/**
 * @brief ISR 触发信号量
 * @param[in] sem 信号量
 * @param[in] px_yield_required 忽略
 * @return true 成功
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    MINI_IGNORE_RESULT(px_yield_required);
    return osal_sem_post(sem);
}

/**
 * @brief 无调度 yield
 * @param[in] yield_required 忽略
 */
void osal_yield_from_isr(bool yield_required) { MINI_IGNORE_RESULT(yield_required); }

/**
 * @brief 池化 SPSC FIFO 队列
 * @param[in] queue_len 深度(2^n)
 * @param[in] item_size 字节(fifo_data_type 倍数)
 * @return 句柄或 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    if (queue_len == 0 || item_size == 0)
        return NULL;

    if ((queue_len & (queue_len - 1)) != 0)
        MINI_TRAP();

    /**< item_size 必须是 sizeof(fifo_data_type) 的整数倍, 否则无法直接 cast 做块读写 */
    if (item_size % sizeof(fifo_data_type) != 0)
        return NULL;

    size_t elements_per_item = item_size / sizeof(fifo_data_type); /**< 计算出一个消息 item 占用了多少个 FIFO 基础单元 */
    size_t total_elements = queue_len * elements_per_item;         /**< 计算总元素个数,因为底层fifo就没有%和//所以这里必须2的整数倍 */

    if ((total_elements & (total_elements - 1)) != 0) /**< 判断总元素个数是否为2的整数倍 */
        MINI_TRAP();

    if (total_elements > OSAL_NULL_QUEUE_ELEM_COUNT)
        return NULL;

#if OSAL_NULL_QUEUE_POOL_SIZE > 0
    int idx = osal_pool_claim(&s_queue_pool_ctrl);
    if (idx < 0)
        return NULL;

    struct osal_queue_obj* queue = &s_queues[idx];
    queue->elements_per_item = elements_per_item; /**< 设置每个队列元素包含的元素个数 */
    if (fifo_init(&queue->fifo, queue->buf, (uint16_t)total_elements) != BUFF_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_queue_pool_ctrl, idx));
        return NULL;
    }

    return (osal_queue_handle_t)queue;
#else
    MINI_UNUSED_PARAM(total_elements);
    return NULL; /* 队列池未启用, 需在 Kconfig 设置基础队列数或开启 EVENT_BUS */
#endif
}

/**
 * @brief 释放队列槽
 * @param[in] queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
#if OSAL_NULL_QUEUE_POOL_SIZE > 0
    int idx = queue_index_of(queue);
    if (idx < 0)
        return;
    MINI_IGNORE_RESULT(osal_pool_release(&s_queue_pool_ctrl, idx));
#else
    MINI_UNUSED_PARAM(queue);
#endif
}

/**
 * @brief 发送队列
 * @param[in] queue 队列句柄
 * @param[in] item 发送数据
 * @return 是否成功
 */
static bool queue_send_internal(osal_queue_handle_t queue, const void* item)
{
    int idx = queue_index_of(queue);
    if (idx < 0 || !item)
        return false;

    struct osal_queue_obj* queue_obj = queue_at(idx);
    if (!queue_obj)
        return false;
    uint16_t epi = (uint16_t)queue_obj->elements_per_item;
    uint16_t count = 0;
    uint16_t written = 0;

    /**< SPSC 安全: 消费者只能释放空间不会缩减,
     * 检查通过则写入必然完整就是如果你传的格子数大小超过剩余各子数那么就会返回false */
    MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
    if ((uint16_t)(queue_obj->fifo.size - count) < epi)
        return false;

    if (fifo_write_block(&queue_obj->fifo, (const fifo_data_type*)item, epi, &written) != BUFF_OK)
        return false;
    return written == epi;
}

/**
 * @brief 任务态入队 (非阻塞)
 * @param[in] queue 句柄
 * @param[in] item 数据
 * @param[in] timeout_ms 忽略
 * @return true 成功
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    MINI_UNUSED_PARAM(timeout_ms);

    if (osal_in_isr())
        return false;

    return queue_send_internal(queue, item);
}

/**
 * @brief ISR 态入队 (SPSC 直接写 FIFO, 无 yield)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送数据
 * @param[in] px_yield_required yield 标志 (裸机忽略)
 * @return true 成功, false 失败
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);
    return queue_send_internal(queue, item);
}

/**
 * @brief 任务态出队
 * @param[out] queue 句柄
 * @param[out] item 缓冲
 * @param[in] timeout_ms 超时
 * @return true 成功
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item)
        return false;

    struct osal_queue_obj* queue_obj = queue_at(idx);
    if (!queue_obj)
        return false;
    uint16_t epi = (uint16_t)queue_obj->elements_per_item;
    uint16_t count = 0;
    uint16_t rd = 0;

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
        while (count < epi)
        {
#ifdef CONFIG_OSAL_NULL_WFI
            osal_null_wfi();
#endif
            MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
        }
    }
    else if (timeout_ms > 0)
    {
        uint32_t start = osal_time_ms();
        MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
        while (count < epi)
        {
            if ((osal_time_ms() - start) >= timeout_ms)
                return false;
#ifdef CONFIG_OSAL_NULL_WFI
            osal_null_wfi();
#endif
            MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
        }
    }

    MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
    if (count < epi)
        return false;

    if (fifo_read_block(&queue_obj->fifo, (fifo_data_type*)item, epi, &rd) != BUFF_OK)
        return false;
    return rd == epi;
}

/**
 * @brief ISR 态出队 (不阻塞, 无 yield)
 * @param[out] queue 队列句柄
 * @param[out] item 接收缓冲区
 * @param[out] px_yield_required yield 标志 (裸机忽略)
 * @return true 收到完整 item, false 数据不足或参数无效
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    MINI_IGNORE_RESULT(px_yield_required);

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) /**< 判断队列句柄是否有效且接收数据指针是否有效 */
        return false;

    struct osal_queue_obj* queue_obj = queue_at(idx);
    if (!queue_obj)
        return false;
    uint16_t epi = (uint16_t)queue_obj->elements_per_item;
    uint16_t count = 0;
    uint16_t rd = 0;

    MINI_IGNORE_RESULT(fifo_get_count(&queue_obj->fifo, &count));
    if (count < epi)
        return false;

    if (fifo_read_block(&queue_obj->fifo, (fifo_data_type*)item, epi, &rd) != BUFF_OK)
        return false;
    return rd == epi;
}

/* -------------------------------------------------------------------------- */
/* 事件组 (CONFIG_OSAL_EVENT 门控; 裸机无内核支持, 用原子标志字 + CAS 自实现) */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_OSAL_EVENT
/**
 * @brief 裸机事件组
 * @details 三个 RTOS 后端都内嵌内核对象, 本后端没有可用原语,
 *          因此直接用一个 32 位原子标志字 + CAS 循环自己实现,
 *          对外语义 (AND/OR、auto_clear、超时) 与三后端一致。
 *          单核下并发只可能来自 ISR 与主流程之间, CAS 循环足以保证
 *          置位/清位/消费三个动作不丢更新。
 */
struct osal_event
{
    MINI_ATOMIC_UINT  flags;      /**< 事件标志字 (原子) */
    osal_event_mode_t mode;       /**< 创建期固定的 AND/OR 模式 */
    bool              auto_clear; /**< 创建期固定的自动消费标志 */
    bool              from_pool;  /**< 是否从池中分配 */
};

_Static_assert(sizeof(struct osal_event) <= OSAL_EVENT_STORAGE_SIZE, "OSAL_EVENT_STORAGE_SIZE too small");

/**
 * @brief 事件组池
 */
static struct osal_event             s_event_pool[OSAL_EVENT_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_event_used[OSAL_EVENT_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_event_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief null OSAL 事件组池启动初始化
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_EVENT_POOL) static void osal_null_event_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_event_pool_ctrl, s_event_used, OSAL_EVENT_POOL_SIZE));
}

/**
 * @brief 校验标志掩码 (非 0 且不越出 OSAL 可用位区)
 * @param[in] bits 待校验掩码
 * @return true 合法
 * @details 本后端的标志字是全 32 位可用的, 但抽象层按四后端最小公约数
 *          (FreeRTOS 只剩 bit0..23) 收口, 保证业务代码换后端不改行为;
 *          见 osal.h 的 OSAL_EVENT_BITS / OSAL_EVENT_MASK。
 */
MINI_STATIC_INLINE bool osal_event_bits_valid(uint32_t bits) { return (bits != 0U) && ((bits & ~OSAL_EVENT_MASK) == 0U); }

/**
 * @brief 判定当前标志是否满足等待掩码
 * @param[in] ev 事件组
 * @param[in] cur 当前标志快照
 * @param[in] bits 等待掩码
 * @return true 满足
 */
MINI_STATIC_INLINE bool osal_null_event_satisfied(const struct osal_event* ev, uint32_t cur, uint32_t bits)
{
    if (ev->mode == OSAL_EVENT_AND)
        return (cur & bits) == bits;
    return (cur & bits) != 0U;
}

/**
 * @brief 原子把 bits 并入标志字
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码
 * @details CAS 失败时 cur 已被刷成最新值, 重算 cur|bits 再试即可;
 *          不用 fetch_or 是为了只依赖 compiler_compat.h 已有的原子宏集。
 */
MINI_STATIC_INLINE void osal_null_event_or(struct osal_event* ev, uint32_t bits)
{
    uint32_t cur = MINI_ATOMIC_LOAD(&ev->flags, MINI_RELAXED);
    for (;;)
    {
        uint32_t want = cur | bits;
        if (MINI_ATOMIC_CAS(&ev->flags, &cur, want, MINI_ACQ_REL, MINI_RELAXED))
            return;
    }
}

/**
 * @brief 原子从标志字清掉 bits
 * @param[in] ev 事件组
 * @param[in] bits 要清除的掩码
 */
MINI_STATIC_INLINE void osal_null_event_and_not(struct osal_event* ev, uint32_t bits)
{
    uint32_t cur = MINI_ATOMIC_LOAD(&ev->flags, MINI_RELAXED);
    for (;;)
    {
        uint32_t want = cur & ~bits;
        if (MINI_ATOMIC_CAS(&ev->flags, &cur, want, MINI_ACQ_REL, MINI_RELAXED))
            return;
    }
}

/**
 * @brief 单次非阻塞尝试: 满足则 (按需) 消费并回报相关位
 * @param[in] ev 事件组
 * @param[in] bits 等待掩码
 * @param[out] out_bits 回传消费前的相关位 (可为 NULL)
 * @return 满足 OSAL_OK; 未满足 OSAL_ERR_TIMEOUT
 * @details "判定满足"与"消费位"必须在同一个 CAS 里完成: 否则两个
 *          auto_clear 等待者可能都看到满足而重复消费同一组事件。
 *          CAS 成功后 cur 仍是交换前的值, 正好用来回报。
 */
static int osal_null_event_try_wait(struct osal_event* ev, uint32_t bits, uint32_t* out_bits)
{
    uint32_t cur = MINI_ATOMIC_LOAD(&ev->flags, MINI_ACQUIRE);
    for (;;)
    {
        if (!osal_null_event_satisfied(ev, cur, bits))
            return OSAL_ERR_TIMEOUT;

        uint32_t want = ev->auto_clear ? (cur & ~bits) : cur;
        if (MINI_ATOMIC_CAS(&ev->flags, &cur, want, MINI_ACQ_REL, MINI_ACQUIRE))
        {
            if (out_bits != NULL)
                *out_bits = cur & bits;
            return OSAL_OK;
        }
        /* CAS 失败: cur 已刷新, 重新判定 (并发方可能刚置位或刚清位) */
    }
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
    if (mode != OSAL_EVENT_OR && mode != OSAL_EVENT_AND)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_event_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_event* ev = &s_event_pool[idx];
    MINI_ATOMIC_STORE(&ev->flags, 0U, MINI_RELEASE); /**< 初始标志 0 */
    ev->mode = mode;
    ev->auto_clear = auto_clear;
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
    if (mode != OSAL_EVENT_OR && mode != OSAL_EVENT_AND)
        return OSAL_ERR_INVAL;

    struct osal_event* ev = (struct osal_event*)storage;
    MINI_ATOMIC_STORE(&ev->flags, 0U, MINI_RELEASE);
    ev->mode = mode;
    ev->auto_clear = auto_clear;
    ev->from_pool = false;
    *out = ev;
    return OSAL_OK;
}

/**
 * @brief 销毁事件组
 * @param[in] ev 事件组
 * @details 裸机没有阻塞等待者队列 (wait 是忙等), 因此无需判忙;
 *          清零标志后归还池槽, 池外/非法指针直接拒绍避免越界写。
 */
void osal_event_destroy(struct osal_event* ev)
{
    if (!ev)
        return;

    if (ev->from_pool)
    {
        if (ev < s_event_pool || ev >= &s_event_pool[OSAL_EVENT_POOL_SIZE])
            return;

        MINI_ATOMIC_STORE(&ev->flags, 0U, MINI_RELEASE);
        int idx = (int)(ev - s_event_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_event_pool_ctrl, idx));
    }
}

/**
 * @brief 置位事件标志 (task 上下文)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0, 仅 bit0..23)
 * @return OSAL_OK 或错误码
 * @details 裸机无调度器可唤醒, 置位后正在忙等的 osal_event_wait 会在
 *          下一轮重试时自行看到。
 */
int osal_event_set(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    osal_null_event_or(ev, bits);
    return OSAL_OK;
}

/**
 * @brief 置位事件标志 (ISR 上下文, 裸机与 task 同构)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0, 仅 bit0..23)
 * @param[out] px_yield_required yield 标志 (裸机无调度, 忽略)
 * @return OSAL_OK 或错误码
 */
int osal_event_set_from_isr(struct osal_event* ev, uint32_t bits, bool* px_yield_required)
{
    MINI_IGNORE_RESULT(px_yield_required);
    return osal_event_set(ev, bits);
}

/**
 * @brief 清除事件标志
 * @param[in] ev 事件组
 * @param[in] bits 要清除的掩码 (> 0, 仅 bit0..23)
 * @return OSAL_OK 或错误码
 */
int osal_event_clear(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    osal_null_event_and_not(ev, bits);
    return OSAL_OK;
}

/**
 * @brief 读取当前事件标志 (不阻塞、不消费)
 * @param[in] ev 事件组
 * @param[out] out_bits 回传当前标志 (可为 NULL, 则仅做存在性检查)
 * @return OSAL_OK 或错误码
 */
int osal_event_get(struct osal_event* ev, uint32_t* out_bits)
{
    if (!ev)
        return OSAL_ERR_INVAL;
    if (out_bits == NULL)
        return OSAL_OK;

    *out_bits = MINI_ATOMIC_LOAD(&ev->flags, MINI_ACQUIRE);
    return OSAL_OK;
}

/**
 * @brief 等待事件标志 (忙等)
 * @param[in] ev 事件组
 * @param[in] bits 等待的掩码 (> 0, 仅 bit0..23)
 * @param[in] timeout_ms 超时毫秒 (0 = 不阻塞, OSAL_WAIT_FOREVER = 永久)
 * @param[out] out_bits 回传实际已置位的相关位 (可为 NULL)
 * @return 满足 OSAL_OK; 未满足/超时 OSAL_ERR_TIMEOUT; 参数非法 OSAL_ERR_INVAL
 * @details 忙等结构与 osal_sem_wait 完全一致 (先试一次 → 0 超时直接返回 →
 *          FOREVER 死循环 → 否则按 osal_time_ms 卡截止时间)。
 * @note CONFIG_OSAL_NULL_WFI 开启时循环体走 WFI 省电, 但置位方必须来自
 *       中断 (否则无人能唤醒 WFI), 与信号量/队列等待的限制相同。
 */
int osal_event_wait(struct osal_event* ev, uint32_t bits, uint32_t timeout_ms, uint32_t* out_bits)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    if (osal_null_event_try_wait(ev, bits, out_bits) == OSAL_OK)
        return OSAL_OK;

    if (timeout_ms == 0U)
        return OSAL_ERR_TIMEOUT;

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (osal_null_event_try_wait(ev, bits, out_bits) != OSAL_OK)
        {
#ifdef CONFIG_OSAL_NULL_WFI
            osal_null_wfi();
#endif
        }
        return OSAL_OK;
    }

    uint32_t start = osal_time_ms();
    while (osal_null_event_try_wait(ev, bits, out_bits) != OSAL_OK)
    {
        if ((osal_time_ms() - start) >= timeout_ms)
            return OSAL_ERR_TIMEOUT;
#ifdef CONFIG_OSAL_NULL_WFI
        osal_null_wfi();
#endif
    }
    return OSAL_OK;
}
#endif /* CONFIG_OSAL_EVENT */

/* -------------------------------------------------------------------------- */
/* 硬件安全关断 & 日志 */
/* /** */
/* @brief 弱符号硬件安全关断 (板级未覆盖时触发 trap) */
/* -------------------------------------------------------------------------- */
MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }

/**
 * @brief 弱符号 Panic 安全互锁 (板级可覆盖: 喂狗、切断执行器等)
 */
MINI_WEAK void osal_panic_interlock(void) {}

/**
 * @brief 关中断冻结调度
 */
void osal_sched_freeze(void) { MINI_IGNORE_RESULT(osal_null_irq_disable()); }

/**
 * @brief 关中断不可恢复
 */
void osal_int_freeze(void) { (void)osal_null_irq_disable(); }

/**
 * @brief 格式化日志
 * @param[in] level 级别
 * @param[in] tag 标签
 * @param[in] fmt 格式
 * @param ... 参数
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
 * @brief 断言失败日志
 * @param[in] file 文件
 * @param[in] line 行号
 * @param[in] fmt 格式
 * @param ... 参数
 */
void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
{
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[1 FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_NULL */
