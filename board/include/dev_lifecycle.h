/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file dev_lifecycle.h
 *@brief dev lifecycle 头文件
 *@author H-000-H
 *@details
 *   dev_lifecycle.h — 设备 I/O 生命周期状态机 (CAS 哨兵版, 无锁)
 *   opens / io_active 使用 MINI_ATOMIC_INT, -1 哨兵表示 "teardown 已锁定".
 *   open_begin / io_begin: CAS 循环递增, 遇 -1 或 state != LIVE 直接拒绝.
 *   remove_drain: CAS opens 0→-1, 成功后保持锁定并 CAS io_active 0→-1,
 *   两阶段全部成功才返回. 第一阶段失败 (opens 临时 +1) 重试等待稳定归零;
 *   第二阶段失败不触碰 opens (保持 -1), 仅重试 io_active.
 *   退避由 timeout_ms + 1 ms 周期驱动, 非自旋.
 *   remove_finish: reset 恢复全部为 0 / UNINITIALIZED.
 *   正确性论证:
 *   - 状态机门控: remove_drain 进入前提 state == REMOVING, 而 state 切换
 *   由单一调用方 (driver remove) 完成, 不会出现并发 "进入 REMOVING";
 *   REMOVING 下 open_begin / io_begin 均检查 state == LIVE, 故无新计数递增.
 *   - 单调锁定: opens 一旦 CAS 0→-1 成功即保持 -1, 绝不回滚到 0 (消除把 opens
 *   短暂暴露为 0 的窗口); io_active 在 opens 锁定后反复 CAS 0→-1 直至归零.
 *   - 内存序: load 用 ACQUIRE 看见 state 切换; CAS 用 ACQ_REL 与失败路径的
 *   RELAXED 配对, 不需要全序语义; 写终态用 RELEASE 配 ACQUIRE;
 *   - 无 ABA: opens/io_active 单调递增/递减, 哨兵 -1 是终态 (经
 *   remove_finish 才回 0), 中间不可能再现同值导致 CAS 误判;
 *   - 无竞态窗口: drain 退出时两计数器均稳定为 -1, open/io_begin 见到 -1 或
 *   state != LIVE 立即拒绝, 不可能出现 "drain 已成功但有 IO 在做".
 *   无互斥锁, 无脉冲窗口, 无 ABA.
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
        MINI_ATOMIC_INT opens; /**< 当前打开计数 (-1 = teardown 锁定) */
        MINI_ATOMIC_INT io_active; /**< 当前 I/O 活跃计数 (-1 = teardown 锁定) */
        MINI_ATOMIC_INT state; /**< 状态机 (dev_lc_state_t) */
    };

    /**
     * @brief 初始化设备生命周期状态机 (opens/io_active=0, state=UNINITIALIZED)
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_init(struct dev_lifecycle* lc);
    /**
     * @brief 复位生命周期 (移除完成后恢复为 0/UNINITIALIZED)
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_reset(struct dev_lifecycle* lc);

    /**
     * @brief 查询当前生命周期状态
     * @param[in] lc 生命周期状态机指针
     * @return dev_lc_state_t 状态
     */
    dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc);
    /**
     * @brief 查询当前打开计数
     * @param[in] lc 生命周期状态机指针
     * @return opens (-1 表示 teardown 锁定)
     */
    int dev_lc_opens(const struct dev_lifecycle* lc);
    /**
     * @brief 查询当前活跃 I/O 计数
     * @param[in] lc 生命周期状态机指针
     * @return io_active (-1 表示 teardown 锁定)
     */
    int dev_lc_io_active_count(const struct dev_lifecycle* lc);

    /**
     * @brief 打开操作前段: CAS 递增 opens, 拒绝 teardown/非 LIVE 状态
     * @param[in] lc 生命周期状态机指针
     * @return 允许打开返回 MINI_OK, teardown 或未 LIVE 返回 MINI_ERR_NODEV
     */
    int dev_lc_open_begin(struct dev_lifecycle* lc) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 打开操作后段: 递减 opens (与 open_begin 配对)
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_open_end(struct dev_lifecycle* lc);
    /**
     * @brief 打开操作放弃: 回滚 open_begin 的递增
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_open_abort(struct dev_lifecycle* lc);

    /**
     * @brief 关闭操作前段: 递减 opens (归零前阻止)
     * @param[in] lc 生命周期状态机指针
     * @return 允许关闭返回 MINI_OK, 计数非法返回 MINI_ERR_INVAL
     */
    int dev_lc_close_begin(struct dev_lifecycle* lc) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭操作后段: 递增 opens (与 close_begin 配对)
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_close_end(struct dev_lifecycle* lc);

    /**
     * @brief I/O 操作前段: CAS 递增 io_active, 拒绝 teardown/非 LIVE 状态
     * @param[in] lc 生命周期状态机指针
     * @return 允许 I/O 返回 MINI_OK, teardown 或未 LIVE 返回 MINI_ERR_NODEV
     */
    int dev_lc_io_begin(struct dev_lifecycle* lc) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief I/O 操作后段: 递减 io_active (与 io_begin 配对)
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_io_end(struct dev_lifecycle* lc);

    /**
     * @brief 移除开始: 进入 REMOVING 状态, 禁止新 open/IO
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_remove_start(struct dev_lifecycle* lc);

    /**
     * @brief 移除排空: 锁定 opens=-1, 并等待 io_active 归零为 -1
     * @param[in] lc 生命周期状态机指针
     * @param[in] timeout_ms 超时毫秒数
     * @return 排空完成返回 MINI_OK, 超时返回 MINI_ERR_TIMEOUT
     */
    int dev_lc_remove_drain(struct dev_lifecycle* lc,
                            uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

    /**
     * @brief 移除完成: 复位计数器, 回到 UNINITIALIZED
     * @param[in] lc 生命周期状态机指针
     */
    void dev_lc_remove_finish(struct dev_lifecycle* lc);

#ifdef __cplusplus
}
#endif

#endif /* DEV_LIFECYCLE_H */
