/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file dev_lifecycle.c
 *@brief dev lifecycle 实现
 *@author H-000-H
 *@details
 *   dev_lifecycle.c — 设备 I/O 生命周期状态机 (CAS 哨兵版, 无锁)
 *   opens / io_active 使用 -1 哨兵表示 "teardown 已锁定".
 *   remove_drain: CAS opens 0→-1, 成功后保持锁定并 CAS io_active 0→-1,
 *   两阶段全部成功才返回. 第一阶段失败 (opens 临时 +1) 重试等待稳定归零;
 *   第二阶段失败不触碰 opens (opens 保持 -1 锁定), 仅继续重试 io_active.
 *   退避由 timeout_ms + 1 ms 周期驱动.
 *   open_begin / io_begin: CAS 循环递增, 遇 -1 直接拒绝.
 *   - 状态机门控: remove_drain 进入前提 state == REMOVING; 期间 open_begin /
 *   io_begin 均检查 state == LIVE (见 open_begin/io_begin 的 state 门控),
 *   因此 REMOVING 下不会有新的 open/io 计数递增.
 *   - 单调锁定: opens 一旦经 CAS 0→-1 成功即保持 -1, 绝不回滚到 0 (避免把
 *   opens 短暂暴露为 0 的窗口); io_active 在 opens 锁定后反复 CAS 0→-1 直到
 *   归零. 期间 open/io 见 -1 或 state != LIVE 立即拒绝.
 *   - 内存序: load 用 ACQUIRE 看见 state/计数; CAS 用 ACQ_REL 与失败路径的
 *   RELAXED 配对; 写终态用 RELEASE 配 ACQUIRE.
 *   - 无 ABA: 单调递增/递减 + -1
 */

#include "dev_lifecycle.h"

#include "osal.h"

#include "compiler_compat_poison.h"

#define DEV_LC_LOCKED (-1)

/**
 * @brief 初始化生命周期状态机为 LIVE
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_init(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    MINI_ATOMIC_STORE(&lc->opens, 0, MINI_RELEASE);
    MINI_ATOMIC_STORE(&lc->io_active, 0, MINI_RELEASE);
    MINI_ATOMIC_STORE(&lc->state, DEV_LC_LIVE, MINI_RELEASE);
}

/**
 * @brief 重置生命周期状态机为 UNINITIALIZED
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_reset(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    MINI_ATOMIC_STORE(&lc->opens, 0, MINI_RELEASE);
    MINI_ATOMIC_STORE(&lc->io_active, 0, MINI_RELEASE);
    MINI_ATOMIC_STORE(&lc->state, DEV_LC_UNINITIALIZED, MINI_RELEASE);
}

/**
 * @brief 读取生命周期状态
 * @param[in] lc 生命周期对象指针
 * @return 当前状态, lc 为 NULL 返回 DEV_LC_UNINITIALIZED
 */
dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc)
{
    return lc ? (dev_lc_state_t)MINI_ATOMIC_LOAD(&lc->state, MINI_ACQUIRE) :
                DEV_LC_UNINITIALIZED;
}

/**
 * @brief 读取当前 open 引用计数 (teardown 锁定后返回 0)
 * @param[in] lc 生命周期对象指针
 * @return open 计数
 */
int dev_lc_opens(const struct dev_lifecycle* lc)
{
    int value = lc ? MINI_ATOMIC_LOAD(&lc->opens, MINI_ACQUIRE) : 0;
    return value < 0 ? 0 : value;
}

/**
 * @brief 读取当前活跃 I/O 计数 (teardown 锁定后返回 0)
 * @param[in] lc 生命周期对象指针
 * @return io_active 计数
 */
int dev_lc_io_active_count(const struct dev_lifecycle* lc)
{
    int value = lc ? MINI_ATOMIC_LOAD(&lc->io_active, MINI_ACQUIRE) : 0;
    return value < 0 ? 0 : value;
}

/**
 * @brief 开始 open (CAS 递增 opens, teardown 或非 LIVE 拒绝)
 * @param[in] lc 生命周期对象指针
 * @return 首次 open 返回 1, 重复 open 返回 0, 失败返回负数错误码
 */
int dev_lc_open_begin(struct dev_lifecycle* lc)
{
    if (!lc)
        return MINI_ERR_INVAL;

    int old;
    do
    {
        old = MINI_ATOMIC_LOAD(&lc->opens, MINI_RELAXED);
        if (old < 0)
            return MINI_ERR_NODEV;
        if (MINI_ATOMIC_LOAD(&lc->state, MINI_ACQUIRE) != DEV_LC_LIVE)
            return MINI_ERR_NODEV;
    } while (!MINI_ATOMIC_CAS(&lc->opens, &old, old + 1, MINI_ACQ_REL, MINI_RELAXED));

    return (old + 1 == 1) ? 1 : 0;
}

/**
 * @brief open 完成占位 (当前无额外逻辑)
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_open_end(struct dev_lifecycle* lc) {MINI_UNUSED_PARAM(lc); }

/**
 * @brief 中止 open (opens -1, 用于 open 中途失败回滚)
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_open_abort(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    MINI_ATOMIC_SUB_FETCH(&lc->opens, 1, MINI_RELEASE);
}

/**
 * @brief 开始 close (CAS 递减 opens)
 * @param[in] lc 生命周期对象指针
 * @return 末次 close 返回 1, 非末次返回 0, opens<=0 返回 MINI_ERR_IO
 */
int dev_lc_close_begin(struct dev_lifecycle* lc)
{
    if (!lc)
        return MINI_ERR_INVAL;

    int old;
    do
    {
        old = MINI_ATOMIC_LOAD(&lc->opens, MINI_RELAXED);
        if (old <= 0)
            return MINI_ERR_IO;
    } while (!MINI_ATOMIC_CAS(&lc->opens, &old, old - 1, MINI_ACQ_REL, MINI_RELAXED));

    return (old - 1 == 0) ? 1 : 0;
}

/**
 * @brief close 完成占位 (当前无额外逻辑)
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_close_end(struct dev_lifecycle* lc) {MINI_UNUSED_PARAM(lc); }

/**
 * @brief 开始 I/O (CAS 递增 io_active, teardown 或非 LIVE 拒绝)
 * @param[in] lc 生命周期对象指针
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
int dev_lc_io_begin(struct dev_lifecycle* lc)
{
    if (!lc)
        return MINI_ERR_INVAL;

    int old;
    do
    {
        old = MINI_ATOMIC_LOAD(&lc->io_active, MINI_RELAXED);
        if (old < 0)
            return MINI_ERR_NODEV;
        if (MINI_ATOMIC_LOAD(&lc->state, MINI_ACQUIRE) != DEV_LC_LIVE)
            return MINI_ERR_NODEV;
    } while (
        !MINI_ATOMIC_CAS(&lc->io_active, &old, old + 1, MINI_ACQ_REL, MINI_RELAXED));

    return MINI_OK;
}

/**
 * @brief 结束 I/O (io_active -1)
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_io_end(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    MINI_ATOMIC_SUB_FETCH(&lc->io_active, 1, MINI_RELEASE);
}

/**
 * @brief 标记设备进入 REMOVING 状态
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_remove_start(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    MINI_ATOMIC_STORE(&lc->state, DEV_LC_REMOVING, MINI_RELEASE);
}

/**
 * @brief 排空 open/io 并 CAS 锁定 (teardown drain)
 * @param[in] lc 生命周期对象指针
 * @param[in] timeout_ms 超时 (毫秒, OSAL_WAIT_FOREVER 表示永久等待)
 * @return 成功返回 MINI_OK, 超时返回 MINI_ERR_TIMEOUT, 状态非法返回 MINI_ERR_BUSY
 */
int dev_lc_remove_drain(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    if (!lc)
        return MINI_ERR_INVAL;

    if (MINI_ATOMIC_LOAD(&lc->state, MINI_ACQUIRE) != DEV_LC_REMOVING)
        return MINI_ERR_BUSY;

    const uint32_t start_ms = osal_time_ms();
    for (;;)
    {
        int opens_expected = 0;
        if (MINI_ATOMIC_CAS(&lc->opens, &opens_expected, DEV_LC_LOCKED, MINI_ACQ_REL,
                              MINI_RELAXED))
        {
            /* opens 已锁定为 -1. 在 state == REMOVING 门控下 open_begin 必拒绝,
             * 因此 opens 一旦锁定即保持锁定, 不回滚到 0 (避免短暂暴露窗口). */
            for (;;)
            {
                int io_expected = 0;
                if (MINI_ATOMIC_CAS(&lc->io_active, &io_expected, DEV_LC_LOCKED,
                                      MINI_ACQ_REL, MINI_RELAXED))
                    return MINI_OK;

                if (timeout_ms != OSAL_WAIT_FOREVER && (osal_time_ms() - start_ms) >= timeout_ms)
                    return MINI_ERR_TIMEOUT;

                osal_delay_ms(1);
            }
        }

        if (timeout_ms != OSAL_WAIT_FOREVER)
        {
            if (osal_time_ms() - start_ms >= timeout_ms)
                return MINI_ERR_TIMEOUT;
        }
        osal_delay_ms(1);
    }
}

/**
 * @brief remove 完成并重置生命周期状态机
 * @param[in] lc 生命周期对象指针
 */
void dev_lc_remove_finish(struct dev_lifecycle* lc)
{
    if (!lc)
        return;
    dev_lc_reset(lc);
}
