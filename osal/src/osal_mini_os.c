/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file osal_mini_os.c
 *@brief osal mini-os (lib/mini-os 自研 RTOS) 后端实现
 *@author H-000-H
 *@details
 *   osal_mini_os.c — OSAL mini-os 后端实现
 *   将 OSAL API 映射到 mini_os_thread/mini_os_mutex/mini_os_semaphore/
 *   mini_os_queue 等内核原语; 互斥锁/信号量静态内嵌内核对象 + 槽位池
 *   (osal_pool), 池临界区用 mini_os_irq_save/restore (可嵌套关中断).
 *   关键差异 (其他后端对齐参考 osal_freertos.c 文件头):
 *   1. 优先级语义: mini-os 数字越小越优先 (同 RT-Thread), 与 FreeRTOS 相反,
 *      切换后端时需留意 (OSAL 约定每后端用所属内核原生语义);
 *   2. ISR 模式: mini-os 的 *_isr API 不主动触发上下文切换, ISR 出口统一调
 *      mini_os_schedule_yield_isr() (内部自判就绪位图, 仅更高优先级就绪才
 *      置 PendSV); 本层 osal_yield_from_isr() 即转发该调用;
 *   3. 信号量原生二值 (max_count=1), 多次 post 天然合并计数 <= 1, 无需本层
 *      自实现 posted 标志;
 *   4. osal_scheduler_start() 内部先惰性引导内核 (schedule_init + idle 线程
 *      + SysTick), 再启动调度器, 正常情况下不返回;
 *   5. osal_calloc/osal_free 走 mini-os 自带堆 (链接脚本 mini-os-heap.ld 提供
 *      __heap_start/__heap_end), 不用 libc;
 *   6. mini-os 依赖启动构造函数 (heap/注册表/idle 线程自初始化), 要求启动
 *      流程遍历 .init_array (GCC/Clang 工具链默认满足);
 *   7. 板级接线: SysTick_Handler -> mini_os_systick_handler(),
 *      PendSV_Handler -> pendsv_handler() (小写, 见 port.S);
 *   8. 无调度器全局挂起 (suspend-all) API, osal_sched_freeze() 退化为关中断
 *      (与 osal_null 一致的单向冻结语义).
 */

#ifdef CONFIG_OSAL_MINI_OS

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "board_config.h"
#include "compiler_compat.h"
#include "config.h"
#include "event.h"
#include "mutex.h"
#include "osal.h"
#include "queue.h"
#include "schedule.h"
#include "semaphore.h"
#include "spinlock.h"
#include "status.h"
#include "thread.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* 错误码映射 (mini-os err.h 在可见 config.h/status.h 时数值已与 OSAL 对齐) */
/* -------------------------------------------------------------------------- */
/**
 * @brief mini-os 错误码转 OSAL 错误码
 * @param[in] err mini-os 错误码
 * @return OSAL 错误码
 * @details MINI_OS_ERR_* 与 OSAL_ERR_* 数值一致时零开销直通;
 *          仅 MINI_OS_ERR_AGAIN (非阻塞竞争/队列满空) 需要对齐为
 *          OSAL_ERR_TIMEOUT (FreeRTOS 后端同样以 TIMEOUT 表达take失败).
 */
static int osal_err_from_mini_os(mini_os_err_t err)
{
    if (err == MINI_OS_ERR_AGAIN)
        return OSAL_ERR_TIMEOUT;
    return (int)err;
}

/**
 * @brief 超时毫秒转 mini-os tick
 * @param[in] timeout_ms 超时毫秒数 (OSAL_WAIT_FOREVER 永久等待)
 * @return mini-os tick (MINI_OS_WAIT_FOREVER / 按内核 tick 频率换算)
 */
MINI_STATIC_INLINE mini_os_tick_t osal_mini_os_timeout(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return MINI_OS_WAIT_FOREVER;
    return (mini_os_tick_t)MINI_OS_MS_TO_TICK(timeout_ms);
}

/**
 * @brief 判定是否在 mini-os ISR 上下文
 * @return 1 在 ISR 中, 0 不在
 * @details ARMv7-M/v8-M 读 IPSR; RISC-V 读 mcause bit31; 其他架构保守返回 0
 */
int osal_in_isr(void)
{
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

/* -------------------------------------------------------------------------- */
/* 槽位池 (每池独立可嵌套关中断临界区) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 进入槽位池临界区 (mini_os_irq_save 可嵌套)
 * @param[out] irq_level 保存的中断状态
 */
MINI_STATIC_INLINE mini_os_irq_t osal_pool_lock(void) { return mini_os_irq_save(); }

/**
 * @brief 退出槽位池临界区
 * @param[in] irq_level osal_pool_lock 返回的中断状态
 */
MINI_STATIC_INLINE void osal_pool_unlock(mini_os_irq_t irq_level) { mini_os_irq_restore(irq_level); }

/**
 * @brief 初始化槽位池
 * @param[in] pool 池
 * @param[in] used_slots 数组
 * @param[in] slot_count 数量
 * @return OSAL_OK 或 OSAL_ERR_INVAL
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* used_slots, size_t slot_count)
{
    if (!pool || !used_slots || slot_count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = used_slots;
    pool->slot_count = slot_count;

    for (size_t iter_index = 0; iter_index < slot_count; iter_index++)
        used_slots[iter_index] = 0;

    return OSAL_OK;
}

/**
 * @brief 申请槽位
 * @param[in] pool 池
 * @return 索引或负错误码
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;

    mini_os_irq_t irq = osal_pool_lock();

    int ret_idx = -1;
    for (size_t iter_index = 0; iter_index < pool->slot_count; iter_index++)
    {
        if (!pool->used_slots[iter_index])
        {
            pool->used_slots[iter_index] = 1;
            ret_idx = (int)iter_index;
            break;
        }
    }

    osal_pool_unlock(irq);
    return ret_idx;
}

/**
 * @brief 释放槽位
 * @param[in] pool 池
 * @param[in] slot_index 索引
 * @return OSAL_OK 或 OSAL_ERR_INVAL
 */
int osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return OSAL_ERR_INVAL;

    mini_os_irq_t irq = osal_pool_lock();
    pool->used_slots[slot_index] = 0;
    osal_pool_unlock(irq);
    return OSAL_OK;
}

/**
 * @brief 查询槽占用
 * @param[in] pool 池
 * @param[in] slot_index 索引
 * @return true 已占用
 */
bool osal_pool_is_used(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return false;
    return pool->used_slots[slot_index] != 0U;
}

/* -------------------------------------------------------------------------- */
/* 自旋锁 (直接复用内核 inc/spinlock.h 的 mini_os_spinlock_t) */
/* -------------------------------------------------------------------------- */
/* 一致性检查: 锁模式是编译期内核 ABI (mini_os_spinlock_t 的成员随
 * MINI_OS_SPINLOCK_ATOMIC 切换), OSAL 层的 CONFIG_OSAL_SPINLOCK_* 无法覆盖,
 * 对 mini-os 后端无效; 两边不一致时提醒, 避免误以为改了 OSAL 开关就生效。 */
#if defined(CONFIG_OSAL_SPINLOCK_ATOMIC) && !MINI_OS_SPINLOCK_ATOMIC
#warning "osal_mini_os: CONFIG_OSAL_SPINLOCK_ATOMIC is ignored by the mini-os backend, enable CONFIG_MINI_OS_SPINLOCK_ATOMIC instead"
#elif defined(CONFIG_OSAL_SPINLOCK_IRQ_DISABLE) && MINI_OS_SPINLOCK_ATOMIC
#warning "osal_mini_os: CONFIG_OSAL_SPINLOCK_IRQ_DISABLE is ignored by the mini-os backend, disable CONFIG_MINI_OS_SPINLOCK_ATOMIC instead"
#endif

/**
 * @details 本层不另立自旋锁实现, 只内嵌内核对象并转发 (NULL 检查 +
 *          错误码直通); 锁的具体行为完全由内核 Kconfig 决定:
 *          - CONFIG_MINI_OS_SPINLOCK_ATOMIC=n (默认, 单核):
 *            mini_os_irq_save/restore + 嵌套计数, 可重入, 最外层记住
 *            中断恢复点, 等效于一个可重入的临界区锁;
 *          - CONFIG_MINI_OS_SPINLOCK_ATOMIC=y (SMP):
 *            TAS 忙等, 失败后 mini_os_pause() 延迟 +
 *            mini_os_schedule_yield() 让出, 不可重入。
 *          CONFIG_MINI_OS_SPINLOCK=n 时内核头不定义任何实体,
 *          本层退化到 irq_save/restore 兜底, 保证 OSAL 公共 API 仍可编译。
 * @note 锁模式不在 OSAL 层重复开关: 旧写法用 CONFIG_OSAL_SPINLOCK_*
 *       自实现一套会与内核对象并存, 两者语义/布局不一致容易误用。
 */
struct osal_spinlock
{
#if MINI_OS_SPINLOCK
    mini_os_spinlock_t obj; /**< mini-os 内核自旋锁对象 (静态内嵌) */
#else
    mini_os_irq_t irq_saved; /**< 兜底: 最外层加锁时保存的中断状态 */
    uint32_t      nest;      /**< 兜底: 嵌套深度 */
#endif
};

_Static_assert(sizeof(struct osal_spinlock) <= OSAL_SPINLOCK_STORAGE_SIZE, "osal_mini_os: OSAL_SPINLOCK_STORAGE_SIZE too small");

/**
 * @brief 初始化自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 * @details 转发 mini_os_spinlock_init(): 单核模式清零 nest 与中断恢复点,
 *          原子模式以 release 语义写 0 (未锁)。
 * @note 必须在任何 lock/unlock 之前调用; 静态零初始化的存储已等价于
 *       未锁状态, 但显式 init 可读且不依赖零值布局约定。
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#if MINI_OS_SPINLOCK
    return osal_err_from_mini_os(mini_os_spinlock_init(&lock->obj));
#else
    lock->irq_saved = 0;
    lock->nest = 0U;
    return OSAL_OK;
#endif
}

/**
 * @brief 获取自旋锁 (ISR 安全, 禁止睡眠)
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 * @details 转发 mini_os_spinlock_lock(): 单核模式 nest++ 并在最外层
 *          mini_os_irq_save(); 原子模式自旋至 TAS 成功。
 * @warning 单核模式下中断已被屏蔽, 持锁区间内禁止调用任何可能阻塞的
 *          OSAL API (互斥锁/信号量/队列/延时), 否则会死锁。
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#if MINI_OS_SPINLOCK
    return osal_err_from_mini_os(mini_os_spinlock_lock(&lock->obj));
#else
    if (lock->nest == 0U)
        lock->irq_saved = mini_os_irq_save();
    lock->nest++;
    return OSAL_OK;
#endif
}

/**
 * @brief 释放自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空或未加锁返回 OSAL_ERR_INVAL
 * @details 转发 mini_os_spinlock_unlock(): 单核模式 nest-- 并仅在回到
 *          最外层时 mini_os_irq_restore(); 原子模式以 release 语义写 0。
 * @note 与兜底分支不同, 内核实现在 nest==0 时解锁会报 INVAL (不静默
 *       吞掉), 多余的 unlock 因此可被调用方发现。
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#if MINI_OS_SPINLOCK
    return osal_err_from_mini_os(mini_os_spinlock_unlock(&lock->obj));
#else
    if (lock->nest > 0U)
    {
        lock->nest--;
        if (lock->nest == 0U)
            mini_os_irq_restore(lock->irq_saved);
    }
    return OSAL_OK;
#endif
}

/**
 * @brief 检查自旋锁是否被持有
 * @param[in] lock 自旋锁指针
 * @return true 已持有; false 未持有或 lock 为空
 * @details 转发 mini_os_spinlock_islocked() (内核用 out 参数回传结果,
 *          本层折成 bool); 单核模式看 nest > 0, 原子模式看 locked != 0。
 * @note 仅作诊断用: 查询结果返回时已可能失效, 不得用作加锁前的试探。
 */
MINI_UNUSED MINI_STATIC_INLINE bool osal_spinlock_is_locked(struct osal_spinlock* lock)
{
    if (!lock)
        return false;
#if MINI_OS_SPINLOCK
    {
        mini_os_bool_t locked = MINI_OS_FALSE;

        if (mini_os_spinlock_islocked(&lock->obj, &locked) != MINI_OS_OK)
            return false;
        return locked != MINI_OS_FALSE;
    }
#else
    return lock->nest != 0U;
#endif
}

/* -------------------------------------------------------------------------- */
/* 互斥锁 (静态内嵌 mini_os_mutex_t) */
/* -------------------------------------------------------------------------- */
struct osal_mutex
{
    mini_os_mutex_t   obj;  /**< mini-os 内核互斥锁对象 (静态内嵌) */
    osal_mutex_type_t type; /**< 互斥锁类型 */
};

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE, "osal_mini_os: OSAL_MUTEX_STORAGE_SIZE too small");

/**
 * @brief 静态互斥锁池
 */
static struct osal_mutex             s_mutex_pool[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_mutex_used[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_mutex_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void osal_mutex_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE));
}

/**
 * @brief 初始化互斥锁 (在调用方存储上创建内核对象)
 * @param[in] mutex 互斥锁指针
 * @param[in] type 互斥锁类型
 * @return 结果
 */
static int osal_mutex_init(struct osal_mutex* mutex, osal_mutex_type_t type)
{
    if (!mutex)
        return OSAL_ERR_INVAL;

    mutex->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
    {
        if (mini_os_mutex_recuring_create_static(MINI_OS_NULL, &mutex->obj) == MINI_OS_NULL)
            return OSAL_ERR_NOMEM;
    }
    else if (type == OSAL_MUTEX_PLAIN)
    {
        if (mini_os_mutex_create_static(&mutex->obj, MINI_OS_NULL) == MINI_OS_NULL)
            return OSAL_ERR_NOMEM;
    }
    else
    {
        return OSAL_ERR_INVAL;
    }

    return OSAL_OK;
}

/**
 * @brief mini-os 池化互斥锁
 * @param[out] out 等见签名
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

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0)
        return OSAL_ERR_NOMEM;

    struct osal_mutex* mutex_obj = &s_mutex_pool[index];
    if (osal_mutex_init(mutex_obj, type) != OSAL_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, index));
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)mutex_obj;
    return OSAL_OK;
}

/**
 * @brief mini-os 静态存储互斥锁
 * @param[out] out 等见签名
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
    *out = NULL;

    struct osal_mutex* mutex = (struct osal_mutex*)storage;
    if (osal_mutex_init(mutex, type) != OSAL_OK)
        return OSAL_ERR_NOMEM;

    *out = (struct osal_mutex*)mutex;
    return OSAL_OK;
}

/**
 * @brief 池化普通互斥锁 (等价 create_typed PLAIN)
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief 静态存储普通互斥锁
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 池化递归互斥锁
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_recursive(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE); }

/**
 * @brief 静态存储递归互斥锁
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 池化普通互斥锁 (与 create 等价, 强调语义)
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_plain(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief 静态存储普通互斥锁 (与 create_static 等价, 强调语义)
 * @param[out] out 输出
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 销毁互斥锁并归还池槽
 * @param[in] mutex 锁
 * @details 本后端互斥锁均为静态创建 (内嵌存储), 统一走
 *          mini_os_mutex_delete_static; 仅池内对象归还池槽.
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex || osal_in_isr())
        return;

    /**< 先销毁内核对象 (被持有/有等待者时返回 BUSY, 忽略不强制 kill) */
    MINI_IGNORE_RESULT(mini_os_mutex_delete_static(&mutex->obj));

    /**< 再判断是否属于全局互斥锁池: 仅池内对象归还池槽, 静态锁不归还 */
    if (mutex >= s_mutex_pool && mutex < &s_mutex_pool[OSAL_MUTEX_POOL_SIZE])
    {
        size_t idx = (size_t)(mutex - s_mutex_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, (int)idx));
    }
}

/**
 * @brief 锁定互斥锁 (内核自带优先级继承)
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

    return osal_err_from_mini_os(mini_os_mutex_lock(&mutex->obj, osal_mini_os_timeout(timeout_ms)));
}

/**
 * @brief 释放互斥锁
 * @param[in] mutex 锁
 * @return OSAL_OK 或 OSAL_ERR_IO
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR; /* 中断中不允许释放 */

    mini_os_err_t err = mini_os_mutex_unlock(&mutex->obj);
    if (err != MINI_OS_OK)
        return OSAL_ERR_IO;
    return OSAL_OK;
}

/* -------------------------------------------------------------------------- */
/* 二值信号量 (静态内嵌 mini_os_semaphore_t, max_count=1 原生二值) */
/* -------------------------------------------------------------------------- */
struct osal_sem
{
    mini_os_semaphore_t obj;       /**< mini-os 内核信号量对象 (静态内嵌) */
    bool                from_pool; /**< 是否来自静态池 */
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE, "OSAL_SEM_STORAGE_SIZE too small");

/**
 * @brief 静态二值信号量池
 */
static struct osal_sem             s_sem_pool[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                     s_sem_used[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_sem_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_SEM_POOL) static void osal_sem_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE));
}

/**
 * @brief 初始化二值信号量 (在调用方存储上创建内核对象)
 * @param[in] sem 二值信号量指针
 * @return 结果
 */
static int osal_sem_init_binary(struct osal_sem* sem)
{
    if (!sem)
        return OSAL_ERR_INVAL;

    if (mini_os_binary_semaphore_create_static(MINI_OS_NULL, &sem->obj) == MINI_OS_NULL)
        return OSAL_ERR_NOMEM;

    return OSAL_OK;
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
    if (osal_sem_init_binary(sem) != OSAL_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
        return OSAL_ERR_NOMEM;
    }

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
    if (osal_sem_init_binary(sem) != OSAL_OK)
        return OSAL_ERR_NOMEM;

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
    if (!sem || osal_in_isr())
        return;

    /**< 先销毁内核对象 (仍有等待者时返回 BUSY, 忽略) */
    MINI_IGNORE_RESULT(mini_os_semaphore_delete_static(&sem->obj));

    /**< 再判断是否属于全局信号量池: 仅池内对象归还池槽, 静态信号量不归还 */
    if (sem->from_pool && sem >= s_sem_pool && sem < &s_sem_pool[OSAL_SEM_POOL_SIZE])
    {
        size_t idx = (size_t)(sem - s_sem_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, (int)idx));
    }
}

/**
 * @brief 等待信号量
 * @param[in] sem 信号量
 * @param[in] timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    return osal_err_from_mini_os(mini_os_semaphore_take(&sem->obj, osal_mini_os_timeout(timeout_ms)));
}

/**
 * @brief 释放信号量 (task 上下文)
 * @param[in] sem 信号量
 * @return true 成功
 */
bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem || osal_in_isr())
        return false;

    return mini_os_semaphore_give(&sem->obj) == MINI_OS_OK;
}

/**
 * @brief 释放信号量 (ISR 上下文, 不内部 yield)
 * @param[in] sem 信号量
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return true 成功
 * @details mini-os 的 give_isr 不上报唤醒状态: 成功即置 yield 标志,
 *          是否真正切换由 ISR 出口 mini_os_schedule_yield_isr() 自判.
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    if (!sem)
        return false;

    bool posted = mini_os_semaphore_give_isr(&sem->obj) == MINI_OS_OK;
    if (posted && px_yield_required != NULL)
        *px_yield_required = true;
    return posted;
}

/**
 * @brief ISR 出口请求上下文切换
 * @param[in] yield_required 是否需要切换
 * @details 转发 mini_os_schedule_yield_isr(): 内部检查就绪位图, 仅当有
 *          更高优先级线程就绪时才置 PendSV, 重复调用无害.
 */
void osal_yield_from_isr(bool yield_required)
{
    if (yield_required)
        MINI_IGNORE_RESULT(mini_os_schedule_yield_isr());
}

/* -------------------------------------------------------------------------- */
/* 时间 */
/* -------------------------------------------------------------------------- */
/**
 * @brief mini-os 内核 tick 毫秒时钟
 * @return 毫秒
 */
uint32_t osal_time_ms(void)
{
    mini_os_tick_t tick = 0;
    MINI_IGNORE_RESULT(mini_os_get_tick(&tick));
    return (uint32_t)MINI_OS_TICK_TO_MS((mini_os_uint32_t)tick);
}

/**
 * @brief 线程态延时 (mini_os_thread_delay_ms, 让出调度器)
 * @param[in] ms 毫秒
 */
void osal_delay_ms(uint32_t ms)
{
    if (osal_in_isr())
        return; /* 中断中不能阻塞 */
    MINI_IGNORE_RESULT(mini_os_thread_delay_ms(ms));
}

/**
 * @brief 忙等微秒 (不让出调度; 1-Wire 等短时序)
 * @param[in] us 微秒
 */
void osal_delay_us(uint32_t us)
{
    if (us == 0U)
        return;
    /* 粗略忙等: 按 CPU 主频近似周期数 (无可靠 us 时钟源) */
    {
        uint32_t          cycles = us * (MINI_OS_CPU_CLOCK_HZ / 1000000U);
        volatile uint32_t iter_index;
        for (iter_index = 0; iter_index < cycles; iter_index++)
            MINI_UNUSED_PARAM(iter_index);
    }
}

/**
 * @brief 毫秒转 tick (按 MINI_OS_DEFAULT_SYSTICK 换算)
 * @param[in] ms 毫秒
 * @return tick
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms) { return (osal_tick_t)MINI_OS_MS_TO_TICK(ms); }

/**
 * @brief 超时转 tick
 * @param[in] timeout_ms 毫秒
 * @return tick (OSAL_WAIT_FOREVER -> UINT32_MAX)
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return UINT32_MAX;
    return (osal_tick_t)MINI_OS_MS_TO_TICK(timeout_ms);
}

/* -------------------------------------------------------------------------- */
/* 内存 (走 mini-os 自带堆, 不用 libc) */
/* -------------------------------------------------------------------------- */
/**
 * @brief calloc 分配并清零
 * @param[in] count 数量
 * @param[in] size 单元素大小
 * @return 指针; 失败返回 NULL
 */
void* osal_calloc(size_t count, size_t size) { return mini_os_calloc(count, size); }

/**
 * @brief free 释放
 * @param[in] ptr 内存指针 (可为 NULL)
 * @return OSAL_OK
 */
int osal_free(void* ptr)
{
    MINI_IGNORE_RESULT(mini_os_free(ptr));
    return OSAL_OK;
}

/* -------------------------------------------------------------------------- */
/* 内核惰性引导与调度器启动 */
/* -------------------------------------------------------------------------- */
/** @brief 内核引导完成标志 (schedule_init/idle 线程/SysTick 仅执行一次) */
static bool s_kernel_booted;

/**
 * @brief 惰性引导 mini-os 内核
 * @details 必须在任何线程创建前执行: 初始化就绪链表/时间轮, 创建空闲线程
 *          (优先级 MINI_OS_PRIORITY-1, 负责尸体回收), 配置并启动 SysTick
 *          (默认 tick 频率, 板级可覆盖 mini_os_systick_init 弱符号).
 *          不可在 ISR 中调用.
 */
static void osal_mini_os_kernel_boot(void)
{
    if (s_kernel_booted)
        return;

    MINI_IGNORE_RESULT(mini_os_schedule_init());
    mini_os_thread_idle_create();
    mini_os_systick_init(0u); /* 0 = 按 MINI_OS_DEFAULT_SYSTICK 默认 tick 频率 */
    s_kernel_booted = true;
}

/**
 * @brief 钳位到 mini-os 合法优先级 [0, MINI_OS_PRIORITY)
 * @details mini-os 语义: 数字越小越优先 (同 RT-Thread, 与 FreeRTOS 相反).
 */
MINI_STATIC_INLINE uint32_t osal_clamp_task_priority(uint32_t priority)
{
    if (priority >= (uint32_t)MINI_OS_PRIORITY)
        return (uint32_t)(MINI_OS_PRIORITY - 1U);
    return priority;
}

/**
 * @brief 栈字节钳位到内核允许的最小栈
 * @param[in] stack_bytes 栈大小 (字节)
 * @return 钳位后的栈字节数
 */
MINI_STATIC_INLINE uint32_t osal_clamp_stack_size(uint32_t stack_bytes)
{
    if (stack_bytes < (uint32_t)MINI_OS_THREAD_MIN_STACK_SIZE)
        return (uint32_t)MINI_OS_THREAD_MIN_STACK_SIZE;
    return stack_bytes;
}

/**
 * @brief 创建任务 (mini_os_thread_create, 动态栈)
 * @param[in] name 名
 * @param[in] stack_size 栈 (字节)
 * @param[in] priority 优先级 (数字越小越优先)
 * @param[in] entry 入口
 * @param[in] param 参数
 * @param[in] core_id 核 (mini-os 不支持多核, 忽略)
 * @return OSAL_OK 或 NOMEM
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id)
{
    MINI_UNUSED_PARAM(core_id);
    if (!entry)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    osal_mini_os_kernel_boot();

    mini_os_thread_t* handle =
        mini_os_thread_create(name, osal_clamp_stack_size(stack_size), (mini_os_uint8_t)osal_clamp_task_priority(priority), entry, param);
    return (handle != MINI_OS_NULL) ? OSAL_OK : OSAL_ERR_NOMEM;
}

/**
 * @brief 创建任务并回传句柄
 * @param[in] name 名
 * @param[in] stack_size 栈
 * @param[in] priority 优先级
 * @param[in] entry 入口
 * @param[in] param 参数
 * @param[in] core_id 核 (忽略)
 * @param[out] out_handle 输出
 * @return OSAL_OK 或 NOMEM
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle)
        return OSAL_ERR_INVAL;
    MINI_UNUSED_PARAM(core_id);
    *out_handle = NULL;
    if (!entry)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    osal_mini_os_kernel_boot();

    mini_os_thread_t* handle =
        mini_os_thread_create(name, osal_clamp_stack_size(stack_size), (mini_os_uint8_t)osal_clamp_task_priority(priority), entry, param);
    if (handle == MINI_OS_NULL)
        return OSAL_ERR_NOMEM;
    *out_handle = (osal_task_handle_t)handle;
    return OSAL_OK;
}

/**
 * @brief 启动 mini-os 调度器
 * @details 惰性引导后转发 mini_os_schedule_start(): PendSV 置最低优先级、
 *          SysTick 次低, PSP 切换并强制首次上下文切换, 之后控制权交给内核,
 *          正常情况下不返回. 应在所有 osal_task_create() 之后调用一次.
 */
void osal_scheduler_start(void)
{
    osal_mini_os_kernel_boot();
    MINI_IGNORE_RESULT(mini_os_schedule_start());
}

/**
 * @brief 终止当前线程 (mini_os_thread_exit, 不返回, 由 idle 回收 TCB)
 */
void osal_task_self_delete(void) { mini_os_thread_exit(MINI_OS_NULL); }

/**
 * @brief 删除指定线程 (强杀路径: 持有的互斥锁由内核强制释放)
 * @param[in] task 句柄
 */
void osal_task_delete(osal_task_handle_t task)
{
    if (!task)
        return;
    MINI_IGNORE_RESULT(mini_os_thread_delete((mini_os_thread_t*)task));
}

/**
 * @brief 查询任务是否仍在运行 (非 TERMINATED/INVALID 即视为运行)
 * @param[in] task 句柄
 * @return true 运行
 */
bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task)
        return false;

    mini_os_thread_state_t state;
    if (mini_os_thread_get_state((mini_os_thread_t*)task, &state) != MINI_OS_OK)
        return false;
    return state != MINI_OS_THREAD_STATE_TERMINATED && state != MINI_OS_THREAD_STATE_INVALID;
}

/**
 * @brief 获取任务名 (mini_os_thread 内嵌定长名字数组, 直接只读访问)
 * @param[in] task 句柄
 * @return 任务名
 */
const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task)
        return "?";
    return ((mini_os_thread_t*)task)->thread_name;
}

/**
 * @brief 栈水位查询
 * @param[out] task 句柄
 * @return 0 (mini-os 无逐线程栈高水位 API, 仅 MSP 哨兵检测)
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return 0U;
}

/* -------------------------------------------------------------------------- */
/* 队列 (mini_os_queue_create: 描述符与消息池从内核堆分配) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 创建定长消息队列
 * @param[in] queue_len 队列容量 (条目数, mini-os 内核上限 255)
 * @param[in] item_size 单条目字节数
 * @return 队列句柄; 创建失败返回 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    if (queue_len == 0 || item_size == 0)
        return NULL;
    if (queue_len > (size_t)UINT8_MAX)
        return NULL; /* mini_os_queue.max_depth 为 uint8_t */
    if (item_size > (size_t)UINT16_MAX)
        return NULL; /* mini_os_queue.msg_size 为 uint16_t */

    return (osal_queue_handle_t)mini_os_queue_create(MINI_OS_NULL, (mini_os_uint16_t)item_size, (mini_os_uint8_t)queue_len);
}

/**
 * @brief 删除消息队列 (堆队列, 归还内核堆)
 * @param[in] queue 队列句柄
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
    if (!queue)
        return;
    MINI_IGNORE_RESULT(mini_os_queue_delete((mini_os_queue_t*)queue));
}

/**
 * @brief 入队 (task 上下文, 满/超时返回 false)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送条目
 * @param[in] timeout_ms 超时毫秒数
 * @return 成功返回 true
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    return mini_os_queue_send((mini_os_queue_t*)queue, item, osal_mini_os_timeout(timeout_ms)) == MINI_OS_OK;
}

/**
 * @brief 入队 (ISR 上下文, 非阻塞, 不内部 yield)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送条目
 * @param[out] px_yield_required 是否需要上下文切换
 * @return 成功返回 true
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    bool sent = mini_os_queue_send_isr((mini_os_queue_t*)queue, item) == MINI_OS_OK;
    if (sent && px_yield_required != NULL)
        *px_yield_required = true;
    return sent;
}

/**
 * @brief 出队 (task 上下文, 空/超时返回 false)
 * @param[in] queue 队列句柄
 * @param[out] item 回传接收条目
 * @param[in] timeout_ms 超时毫秒数
 * @return 成功返回 true
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    return mini_os_queue_receive((mini_os_queue_t*)queue, item, osal_mini_os_timeout(timeout_ms)) == MINI_OS_OK;
}

/**
 * @brief 出队 (ISR 上下文, 非阻塞, 不内部 yield)
 * @param[in] queue 队列句柄
 * @param[out] item 接收缓冲区
 * @param[out] px_yield_required 是否需要上下文切换
 * @return 成功返回 true
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    bool received = mini_os_queue_receive_isr((mini_os_queue_t*)queue, item) == MINI_OS_OK;
    if (received && px_yield_required != NULL)
        *px_yield_required = true;
    return received;
}

/* -------------------------------------------------------------------------- */
/* 事件组 (静态内嵌 mini_os_event_group_t; CONFIG_OSAL_EVENT 门控) */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_OSAL_EVENT
/* 开启 CONFIG_OSAL_EVENT 时 Kconfig 会 select MINI_OS_EVENT; 两者不一致
 * (如脱离 mini_tree 单独构建 mini-os 时外部只传了 -DCONFIG_OSAL_EVENT)
 * 会让 event.h 整体被 #if MINI_OS_EVENT 跳过, 下面全部符号缺失,
 * 不如在这里直接 fail-fast。 */
#if !MINI_OS_EVENT
#error "osal_mini_os: CONFIG_OSAL_EVENT requires CONFIG_MINI_OS_EVENT"
#endif

/**
 * @brief OSAL 事件组对象
 * @details 内核描述符直接内嵌 (不走 mini_os_malloc), 与互斥锁/二值信号量的
 *          静态内嵌策略一致: 池分配与调用方静态存储两条路径共用同一个
 *          mini_os_event_group_create_static(), 全生命周期零堆分配。
 */
struct osal_event
{
    mini_os_event_group_t obj;       /**< mini-os 内核事件组描述符 (静态内嵌) */
    osal_event_mode_t     mode;      /**< 创建期固定的 AND/OR 模式 (仅诊断可读) */
    bool                  from_pool; /**< true = 来自 s_event_pool, destroy 时归还池槽 */
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
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_EVENT_POOL) static void osal_event_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_event_pool_ctrl, s_event_used, OSAL_EVENT_POOL_SIZE));
}

/**
 * @brief OSAL 等待模式转 mini-os 事件组类型
 * @param[in] mode OSAL_EVENT_OR / OSAL_EVENT_AND
 * @return MINI_OS_EVENT_OR_TYPE / MINI_OS_EVENT_WHOLE_TYPE
 * @details mini-os 把 AND/OR 做成了创建期属性 (存在描述符的 event_type),
 *          而 FreeRTOS/RT-Thread 是等待期参数; 本层按创建期固定,
 *          因此这里一次映射即可。
 */
MINI_STATIC_INLINE mini_os_event_type_t osal_mini_os_event_type(osal_event_mode_t mode)
{
    return (mode == OSAL_EVENT_AND) ? MINI_OS_EVENT_WHOLE_TYPE : MINI_OS_EVENT_OR_TYPE;
}

/**
 * @brief 校验标志掩码 (非 0 且不越出 OSAL 可用位区)
 * @param[in] bits 待校验掩码
 * @return true 合法
 * @details mini-os 本身支持全 32 位标志, 但抽象层按四后端最小公约数
 *          (FreeRTOS 只剩 bit0..23) 收口, 保证业务代码换后端不改行为;
 *          见 osal.h 的 OSAL_EVENT_BITS / OSAL_EVENT_MASK。
 */
MINI_STATIC_INLINE bool osal_event_bits_valid(uint32_t bits) { return (bits != 0U) && ((bits & ~OSAL_EVENT_MASK) == 0U); }

/**
 * @brief 在调用方存储上初始化事件组
 * @param[in] ev 事件组对象
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后是否自动消费已满足的位
 * @return OSAL_OK; ev 为空或 mode 非法返回 OSAL_ERR_INVAL
 */
static int osal_event_init(struct osal_event* ev, osal_event_mode_t mode, bool auto_clear)
{
    if (!ev)
        return OSAL_ERR_INVAL;
    if (mode != OSAL_EVENT_OR && mode != OSAL_EVENT_AND)
        return OSAL_ERR_INVAL;

    /**< 初始标志 0 (create_static 用 event_id 直接覆盖描述符的 event 字段) */
    if (mini_os_event_group_create_static(&ev->obj, 0u, osal_mini_os_event_type(mode)) == MINI_OS_NULL)
        return OSAL_ERR_NOMEM;

    /**< 内核两个 create 都默认 auto-clear=TRUE, 要保留标志必须显式关掉 */
    MINI_IGNORE_RESULT(mini_os_event_group_set_auto_clear(&ev->obj, auto_clear ? MINI_OS_TRUE : MINI_OS_FALSE));

    ev->mode = mode;
    return OSAL_OK;
}

/**
 * @brief 把 bits 并入事件组标志 (屏蔽内核 set 的 OR 语义差异)
 * @param[in] obj 内核事件组描述符
 * @param[in] bits 要置位的掩码 (> 0)
 * @param[in] from_isr true = 走不 yield 的 _isr 变体
 * @return mini-os 错误码
 * @details mini-os 的 WHOLE (即 AND) 类型在 set 时是"整体替换标志"
 *          (event.c: mini_os_event_apply), 而 OSAL 契约与 FreeRTOS /
 *          RT-Thread 一致是"按位 OR 并入"; 直接转发会让 AND 组在
 *          第二次置位时丢掉第一次的位, 等待永不满足。
 *          故在关中断临界区内先读当前标志再写回 cur | bits:
 *          WHOLE 的替换语义刚好实现 OR 并入 (OR 类型时 cur|bits 与
 *          内核自己的 |= 等价), 且读-改-写对外原子, 并发置位不互相覆盖。
 * @note mini_os_irq_save 可嵌套, 内核 set 自带的临界区不受影响;
 *       被唤醒的线程要等到最外层恢复中断后才真正切换。
 */
static mini_os_err_t osal_mini_os_event_or_bits(mini_os_event_group_t* obj, uint32_t bits, bool from_isr)
{
    mini_os_irq_t    irq = mini_os_irq_save();
    mini_os_uint32_t cur = 0u;
    mini_os_err_t    err;

    MINI_IGNORE_RESULT(mini_os_event_get_group(obj, &cur));
    if (from_isr)
        err = mini_os_event_set_group_isr(obj, (mini_os_uint32_t)(cur | (mini_os_uint32_t)bits));
    else
        err = mini_os_event_set_group(obj, (mini_os_uint32_t)(cur | (mini_os_uint32_t)bits));
    mini_os_irq_restore(irq);
    return err;
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
 * @param[in] ev 事件组 (可为 NULL, 忽略)
 * @details 仍有线程挂在 wait_list 上时不归还池槽: 描述符一旦被下一个
 *          osal_event_create() 复用, 那些线程就会阻塞在一个标志与模式
 *          都已变的对象上 (内核 mini_os_event_group_delete 同样以 BUSY
 *          拒绝这种删除); 本层对象是静态内嵌的, 没有堆需要释放,
 *          因此"不归还"就是完整的安全行为。
 * @note 静态存储创建的事件组本来就不占池槽, 落到池区间判定为假, 自然跳过。
 */
void osal_event_destroy(struct osal_event* ev)
{
    if (!ev || osal_in_isr())
        return;

    mini_os_irq_t irq = mini_os_irq_save();
    bool          busy = (mini_os_list_is_empty(&ev->obj.wait_list) == MINI_OS_FALSE);
    mini_os_irq_restore(irq);
    if (busy)
        return;

    if (ev->from_pool && ev >= s_event_pool && ev < &s_event_pool[OSAL_EVENT_POOL_SIZE])
    {
        size_t idx = (size_t)(ev - s_event_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_event_pool_ctrl, (int)idx));
    }
}

/**
 * @brief 置位事件标志 (task 上下文)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0)
 * @return OSAL_OK 或错误码
 * @details OR 入现有标志 (见 osal_mini_os_event_or_bits), 唤醒所有已满足
 *          的等待者; 被唤醒者优先级更高时内核会主动让出。
 */
int osal_event_set(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    return osal_err_from_mini_os(osal_mini_os_event_or_bits(&ev->obj, bits, false));
}

/**
 * @brief 置位事件标志 (ISR 上下文, 不内部 yield)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0)
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return OSAL_OK 或错误码
 * @details mini-os 的 set_group_isr 不上报是否唤醒了线程: 成功即置 yield
 *          标志, 是否真切换由 ISR 出口的 mini_os_schedule_yield_isr()
 *          自判 (仅当出现更高优先级就绪线程才置 PendSV),
 *          与队列/信号量的 _from_isr 完全同构。
 */
int osal_event_set_from_isr(struct osal_event* ev, uint32_t bits, bool* px_yield_required)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    mini_os_err_t err = osal_mini_os_event_or_bits(&ev->obj, bits, true);
    if (err == MINI_OS_OK && px_yield_required != NULL)
        *px_yield_required = true;
    return osal_err_from_mini_os(err);
}

/**
 * @brief 清除事件标志
 * @param[in] ev 事件组
 * @param[in] bits 要清除的掩码 (> 0)
 * @return OSAL_OK 或错误码
 * @details 内核实现为 event &= ~bits, 自带关中断临界区且不唤醒不 yield,
 *          因此 task 与 ISR 均可调用 (两种事件组类型下语义一致,
 *          不存在 set 那样的 WHOLE 替换问题)。
 */
int osal_event_clear(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    return osal_err_from_mini_os(mini_os_event_clear_group(&ev->obj, (mini_os_uint32_t)bits));
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

    mini_os_uint32_t cur = 0u;
    mini_os_err_t    err = mini_os_event_get_group(&ev->obj, &cur);
    if (err != MINI_OS_OK)
        return osal_err_from_mini_os(err);

    *out_bits = (uint32_t)cur;
    return OSAL_OK;
}

/**
 * @brief 等待事件标志
 * @param[in] ev 事件组
 * @param[in] bits 等待的掩码 (> 0)
 * @param[in] timeout_ms 超时毫秒 (0 = 不阻塞, OSAL_WAIT_FOREVER = 永久)
 * @param[out] out_bits 回传实际已置位的相关位 (可为 NULL)
 * @return 满足 OSAL_OK; 未满足/超时 OSAL_ERR_TIMEOUT; 参数非法 OSAL_ERR_INVAL
 * @details 是否满足由创建期 mode 决定 (内核 mini_os_event_satisfied);
 *          auto_clear=true 时内核在返回前消费已满足的位。
 *          内核非阻塞未满足报 AGAIN、有限超时到期报 TIMEOUT,
 *          经 osal_err_from_mini_os 统一成 OSAL_ERR_TIMEOUT。
 */
int osal_event_wait(struct osal_event* ev, uint32_t bits, uint32_t timeout_ms, uint32_t* out_bits)
{
    if (!ev || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    mini_os_uint32_t got = 0u;
    mini_os_err_t    err =
        mini_os_event_wait(&ev->obj, (mini_os_uint32_t)bits, osal_mini_os_timeout(timeout_ms), (out_bits != NULL) ? &got : MINI_OS_NULL);
    if (err == MINI_OS_OK && out_bits != NULL)
        *out_bits = (uint32_t)got;
    return osal_err_from_mini_os(err);
}
#endif /* CONFIG_OSAL_EVENT */

/* -------------------------------------------------------------------------- */
/* 调度器冻结 / 中断冻结 (单向不可恢复) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 冻结调度器
 * @details mini-os 无 suspend-all API, 退化为关全局中断 (单向不可恢复,
 *          与 osal_null 后端一致的安全死锁语义).
 */
void osal_sched_freeze(void) { mini_os_irq_disable(); }

/**
 * @brief 冻结全局中断 (单向不可恢复)
 */
void osal_int_freeze(void) { mini_os_irq_disable(); }

/* -------------------------------------------------------------------------- */
/* 硬件安全关断 & 日志 (对齐其他后端模板) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 弱符号硬件安全关断 (板级未覆盖时触发 trap)
 */
MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }

/**
 * @brief 弱符号 Panic 安全互锁 (板级可覆盖: 喂狗、切断执行器等)
 */
MINI_WEAK void osal_panic_interlock(void) { /* 板级可覆盖: 喂硬件看门狗, 切断执行器供电, 等待复位 */ }

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
    my_printf_output("\r\n[CRITICAL_ASSERT FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_MINI_OS */
