/* SPDX-License-Identifier: Apache-2.0 */
/*
 * dev_lifecycle.c — 设备 I/O 生命周期状态机 (CAS 哨兵版)
 *
 * opens / io_active 使用 -1 哨兵表示 "teardown 已锁定".
 * remove_drain: CAS opens 0→-1 → CAS io_active 0→-1, 双锁成功才返回.
 * open_begin / io_begin: CAS 循环递增, 遇 -1 直接拒绝.
 * 无互斥锁, 无脉冲窗口.
 */
#include "dev_lifecycle.h"

#include "osal.h"

#include "compiler_compat_poison.h"

#define DEV_LC_LOCKED (-1)

/**
 * @brief 初始化生命周期状态机为 LIVE
 * @param lc 生命周期对象指针
 */
void dev_lc_init(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    COMPAT_ATOMIC_STORE(&lc->opens, 0, COMPAT_MO_RELEASE);
    COMPAT_ATOMIC_STORE(&lc->io_active, 0, COMPAT_MO_RELEASE);
    COMPAT_ATOMIC_STORE(&lc->state, DEV_LC_LIVE, COMPAT_MO_RELEASE);
}

/**
 * @brief 重置生命周期状态机为 UNINITIALIZED
 * @param lc 生命周期对象指针
 */
void dev_lc_reset(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    COMPAT_ATOMIC_STORE(&lc->opens, 0, COMPAT_MO_RELEASE);
    COMPAT_ATOMIC_STORE(&lc->io_active, 0, COMPAT_MO_RELEASE);
    COMPAT_ATOMIC_STORE(&lc->state, DEV_LC_UNINITIALIZED, COMPAT_MO_RELEASE);
}

/**
 * @brief 读取生命周期状态
 * @param lc 生命周期对象指针
 * @return 当前状态, lc 为 NULL 返回 DEV_LC_UNINITIALIZED
 */
dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc)
{
    return lc ? (dev_lc_state_t)COMPAT_ATOMIC_LOAD(&lc->state, COMPAT_MO_ACQUIRE) :
                DEV_LC_UNINITIALIZED;
}

/**
 * @brief 读取当前 open 引用计数 (teardown 锁定后返回 0)
 * @param lc 生命周期对象指针
 * @return open 计数
 */
int dev_lc_opens(const struct dev_lifecycle* lc)
{
    int v = lc ? COMPAT_ATOMIC_LOAD(&lc->opens, COMPAT_MO_ACQUIRE) : 0;
    return v < 0 ? 0 : v;
}

/**
 * @brief 读取当前活跃 I/O 计数 (teardown 锁定后返回 0)
 * @param lc 生命周期对象指针
 * @return io_active 计数
 */
int dev_lc_io_active_count(const struct dev_lifecycle* lc)
{
    int v = lc ? COMPAT_ATOMIC_LOAD(&lc->io_active, COMPAT_MO_ACQUIRE) : 0;
    return v < 0 ? 0 : v;
}

/**
 * @brief 开始 open (CAS 递增 opens, teardown 或非 LIVE 拒绝)
 * @param lc 生命周期对象指针
 * @return 首次 open 返回 1, 重复 open 返回 0, 失败返回负数错误码
 */
int dev_lc_open_begin(struct dev_lifecycle* lc)
{
    if (!lc)
        return VFS_ERR_INVAL;

    int old;
    do
    {
        old = COMPAT_ATOMIC_LOAD(&lc->opens, COMPAT_MO_RELAXED);
        if (old < 0)
            return VFS_ERR_NODEV;
        if (COMPAT_ATOMIC_LOAD(&lc->state, COMPAT_MO_ACQUIRE) != DEV_LC_LIVE)
            return VFS_ERR_NODEV;
    } while (!COMPAT_ATOMIC_CAS(&lc->opens, &old, old + 1, COMPAT_MO_ACQ_REL, COMPAT_MO_RELAXED));

    return (old + 1 == 1) ? 1 : 0;
}

/**
 * @brief open 完成占位 (当前无额外逻辑)
 * @param lc 生命周期对象指针
 */
void dev_lc_open_end(struct dev_lifecycle* lc) { (void)lc; }

/**
 * @brief 中止 open (opens -1, 用于 open 中途失败回滚)
 * @param lc 生命周期对象指针
 */
void dev_lc_open_abort(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    COMPAT_ATOMIC_SUB_FETCH(&lc->opens, 1, COMPAT_MO_RELEASE);
}

/**
 * @brief 开始 close (CAS 递减 opens)
 * @param lc 生命周期对象指针
 * @return 末次 close 返回 1, 非末次返回 0, opens<=0 返回 VFS_ERR_IO
 */
int dev_lc_close_begin(struct dev_lifecycle* lc)
{
    if (!lc)
        return VFS_ERR_INVAL;

    int old;
    do
    {
        old = COMPAT_ATOMIC_LOAD(&lc->opens, COMPAT_MO_RELAXED);
        if (old <= 0)
            return VFS_ERR_IO;
    } while (!COMPAT_ATOMIC_CAS(&lc->opens, &old, old - 1, COMPAT_MO_ACQ_REL, COMPAT_MO_RELAXED));

    return (old - 1 == 0) ? 1 : 0;
}

/**
 * @brief close 完成占位 (当前无额外逻辑)
 * @param lc 生命周期对象指针
 */
void dev_lc_close_end(struct dev_lifecycle* lc) { (void)lc; }

/**
 * @brief 开始 I/O (CAS 递增 io_active, teardown 或非 LIVE 拒绝)
 * @param lc 生命周期对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int dev_lc_io_begin(struct dev_lifecycle* lc)
{
    if (!lc)
        return VFS_ERR_INVAL;

    int old;
    do
    {
        old = COMPAT_ATOMIC_LOAD(&lc->io_active, COMPAT_MO_RELAXED);
        if (old < 0)
            return VFS_ERR_NODEV;
        if (COMPAT_ATOMIC_LOAD(&lc->state, COMPAT_MO_ACQUIRE) != DEV_LC_LIVE)
            return VFS_ERR_NODEV;
    } while (
        !COMPAT_ATOMIC_CAS(&lc->io_active, &old, old + 1, COMPAT_MO_ACQ_REL, COMPAT_MO_RELAXED));

    return VFS_OK;
}

/**
 * @brief 结束 I/O (io_active -1)
 * @param lc 生命周期对象指针
 */
void dev_lc_io_end(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    COMPAT_ATOMIC_SUB_FETCH(&lc->io_active, 1, COMPAT_MO_RELEASE);
}

/**
 * @brief 标记设备进入 REMOVING 状态
 * @param lc 生命周期对象指针
 */
void dev_lc_remove_start(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    COMPAT_ATOMIC_STORE(&lc->state, DEV_LC_REMOVING, COMPAT_MO_RELEASE);
}

/**
 * @brief 排空 open/io 并 CAS 锁定 (teardown drain)
 * @param lc 生命周期对象指针
 * @param timeout_ms 超时 (毫秒, OSAL_WAIT_FOREVER 表示永久等待)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT, 状态非法返回 VFS_ERR_BUSY
 */
int dev_lc_remove_drain(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    if (!lc)
        return VFS_ERR_INVAL;

    if (COMPAT_ATOMIC_LOAD(&lc->state, COMPAT_MO_ACQUIRE) != DEV_LC_REMOVING)
        return VFS_ERR_BUSY;

    const uint32_t start_ms = osal_time_ms();
    for (;;)
    {
        int opens_expected = 0;
        if (COMPAT_ATOMIC_CAS(&lc->opens, &opens_expected, DEV_LC_LOCKED, COMPAT_MO_ACQ_REL,
                              COMPAT_MO_RELAXED))
        {
            int io_expected = 0;
            if (COMPAT_ATOMIC_CAS(&lc->io_active, &io_expected, DEV_LC_LOCKED, COMPAT_MO_ACQ_REL,
                                  COMPAT_MO_RELAXED))
                return VFS_OK;

            COMPAT_ATOMIC_STORE(&lc->opens, 0, COMPAT_MO_RELEASE);
        }

        if (timeout_ms != OSAL_WAIT_FOREVER)
        {
            if (osal_time_ms() - start_ms >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        osal_delay_ms(1);
    }
}

/**
 * @brief remove 完成并重置生命周期状态机
 * @param lc 生命周期对象指针
 */
void dev_lc_remove_finish(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    dev_lc_reset(lc);
}
