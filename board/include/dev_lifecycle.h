/**
 * @file dev_lifecycle.h
 * @author H-000-H
 * @brief 设备 I/O 生命周期状态机 (CAS 哨兵版, 无锁)
 * @note  opens/io_active 用 -1 哨兵表示 teardown 锁定; remove_drain 两阶段 CAS 归零
 * @copyright SPDX-License-Identifier: Apache-2.0
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
    DEV_LC_LIVE,              /**< 运行中 */
    DEV_LC_REMOVING,          /**< 正在移除 */
    DEV_LC_DEAD,              /**< 已死亡 */
} dev_lc_state_t;

struct dev_lifecycle
{
    MINI_ATOMIC_INT opens;     /**< 当前打开计数 (-1 = teardown 锁定) */
    MINI_ATOMIC_INT io_active; /**< 当前 I/O 活跃计数 (-1 = teardown 锁定) */
    MINI_ATOMIC_INT state;     /**< 状态机 (dev_lc_state_t) */
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
mt_err_t dev_lc_open_begin(struct dev_lifecycle* lc) MINI_WARN_UNUSED_RESULT;
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
mt_err_t dev_lc_close_begin(struct dev_lifecycle* lc) MINI_WARN_UNUSED_RESULT;
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
mt_err_t dev_lc_io_begin(struct dev_lifecycle* lc) MINI_WARN_UNUSED_RESULT;
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
mt_err_t dev_lc_remove_drain(struct dev_lifecycle* lc, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 移除完成: 复位计数器, 回到 UNINITIALIZED
 * @param[in] lc 生命周期状态机指针
 */
void dev_lc_remove_finish(struct dev_lifecycle* lc);

#ifdef __cplusplus
}
#endif

#endif /* DEV_LIFECYCLE_H */
