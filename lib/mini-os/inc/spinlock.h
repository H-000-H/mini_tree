/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file spinlock.h
 * @brief 自旋锁（header-only，两种模式由 MINI_OS_SPINLOCK_ATOMIC 二选一）
 *
 * 默认关闭（MINI_OS_SPINLOCK = 0）：未开启时本头文件不定义任何实体；
 * 通过 CONFIG_MINI_OS_SPINLOCK=1（config/Kconfig 注入）或外部预定义开启。
 *
 * 原子模式（MINI_OS_SPINLOCK_ATOMIC = 1）：TAS 自旋，面向多核/跨核场景。
 * 不可重入，递归加锁会自锁；持锁时间必须极短。重试路径默认纯延迟 +
 * schedule_yield；CONFIG_MINI_OS_SPINLOCK_YIELD=1 才开启 for-yield 轮询循环。
 * 单核模式（默认）：关中断临界区 + 嵌套计数，等效于可重入的临界区锁。
 */
#ifndef SPINLOCK_H
#define SPINLOCK_H
#include "mini_config.h"

#if MINI_OS_SPINLOCK /* 默认 0：CONFIG_MINI_OS_SPINLOCK=1（或外部预定义）才编译本模块 */
#ifdef __cplusplus
extern "C"
{
#endif
#include "err.h"
#include "port.h"
#include "redef.h"
#include "schedule.h"

typedef struct mini_os_spinlock mini_os_spinlock_t;

struct mini_os_spinlock
{
#if MINI_OS_SPINLOCK_ATOMIC
    mini_os_atomic_int8_t locked; /**< 0 = unlocked, 1 = locked（原子模式） */
#else
    mini_os_irq_t   irq;  /**< 最外层加锁时保存的中断状态（单核模式） */
    mini_os_uint8_t nest; /**< 嵌套深度（单核模式） */
#endif /* MINI_OS_SPINLOCK_ATOMIC */
};

/**
 * @brief 初始化自旋锁
 * @param[in] spinlock 待初始化的自旋锁
 * @return MINI_OS_OK 成功；MINI_OS_ERR_INVAL 参数为空
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_init(mini_os_spinlock_t* spinlock)
{
    if (spinlock == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    MINI_OS_ATOMIC_STORE(&spinlock->locked, 0, MINI_OS_RELAXED);
#else
    spinlock->irq = 0;
    spinlock->nest = 0;
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}

/**
 * @brief 加锁
 * @param[in] spinlock 目标自旋锁
 * @return MINI_OS_OK 成功（原子模式：可能自旋/让出后获得）
 * @note 原子模式 TAS 约定：返回 TRUE 表示锁已被占用，拿到锁（返回 FALSE）
 *       才退出循环；单核模式可重入（嵌套计数），最外层保存中断恢复点
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_lock(mini_os_spinlock_t* spinlock)
{
    if (spinlock == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    while (MINI_OS_ATOMIC_TEST_AND_SET(&spinlock->locked, MINI_OS_ACQUIRE))
    {
#if MINI_OS_SPINLOCK_YIELD /* 默认 0：不开 for-yield，纯延迟 + schedule_yield 足够 */
        mini_os_uint32_t i;

        for (i = 0; i < MINI_OS_SPINLOCK_NUM; i++)
        {
            if (MINI_OS_ATOMIC_LOAD(&spinlock->locked, MINI_OS_RELAXED) == 0)
                break;       /* 看起来已释放：回到 TAS 重试 */
            mini_os_pause(); /* 自旋等待提示（port.S: yield 指令） */
        }
#else
        mini_os_pause(); /* 纯延迟（port.S: yield 指令），不做轮询 */
#endif                                  /* MINI_OS_SPINLOCK_YIELD */
        (void)mini_os_schedule_yield(); /* 让出 CPU，避免饿死持锁者 */
    }
#else
    {
        mini_os_irq_t irq = mini_os_irq_save();

        if (spinlock->nest == 0u)
            spinlock->irq = irq; /* 只有最外层需要记住恢复点 */
        spinlock->nest++;
    }
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}

/**
 * @brief 解锁
 * @param[in] spinlock 目标自旋锁
 * @return MINI_OS_OK 成功；MINI_OS_ERR_INVAL 参数为空或未加锁（单核模式）
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_unlock(mini_os_spinlock_t* spinlock)
{
    if (spinlock == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    MINI_OS_ATOMIC_STORE(&spinlock->locked, 0, MINI_OS_RELEASE);
#else
    if (spinlock->nest == 0u)
        return MINI_OS_ERR_INVAL; /* 未加锁却解锁 */
    spinlock->nest--;
    if (spinlock->nest == 0u)
        mini_os_irq_restore(spinlock->irq);
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}

/**
 * @brief 查询自旋锁是否被持有
 * @param[in] spinlock 目标自旋锁
 * @param[out] locked 接收查询结果：MINI_OS_TRUE = 已被持有
 * @return MINI_OS_OK 成功；MINI_OS_ERR_INVAL 参数为空
 */
MINI_OS_STATIC_INLINE mini_os_err_t mini_os_spinlock_islocked(mini_os_spinlock_t* spinlock, mini_os_bool_t* locked)
{
    if (spinlock == MINI_OS_NULL || locked == MINI_OS_NULL)
        return MINI_OS_ERR_INVAL;
#if MINI_OS_SPINLOCK_ATOMIC
    *locked = (MINI_OS_ATOMIC_LOAD(&spinlock->locked, MINI_OS_RELAXED) != 0) ? MINI_OS_TRUE : MINI_OS_FALSE;
#else
    *locked = (spinlock->nest > 0u) ? MINI_OS_TRUE : MINI_OS_FALSE;
#endif /* MINI_OS_SPINLOCK_ATOMIC */
    return MINI_OS_OK;
}
#ifdef __cplusplus
}
#endif
#endif /* MINI_OS_SPINLOCK */
#endif /* SPINLOCK_H */
