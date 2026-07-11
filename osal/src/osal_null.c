/**
 * @license SPDX-License-Identifier: Apache-2.0  
 * @file osal_null.c
 * @brief 裸机适配层 (无 RTOS) 实现
 * @details 裸机适配层 (无 RTOS) 实现 主要实现裸机适配层的函数
 * @note 裸机任务和os的task不同 裸机任务是裸机任务 不是os的task是基于侵入式链表的包装和os一样但是参数不一样而且没有优先级和栈大小而且复杂任务必须走状态机和任务切换(日常就os吧省心省力,除非内存紧张或者对效率要求极高))
 * @note 裸机信号量互斥锁和os的semaphore不同 裸机信号量是裸机信号量 不是os的信号量和互斥锁和是基于原子操作的而且没有优先级和栈大小 互斥锁有两套一套关中断(一般这个)一套原子(amp才用这个(smp老老实实去上os别玩什么裸机))
 * @note 时基系统是复用xtask的时基系统因为xtask的时基就是统一的时基没有xtask的时基系统无法正常运行而且由于xtask的时基中断是虚拟中断所以修改也很容易
 * @note 当然如果不用任务模式也可以自实现时基系统只需要将xtask的时基系统替换为自实现时基系统(定时器虚拟中断帮你写好了自己修改一下数值就行),裸机这边对接osal的task是直接用不了的若你要用请在我的兼容层上自实现休息链表加运行链表
 */
#ifdef CONFIG_OSAL_NULL

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "config.h"
#include "osal.h"
#include "osal_null.h"
#include "board_config.h"
#include "compiler_compat.h"
#include "buffer.h"
#include "xtask.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "compiler_compat_poison.h"
/**
 * @brief 队列最大数量与单队列缓冲区字节大小
 */
#ifndef OSAL_NULL_MAX_QUEUES
#define OSAL_NULL_MAX_QUEUES    4
#endif
#ifndef OSAL_NULL_QUEUE_BUF_SZ
#define OSAL_NULL_QUEUE_BUF_SZ  4096
#endif
#define OSAL_NULL_QUEUE_ELEM_COUNT  (OSAL_NULL_QUEUE_BUF_SZ / sizeof(Fifo_Data_type))

/**
 * @brief 队列内部结构 — 复用 buffer.h 的 fifo_spsc 无锁环形 FIFO
 * @details 每个队列静态内嵌一个 fifo_spsc + 元素缓冲区, item_size 按
 * @details sizeof(Fifo_Data_type) 向下对齐, 多元素项用 fifo_write_block / fifo_read_block 原子读写
 */
struct osal_queue_obj
{
    struct fifo_spsc    fifo;/**<队列*/
    Fifo_Data_type      buf[OSAL_NULL_QUEUE_ELEM_COUNT] COMPAT_ALIGNED(32);/**<队列缓冲区*/
    size_t              elements_per_item;/**<每个队列元素包含的元素个数*/
};

static struct osal_queue_obj s_queues[OSAL_NULL_MAX_QUEUES] COMPAT_ALIGNED(64);/**<队列池*/
static uint8_t               s_queue_used[OSAL_NULL_MAX_QUEUES] COMPAT_ALIGNED(4);/**<队列使用情况*/
static osal_pool_t           s_queue_pool_ctrl COMPAT_ALIGNED(4);/**<队列池控制句柄*/

/**
 * @brief 队列池初始化
 * @details 队列池初始化 主要是队列的缓冲区 队列的长度 队列的元素大小 队列的头部和尾部
 */
pre_execution(152)
static void osal_null_queue_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_queue_pool_ctrl, s_queue_used, OSAL_NULL_MAX_QUEUES));
}

/**
 * @brief 裸机临界区: 关全局中断 (单核 ISR vs 主循环互斥)
 */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
    defined(__ARM_ARCH_8M_MAIN__)
/**
 * @brief 关全局中断
 */
COMPAT_STATIC_INLINE uint32_t osal_null_irq_disable(void)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n\t/*换行而已为了好看*/cpsid i" : "=r"(primask) :: "memory");/**<读取primask寄存器并且禁止编译器对临界区前后的内存读写做重排优化*/
    return primask;
}

/**
 * @brief 恢复全局中断
 */
COMPAT_STATIC_INLINE void osal_null_irq_restore(uint32_t primask)
{
    __asm__ volatile("msr primask, %0" :: "r"(primask) : "memory");
}

/*
 * @brief 等待中断 低功耗指令
 */
COMPAT_STATIC_INLINE void osal_null_wfi(void)
{
    __asm__ volatile("wfi");
}

#elif defined(__riscv)

/**
 * @brief 关全局中断 risv-v 是基于mstatus寄存器的bits[3]
 */
COMPAT_STATIC_INLINE uint32_t osal_null_irq_disable(void)
{
    uintptr_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
    return (uint32_t)mstatus;
}

/**
 * @brief 恢复全局中断
 * @param mstatus 全局中断状态 如果mstatus的bits[3]为1 则恢复全局中断
 */
COMPAT_STATIC_INLINE void osal_null_irq_restore(uint32_t mstatus)
{
    if (mstatus & 8U) 
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
}

/**
 * @brief 等待中断 低功耗指令
 */
COMPAT_STATIC_INLINE void osal_null_wfi(void)
{
    __asm__ volatile("wfi");
}

#else

/**
 * @brief 关全局中断 其他架构 直接返回0
 */
COMPAT_STATIC_INLINE uint32_t osal_null_irq_disable(void) { return 0U; }
COMPAT_STATIC_INLINE void osal_null_irq_restore(uint32_t primask) { (void)primask; }
COMPAT_STATIC_INLINE void osal_null_wfi(void) { }

#endif

/* ── ISR 嵌套计数 (osal_null_isr_enter/exit 维护, 与 IPSR 联合判定) ── */
static volatile uint32_t s_isr_nest;

void osal_null_isr_enter(void)
{
    s_isr_nest++;
}

void osal_null_isr_exit(void)
{
    if (s_isr_nest > 0U)
        s_isr_nest--;
}

/* ── 互斥锁 (原子 CAS + 递归深度) ── */
struct osal_mutex
{
    osal_mutex_type_t   type;/**<互斥锁类型*/
    COMPAT_ATOMIC_UINT  lock;/**<互斥锁状态*/
    COMPAT_ATOMIC_UINT  depth;/**<互斥锁深度*/
};
_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,"osal_null: OSAL_MUTEX_STORAGE_SIZE too small");

/**
 * @brief 互斥锁初始化
 * @param mutex 互斥锁结构体指针
 * @param type 互斥锁类型
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
    COMPAT_ATOMIC_STORE(&mutex->lock, 0U, COMPAT_MO_RELEASE);
    COMPAT_ATOMIC_STORE(&mutex->depth, 0U, COMPAT_MO_RELEASE);
    return OSAL_OK;
}

/**
 * @brief 互斥锁池
 * @details 互斥锁池 主要是互斥锁的缓冲区 互斥锁的使用情况 互斥锁的池控制句柄
 */
static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t           s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t       s_mutex_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 互斥锁池初始化
 * @details 互斥锁池初始化 主要是互斥锁的缓冲区 互斥锁的使用情况 互斥锁的池控制句柄
 */
pre_execution(150)
static void osal_null_mutex_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE));
}


/**
 * @brief 检查是否在中断上下文 (读 CPU 异常状态寄存器)
 */
int osal_in_isr(void)
{
    if (s_isr_nest > 0U)
        return 1;

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
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
 * @param pool 槽位池结构体指针
 * @param used_slots 槽位使用情况指针
 * @param count 槽位数量
 * @return 结果
 * @details 初始化槽位池时, 将槽位使用情况指针设置为0
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* used_slots, size_t count)
{
    if (!pool || !used_slots || count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = used_slots;
    pool->slot_count = count;

    for (size_t i = 0; i < count; i++)
        used_slots[i] = 0;

    return OSAL_OK;
}

/**
 * @brief 申请槽位
 * @param pool 槽位池结构体指针
 * @return 槽位索引
 * @details 申请槽位时, 先禁用中断, 然后遍历槽位使用情况指针, 找到第一个未使用的槽位, 然后返回槽位索引
 * @details 如果遍历完所有槽位都没有找到未使用的槽位, 则返回 OSAL_ERR_NOMEM
 */
int osal_pool_claim(osal_pool_t* pool) 
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;

    uint32_t irq = osal_null_irq_disable();
    int claimed = -1;
    for (size_t i = 0; i < pool->slot_count; i++)
    {
        if (!pool->used_slots[i])
        {
            pool->used_slots[i] = 1;
            claimed = (int)i;
            break;
        }
    }
    osal_null_irq_restore(irq);
    return claimed;
}

/**
 * @brief 释放槽位
 * @param pool 槽位池结构体指针
 * @param idx 槽位索引
 * @return void
 * @details 释放槽位时, 直接将槽位使用情况指针设置为0
 * @details 如果槽位索引无效, 则返回
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

bool osal_pool_is_used(osal_pool_t* pool, int idx)
{
    if (!pool || !pool->used_slots || idx < 0 || (size_t)idx >= pool->slot_count)
        return false;
    return pool->used_slots[idx] != 0U;
}

/**
 * @brief 队列索引
 * @param queue 队列句柄
 * @return 队列索引
 */
COMPAT_STATIC_INLINE int queue_index_of(osal_queue_handle_t queue)
{
    if (!queue) return -1;
    int idx = (int)((struct osal_queue_obj*)queue - s_queues);/**<计算出队列在静态队列池中的索引queue的地址减去全局起始地址>*/
    if (idx < 0 || idx >= OSAL_NULL_MAX_QUEUES || !s_queue_used[idx])
        return -1;
    return idx;
}

/**
 * @brief 自旋锁
 * @details 默认 CONFIG_OSAL_SPINLOCK_IRQ_DISABLE: 单核下退化为关中断临界区,
 * @details  与 RT-Thread 后端语义一致, 保留 nest 支持嵌套 lock/unlock.
 * @details  CONFIG_OSAL_SPINLOCK_ATOMIC: 与 FreeRTOS 非 ESP 后端一致,
 * @details  使用原子 test-and-set 忙等自旋锁 (仅适合 SMP).
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    uint32_t irq_saved;
    uint32_t nest;
#else
    volatile int locked;
#endif
};

/**
 * @brief 初始化自旋锁可以原子嵌套
 * @param lock 自旋锁指针
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    lock->irq_saved = 0U;
    lock->nest      = 0U;
#else
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 锁定自旋锁可以原子嵌套
 * @param lock 自旋锁指针
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    uint32_t irq = osal_null_irq_disable();
    if (lock->nest == 0U)
        lock->irq_saved = irq;
    lock->nest++;
#else
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE));
#endif
    return OSAL_OK;
}

/**
 * @brief 解锁自旋锁可以原子嵌套
 * @param lock 自旋锁指针
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
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
 * @param lock 自旋锁指针
 * @return 是否锁定
 */
COMPAT_STATIC_INLINE bool osal_spinlock_is_locked(struct osal_spinlock* lock)
{
    if (!lock) return false;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    return lock->nest != 0U;
#else
    return __atomic_load_n(&lock->locked, __ATOMIC_ACQUIRE) != 0;
#endif
}

/**
 * @brief 获取时间基于时基系统的tick计数
 * @return 时间(毫秒)
 */
uint32_t osal_time_ms(void)
{
    return COMPAT_ATOMIC_LOAD(&g_scheduler.tick_count, COMPAT_MO_RELAXED);
}


#define OSAL_NULL_TICK_HANG_THRESHOLD   10000U
/**
 * @brief 延迟ms
 * @param ms 延迟时间(毫秒)
 * @note 若time时基系统启动后默认失败返回 
 * @warning 由于wfi会被其他中断唤醒所以我将连续10000次tick未推进作为时基系统未运行或中断被意外屏蔽的判断条件若你觉得不安全可以上调阈值
 * @note 如果时基系统未运行或中断被意外屏蔽，则退出死等以防硬死锁
 */
void osal_delay_ms(uint32_t ms)
{
    if (ms == 0) return;
 
    uint32_t start = osal_time_ms();
    uint32_t last  = start;
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

/**
 * @brief 将毫秒转换为tick
 * @note 裸机适配层不支持时间戳转换为tick 因为时基系统就是默认1ms 1tick=1ms只是为了和os保持一致
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return ms;
}

/**
 * @brief 将超时时间(毫秒)转换为tick
 * @param timeout_ms 超时时间(毫秒)
 * @return tick
 * @details 裸机适配层不支持时间戳转换为tick 因为时基系统就是默认1ms 1tick=1ms只是为了和os保持一致
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return UINT32_MAX;
    return timeout_ms;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  内存 (不推荐使用)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief 分配内存
 * @param count 内存块数量
 * @param size 内存块大小
 * @return 内存指针
 * @details 分配内存 用于分配内存
 */
void* osal_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

/**
 * @brief 释放内存
 * @param ptr 内存指针
 * @details 释放内存 用于释放内存
 */
int osal_free(void* ptr)
{
    free(ptr);
    return OSAL_OK;
}

/**
 * @brief 创建互斥锁
 * @param out 互斥锁指针
 * @param type 互斥锁类型
 * @return 错误码
 * @details 创建互斥锁 用于创建互斥锁
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

    if (osal_mutex_init(&s_mutex_pool[idx], type) != OSAL_OK)/**<初始化互斥锁>*/
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, idx));/**<释放互斥锁>*/
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)&s_mutex_pool[idx];/**<返回互斥锁指针>*/
    return OSAL_OK;
}

/**
 * @brief 创建静态互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储空间
 * @param storage_size 静态互斥锁存储空间大小
 * @param type 互斥锁类型
 * @return 错误码
 * @details 创建静态互斥锁 用于创建静态互斥锁
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) 
        return OSAL_ERR_INVAL;
    if (osal_in_isr()) 
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) 
        return OSAL_ERR_INVAL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type) != OSAL_OK) 
        return OSAL_ERR_NOMEM;
    *out = m;
    return OSAL_OK;
}

/**
 * @brief 创建互斥锁
 * @param out 互斥锁指针
 * @return 错误码
 * @details 创建互斥锁 用于创建互斥锁
 */
int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建静态互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储空间
 * @param storage_size 静态互斥锁存储空间大小
 * @return 错误码
 * @details 创建静态互斥锁 用于创建静态互斥锁
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建递归互斥锁
 * @param out 互斥锁指针
 * @return 错误码
 * @details 创建递归互斥锁 用于创建递归互斥锁
 */
int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 创建静态递归互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储空间
 * @param storage_size 静态互斥锁存储空间大小
 * @return 错误码
 * @details 创建静态递归互斥锁 用于创建静态递归互斥锁
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 创建普通互斥锁
 * @param out 互斥锁指针
 * @return 错误码
 * @details 创建普通互斥锁 用于创建普通互斥锁
 */
int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建静态普通互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储空间
 * @param storage_size 静态互斥锁存储空间大小
 * @return 错误码
 * @details 创建静态普通互斥锁 用于创建静态普通互斥锁
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 销毁互斥锁
 * @param mutex 互斥锁指针
 * @return 错误码
 * @details 销毁互斥锁 用于销毁互斥锁
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex) 
        return;
    if (osal_in_isr()) 
        return;

    COMPAT_ATOMIC_STORE(&mutex->lock, 0U, COMPAT_MO_RELEASE);/**<释放互斥锁>*/
    COMPAT_ATOMIC_STORE(&mutex->depth, 0U, COMPAT_MO_RELEASE);/**<释放互斥锁深度>*/

    int idx = (int)(mutex - s_mutex_pool);/**<计算互斥锁在静态互斥锁池中的索引>*/
    if (idx >= 0 && idx < OSAL_MUTEX_POOL_SIZE)
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, idx));/**<释放互斥锁索引>*/
}

/**
 * @brief 尝试获取互斥锁
 * @param mutex 互斥锁指针
 * @return 错误码
 * @details AMP 模式: 原子 CAS + depth 递增, 支持跨核竞争.
 *          普通模式: 关中断后用 if 判断, 单核下无竞争.
 */
static int osal_mutex_try_acquire(struct osal_mutex* mutex)
{
#ifdef CONFIG_AMP_MODE
    uint32_t expected = 0;
    if (COMPAT_ATOMIC_CAS(&mutex->lock, &expected, 1,COMPAT_MO_ACQUIRE, COMPAT_MO_RELAXED))
    {
        COMPAT_ATOMIC_STORE(&mutex->depth, 1, COMPAT_MO_RELAXED);
        return OSAL_OK;
    }

    if (mutex->type == OSAL_MUTEX_RECURSIVE)
    {
        uint32_t depth = COMPAT_ATOMIC_LOAD(&mutex->depth, COMPAT_MO_RELAXED);
        if (depth == 0U)
            return OSAL_ERR_BUSY;

        COMPAT_ATOMIC_STORE(&mutex->depth, depth + 1U, COMPAT_MO_RELAXED);
        return OSAL_OK;
    }

    return OSAL_ERR_BUSY;
#else
    uint32_t irq = osal_null_irq_disable();
    if (mutex->lock == 0U)
    {
        mutex->lock  = 1U;
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

        if (timeout_ms != OSAL_WAIT_FOREVER &&
            (osal_time_ms() - start) >= timeout_ms)
            return OSAL_ERR_TIMEOUT;

        osal_null_wfi();
    }
}

/**
 * @brief 解锁互斥锁
 * @param mutex 互斥锁指针
 * @return 错误码
 * @details 解锁互斥锁 用于解锁互斥锁
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex) return OSAL_ERR_INVAL;
    if (osal_in_isr()) return OSAL_ERR_ISR;

#ifdef CONFIG_AMP_MODE
    uint32_t depth = COMPAT_ATOMIC_LOAD(&mutex->depth, COMPAT_MO_RELAXED);
    if (depth == 0U)
        return OSAL_ERR_IO;

    if (depth > 1U)
    {
        COMPAT_ATOMIC_STORE(&mutex->depth, depth - 1U, COMPAT_MO_RELAXED);
        return OSAL_OK;
    }

    COMPAT_ATOMIC_STORE(&mutex->depth, 0U, COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&mutex->lock, 0U, COMPAT_MO_RELEASE);
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
    mutex->lock  = 0U;
    osal_null_irq_restore(irq);
    return OSAL_OK;
#endif
}

/**
 * @brief 周期任务 stub 函数 可以不用这个直接xTask直接写逻辑也可以用这个包装一下虽然也没什么用就为了对其OS吧但是用了就是要双侵入式链表走定时器唤醒这条路
 * @details 建议直接原生xTask直接写逻辑毕竟把状态机换成真正的task就可以了也改不了多少作者一般也不用这个抽象
 * @param param 周期任务包装指针
 * @return void
 * @details 模拟周期触发行为
 */
__attribute__((unused))
static void osal_periodic_task_stub(void* param) 
{
    struct osal_periodic_task_wrap* wrap = (struct osal_periodic_task_wrap*)param;
    if (!wrap || !wrap->orig_callback) 
        return;

    /**<模拟周期任务死循环> */
    while (1)
    {
        uint32_t start_time = osal_time_ms();

        /**<执行os期望的回调函数> */
        wrap->orig_callback(wrap->x_task);

        /**<计算执行耗时，精准补偿延时，防止时间漂移> */
        uint32_t cost = osal_time_ms() - start_time;
        if (cost < wrap->period_ms)
            osal_delay_ms(wrap->period_ms - cost); 
        else
            /**<如果业务执行时间已经超过了周期，直接让出 CPU 周期（Yield）> */
            osal_delay_ms(1); 
    }
}

/**
 * @brief 创建任务
 * @param name 任务名称
 * @param stack_size 任务栈大小
 * @param priority 任务优先级
 * @param entry 任务入口函数
 * @param param 任务参数
 * @param core_id 任务所在核心ID
 * @return 错误码
 * @details 创建任务 用于创建任务
 */
int osal_task_create(const char* name, uint32_t stack_size,uint32_t priority, osal_task_entry_t entry,void* param, int core_id)
{
    (void)name; (void)stack_size; (void)priority;
    (void)entry; (void)param; (void)core_id;
    return OSAL_ERR_NOTSUPP;
}

int osal_task_create_handle(const char* name, uint32_t stack_size,uint32_t priority, osal_task_entry_t entry,void* param, int core_id,osal_task_handle_t* out_handle)
{
    if (!out_handle) return OSAL_ERR_INVAL;
    (void)name; (void)stack_size; (void)priority;
    (void)entry; (void)param; (void)core_id;
    *out_handle = NULL;
    return OSAL_ERR_NOTSUPP;
}

void osal_task_self_delete(void)
{
    while (1)
        osal_null_wfi();
}

void osal_task_delete(osal_task_handle_t task)
{
    (void)task;
}

bool osal_task_is_running(osal_task_handle_t task)
{
    (void)task;
    return false;
}

const char* osal_task_get_name(osal_task_handle_t task)
{
    (void)task;
    return "baremetal";
}

uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    (void)task;
    return 0U;
}

/*===============================================================================================================*/
                        /*信号量推荐走全局不要自己定义非全局信号量,信号量基本不需要全局监督某一个资源*/
/*===============================================================================================================*/
/**
 * @brief 二值信号量
 * @param signaled 信号量状态
 * @param from_pool 是否从池中分配
 */
struct osal_sem
{
    COMPAT_ATOMIC_UINT signaled;
    bool               from_pool;
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE,"OSAL_SEM_STORAGE_SIZE too small");

/**
 * @brief 二值信号量池
 * @param s_sem_pool 信号量池
 * @param s_sem_used 信号量使用情况
 * @param s_sem_pool_ctrl 信号量池控制
 */
static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t         s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t     s_sem_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(151)
static void osal_null_sem_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE));
}

/**
 * @brief 创建二值信号量
 * @param out 信号量指针
 * @return 错误码
 * @details 创建二值信号量 
 */
int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_sem* sem = &s_sem_pool[idx];
    COMPAT_ATOMIC_STORE(&sem->signaled, 0U, COMPAT_MO_RELEASE);
    sem->from_pool = true;
    *out = sem;
    return OSAL_OK;
}


/**
 * @brief 创建静态二值信号量
 * @param out 信号量指针
 * @param storage 存储空间
 * @param storage_size 存储空间大小
 * @return 错误码
 * @details 创建静态二值信号量 
 */
int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return OSAL_ERR_INVAL;

    struct osal_sem* sem = (struct osal_sem*)storage;
    COMPAT_ATOMIC_STORE(&sem->signaled, 0U, COMPAT_MO_RELEASE);
    sem->from_pool = false;
    *out = sem;
    return OSAL_OK;
}

/**
 * @brief 销毁二值信号量
 * @param sem 信号量指针
 * @return 错误码
 * @details 销毁二值信号量 用于销毁二值信号量
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem)
        return;

    COMPAT_ATOMIC_STORE(&sem->signaled, 0U, COMPAT_MO_RELEASE);
    if (sem->from_pool)
    {
        int idx = (int)(sem - s_sem_pool);/**<计算信号量在信号量池中的索引>*/
        if (idx >= 0 && idx < OSAL_SEM_POOL_SIZE)/**<判断信号量在信号量池中的索引是否有效>*/
            COMPAT_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
    }
}

/**
 * @brief 尝试等待二值信号量
 * @param sem 信号量指针
 * @return 错误码
 */
static int osal_sem_try_wait(struct osal_sem* sem)
{
    uint32_t expected = 1U;
    if (COMPAT_ATOMIC_CAS(&sem->signaled, &expected, 0U, COMPAT_MO_ACQUIRE, COMPAT_MO_RELAXED))
        return OSAL_OK;
    return OSAL_ERR_BUSY;
}

/**
 * @brief 等待二值信号量
 * @param sem 信号量指针
 * @param timeout_ms 超时时间
 * @return 错误码
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
            osal_null_wfi();
        return OSAL_OK;
    }

    uint32_t start = osal_time_ms();
    while (osal_sem_try_wait(sem) != OSAL_OK)
    {
        if ((osal_time_ms() - start) >= timeout_ms)
            return OSAL_ERR_TIMEOUT;
        osal_null_wfi();
    }
    return OSAL_OK;
}

/**
 * @brief 发布二值信号量
 * @param sem 信号量指针
 * @return 是否成功
 */
bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem)
        return false;
    COMPAT_ATOMIC_STORE(&sem->signaled, 1U, COMPAT_MO_RELEASE);
    return true;
}

/**
 * @brief 从 ISR 发布二值信号量因为本身就是原子操作所以可以直接在isr中直接发布而且是裸机可以不考虑yield_required
 * @param sem 信号量指针
 * @param px_yield_required 是否需要让出 CPU 周期
 * @return 是否成功
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    COMPAT_IGNORE_RESULT(px_yield_required);
    return osal_sem_post(sem);
}

/**
 * @brief 从 ISR 让出 CPU 周期本身就没有yield_required所以直接返回
 * @param yield_required 是否需要让出 CPU 周期
 */
void osal_yield_from_isr(bool yield_required)
{
    COMPAT_IGNORE_RESULT(yield_required);
}


/**
 * @brief 创建队列
 * @param queue_len 队列长度
 * @param item_size 每个元素大小
 * @return 队列句柄
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    if (queue_len == 0 || item_size == 0)
        return NULL;

    if ((queue_len & (queue_len - 1)) != 0)
        COMPAT_TRAP();

    /**< item_size 必须是 sizeof(Fifo_Data_type) 的整数倍, 否则无法直接 cast 做块读写 */
    if (item_size % sizeof(Fifo_Data_type) != 0)
        return NULL;

    size_t elements_per_item = item_size / sizeof(Fifo_Data_type);/**<计算出一个消息 item 占用了多少个 FIFO 基础单元>*/
    size_t total_elements    = queue_len * elements_per_item;/**<计算总元素个数,因为底层fifo就没有%和//所以这里必须2的整数倍>*/

    if ((total_elements & (total_elements - 1)) != 0)/**<判断总元素个数是否为2的整数倍>*/
        COMPAT_TRAP();

    if (total_elements > OSAL_NULL_QUEUE_ELEM_COUNT)
        return NULL;

    int idx = osal_pool_claim(&s_queue_pool_ctrl);
    if (idx < 0) 
        return NULL;

    struct osal_queue_obj* queue = &s_queues[idx];
    queue->elements_per_item = elements_per_item;/**<设置每个队列元素包含的元素个数>*/
    fifo_init(&queue->fifo, queue->buf, (uint16_t)total_elements);

    return (osal_queue_handle_t)queue;
}

/**
 * @brief 删除队列
 * @param queue 队列句柄
 * @return 错误码
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
    int idx = queue_index_of(queue);
    if (idx < 0) 
        return;
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_queue_pool_ctrl, idx));
}

/**
 * @brief 发送队列
 * @param queue 队列句柄
 * @param item 发送数据
 * @return 是否成功
 */
static bool queue_send_internal(osal_queue_handle_t queue, const void* item)
{
    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    uint16_t epi = (uint16_t)q->elements_per_item;

    /**< SPSC 安全: 消费者只能释放空间不会缩减, 检查通过则写入必然完整就是如果你传的格子数大小超过剩余各子数那么就会返回false */
    if ((uint16_t)(q->fifo.size - fifo_get_count(&q->fifo)) < epi)
        return false;

    return fifo_write_block(&q->fifo, (const Fifo_Data_type*)item, epi) == epi;
}

/**
 * @brief 发送队列
 * @param queue 队列句柄
 * @param item 发送数据
 * @param timeout_ms 超时时间
 * @return 是否成功
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (osal_in_isr())
        return false;

    return queue_send_internal(queue, item);
}

/**
 * @brief 从 ISR 发送队列因为本身就是原子操作所以可以直接在isr中直接发送而且是裸机可以不考虑yield_required
 * @param queue 队列句柄
 * @param item 发送数据
 * @param px_yield_required 
 * @return 是否成功
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;
    return queue_send_internal(queue, item);
}

/**
 * @brief 接收队列
 * @param queue 队列句柄
 * @param item 接收数据
 * @param timeout_ms 超时时间
 * @return 是否成功
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    uint16_t epi = (uint16_t)q->elements_per_item;

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (fifo_get_count(&q->fifo) < epi)
            osal_null_wfi();
    }
    else if (timeout_ms > 0)
    {
        uint32_t start = osal_time_ms();
        while (fifo_get_count(&q->fifo) < epi)
        {
            if ((osal_time_ms() - start) >= timeout_ms) 
                return false;
            osal_null_wfi();
        }
    }

    if (fifo_get_count(&q->fifo) < epi) 
        return false;

    return fifo_read_block(&q->fifo, (Fifo_Data_type*)item, epi) == epi;
}

/**
 * @brief 从 ISR 接收队列把延迟删了的队列而已
 * @param queue 队列句柄
 * @param item 接收数据
 * @param px_yield_required 是否需要让出 CPU 周期
 * @return 是否成功
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    COMPAT_IGNORE_RESULT(px_yield_required);

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) /**<判断队列句柄是否有效且接收数据指针是否有效>*/
        return false;

    struct osal_queue_obj* q = &s_queues[idx];
    uint16_t epi = (uint16_t)q->elements_per_item;

    if (fifo_get_count(&q->fifo) < epi) 
        return false;

    return fifo_read_block(&q->fifo, (Fifo_Data_type*)item, epi) == epi;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  硬件安全关断 & 日志
 * ═══════════════════════════════════════════════════════════════════════════ */
/**
 * @brief 硬件安全关断 陷入指令执行
 * @return 错误码
 */
COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

/**
 * @brief 错误关断自己写自己的错误处理逻辑比如打印日志或者重启设备
 * @return 错误码
 */
COMPAT_WEAK void osal_panic_interlock(void)
{
}

/**
 * @brief 硬件安全关断 冻结调度裸机就是关中断
 * @return 错误码
 */
void osal_sched_freeze(void)
{
    COMPAT_IGNORE_RESULT(osal_null_irq_disable());
}

/**
 * @brief 硬件安全关断 冻结中断裸机就是关中断
 * @return 错误码
 */
void osal_int_freeze(void)
{
    (void)osal_null_irq_disable();
}

/**
 * @brief 日志
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 日志格式
 * @return 错误码
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
 * @brief 日志 致命错误
 * @param fmt 日志格式
 * @return 错误码
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
 * @brief 日志 严重错误
 * @param file 文件名
 * @param line 行号
 * @param fmt 日志格式
 * @return 错误码
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

#endif /* CONFIG_OSAL_NULL */
