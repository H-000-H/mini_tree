/* SPDX-License-Identifier: Apache-2.0 */
/*
 * dev_lifecycle.c — 设备 I/O 生命周期状态机实现
 *
 * open/close/io_begin 持 io_lock 进入并校验 LIVE 状态, REMOVING 状态返回 NODEV.
 * remove_drain 轮询等待 opens==0 且 io_active==0, 成功返回时持有 io_lock.
 * remove_finish 释放 io_lock 并重置状态, 不 destroy mutex (由驱动自行销毁).
 */
#include "dev_lifecycle.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat_poison.h"

// 辅助宏：用于简化入参校验
#define VALIDATE_LC(lc) if (IS_ERR(lc) || !(lc)) return VFS_ERR_INVAL
#define VALIDATE_LC_VOID(lc) if (IS_ERR(lc) || !(lc)) return

static int dev_lc_lock_live(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    // 注意：lc->state 如果可能在中断或其他核改变，其结构体定义中 state 最好加 volatile
    if (IS_ERR(lc) || !lc || !lc->io_lock)
        return VFS_ERR_INVAL;

    if (lc->state != DEV_LC_LIVE)
        return VFS_ERR_NODEV;

    if (osal_mutex_lock(lc->io_lock, timeout_ms) != 0)
        return VFS_ERR_TIMEOUT;

    // 二次检查
    if (lc->state != DEV_LC_LIVE) 
    {
        (void)osal_mutex_unlock(lc->io_lock);
        return VFS_ERR_NODEV;
    }

    return VFS_OK;
}

void dev_lc_init(struct dev_lifecycle* lc, struct osal_mutex* io_lock)
{
    VALIDATE_LC_VOID(lc);

    lc->io_lock   = io_lock;
    lc->opens     = 0;
    lc->io_active = 0;
    lc->state     = DEV_LC_LIVE;
}

void dev_lc_reset(struct dev_lifecycle* lc)
{
    VALIDATE_LC_VOID(lc);

    lc->io_lock   = NULL;
    lc->opens     = 0;
    lc->io_active = 0;
    lc->state     = DEV_LC_UNINITIALIZED;
}

dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc)
{
    if (IS_ERR(lc) || !lc)
        return DEV_LC_UNINITIALIZED;
    return lc->state;
}

int dev_lc_opens(const struct dev_lifecycle* lc)
{
    if (IS_ERR(lc) || !lc)
        return 0;
    return lc->opens;
}

int dev_lc_io_active_count(const struct dev_lifecycle* lc)
{
    if (IS_ERR(lc) || !lc)
        return 0;
    return lc->io_active;
}

int dev_lc_open_begin(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    int ret = dev_lc_lock_live(lc, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    lc->opens++;
    // 保持原逻辑：如果是第一个打开的，返回1，否则返回0
    return (lc->opens == 1) ? 1 : 0;
}

void dev_lc_open_end(struct dev_lifecycle* lc)
{
    if (IS_ERR(lc) || !lc || !lc->io_lock)
        return;
    (void)osal_mutex_unlock(lc->io_lock);
}

void dev_lc_open_abort(struct dev_lifecycle* lc)
{
    VALIDATE_LC_VOID(lc);

    // 整个 abort 在获取锁的状态下进行，这里假设外部调用 abort 时已经持有锁或者有上层同步
    if (lc->opens > 0)
        lc->opens--;

    dev_lc_open_end(lc);
}

int dev_lc_close_begin(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    int ret = dev_lc_lock_live(lc, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    if (lc->opens <= 0)
    {
        (void)osal_mutex_unlock(lc->io_lock);
        return VFS_ERR_IO; 
    }

    lc->opens--;
    return (lc->opens == 0) ? 1 : 0;
}

void dev_lc_close_end(struct dev_lifecycle* lc)
{
    dev_lc_open_end(lc); // 复用统一的释放锁逻辑
}

int dev_lc_io_begin(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    int ret = dev_lc_lock_live(lc, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    lc->io_active++;
    return VFS_OK;
}

void dev_lc_io_end(struct dev_lifecycle* lc)
{
    if (IS_ERR(lc) || !lc || !lc->io_lock)
        return;

    if (lc->io_active > 0)
        lc->io_active--;

    (void)osal_mutex_unlock(lc->io_lock);
}

void dev_lc_remove_start(struct dev_lifecycle* lc)
{
    VALIDATE_LC_VOID(lc);
    lc->state = DEV_LC_REMOVING; // 设置为正在卸载状态，从此开始，新的 I/O 和 Open 将无法进入
}

int dev_lc_remove_drain(struct dev_lifecycle* lc, uint32_t timeout_ms)
{
    if (IS_ERR(lc) || !lc || !lc->io_lock)
        return VFS_ERR_INVAL;

    if (lc->state != DEV_LC_REMOVING)
        return VFS_ERR_IO;

    const uint32_t start_ms = osal_time_ms();
    uint32_t remaining_ms = timeout_ms;

    for (;;)
    {
        // 优化：动态计算剩余超时时间，避免整体超时时间无线拉长
        if (timeout_ms != OSAL_WAIT_FOREVER)
        {
            uint32_t elapsed = osal_time_ms() - start_ms;
            if (elapsed >= timeout_ms)
                return VFS_ERR_TIMEOUT;
            remaining_ms = timeout_ms - elapsed;
        }

        if (osal_mutex_lock(lc->io_lock, remaining_ms) != 0)
            return VFS_ERR_TIMEOUT;

        // 检查引用计数是否排空
        if (lc->opens == 0 && lc->io_active == 0)
        {
            //排空成功，必须依然持有锁，返回给 finish 去释放并重置
            return VFS_OK;
        }

        // 还有未完成的引用，解锁，让出 CPU 稍后重试
        (void)osal_mutex_unlock(lc->io_lock);

        osal_delay_ms(1);
    }
}

void dev_lc_remove_finish(struct dev_lifecycle* lc)
{
    VALIDATE_LC_VOID(lc);

    // 因为 drain 成功时持有锁，这里释放并彻底销毁
    if (lc->io_lock)
        (void)osal_mutex_unlock(lc->io_lock);

    dev_lc_reset(lc);
}