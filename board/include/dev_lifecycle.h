/* SPDX-License-Identifier: Apache-2.0 */
/*
 * dev_lifecycle.h — 设备 I/O 生命周期状态机 (CAS 哨兵版)
 *
 * opens / io_active 使用 COMPAT_ATOMIC_INT, -1 哨兵表示 "teardown 已锁定".
 * open_begin / io_begin: CAS 循环递增, 遇 -1 或 state != LIVE 直接拒绝.
 * remove_drain: CAS opens 0→-1 → CAS io_active 0→-1, 双锁成功才返回.
 *   任何 opens / io_active 临时 +1 都让 CAS 失败, drain 重试等待稳定归零.
 * remove_finish: reset 恢复全部为 0 / UNINITIALIZED.
 * 无互斥锁, 无脉冲窗口.
 */
#ifndef DEV_LIFECYCLE_H
#define DEV_LIFECYCLE_H

#include "compiler_compat.h"
#include "status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum dev_lc_state
    {
        DEV_LC_UNINITIALIZED = 0, /**< 未初始化 */
        DEV_LC_LIVE, /**< 运行中 */
        DEV_LC_REMOVING, /**< 正在移除 */
        DEV_LC_DEAD, /**< 已死亡 */
    } dev_lc_state_t;

    struct dev_lifecycle
    {
        COMPAT_ATOMIC_INT opens; /**< 当前打开计数 (-1 = teardown 锁定) */
        COMPAT_ATOMIC_INT io_active; /**< 当前 I/O 活跃计数 (-1 = teardown 锁定) */
        COMPAT_ATOMIC_INT state; /**< 状态机 (dev_lc_state_t) */
    };

    void dev_lc_init(struct dev_lifecycle* lc);
    void dev_lc_reset(struct dev_lifecycle* lc);

    dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc);
    int dev_lc_opens(const struct dev_lifecycle* lc);
    int dev_lc_io_active_count(const struct dev_lifecycle* lc);

    int dev_lc_open_begin(struct dev_lifecycle* lc) COMPAT_WARN_UNUSED_RESULT;
    void dev_lc_open_end(struct dev_lifecycle* lc);
    void dev_lc_open_abort(struct dev_lifecycle* lc);

    int dev_lc_close_begin(struct dev_lifecycle* lc) COMPAT_WARN_UNUSED_RESULT;
    void dev_lc_close_end(struct dev_lifecycle* lc);

    int dev_lc_io_begin(struct dev_lifecycle* lc) COMPAT_WARN_UNUSED_RESULT;
    void dev_lc_io_end(struct dev_lifecycle* lc);

    void dev_lc_remove_start(struct dev_lifecycle* lc);

    int dev_lc_remove_drain(struct dev_lifecycle* lc,
                            uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

    void dev_lc_remove_finish(struct dev_lifecycle* lc);

#ifdef __cplusplus
}
#endif

#endif /* DEV_LIFECYCLE_H */
