#ifndef DEV_LIFECYCLE_H
#define DEV_LIFECYCLE_H

#include <stdint.h>

#include "VFS.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" 
{
#endif

struct osal_mutex;

/*
 * dev_lifecycle — 类 Linux 字符设备生命周期
 *
 * 阶段映射:
 *   probe  → dev_lc_init() / device_lc_bind()
 *   open   → dev_lc_open_begin() / dev_lc_open_end()
 *   I/O    → dev_lc_io_begin()   / dev_lc_io_end()
 *   close  → dev_lc_close_begin() / dev_lc_close_end()
 *   remove → dev_lc_remove_start()
 *            device_ops_unregister()   (框架层, 切断 VFS fops)
 *            dev_lc_remove_drain()     (见下方「持锁返回」契约)
 *            ... 驱动 teardown (仍持 io_lock) ...
 *            dev_lc_remove_finish()    (释放 io_lock)
 */

typedef enum dev_lc_state
{
    DEV_LC_UNINITIALIZED = 0,
    DEV_LC_LIVE,       /* probe 完成, 接受 open / I/O */
    DEV_LC_REMOVING,   /* remove 已开始, 拒绝新 open / I/O */
    DEV_LC_DEAD,
} dev_lc_state_t;

struct dev_lifecycle
{
    struct osal_mutex* io_lock;  /* 普通互斥锁 (osal_mutex_create_static) */
    int                opens;
    int                io_active;
    dev_lc_state_t     state;
};

void dev_lc_init(struct dev_lifecycle* lc, struct osal_mutex* io_lock);
void dev_lc_reset(struct dev_lifecycle* lc);

dev_lc_state_t dev_lc_state(const struct dev_lifecycle* lc);
int dev_lc_opens(const struct dev_lifecycle* lc);
int dev_lc_io_active_count(const struct dev_lifecycle* lc);

/*
 * open: 持锁进入; 返回 1=首次 open, 0=非首次, <0 错误.
 * 成功路径: open_begin → ... → open_end.
 */
int dev_lc_open_begin(struct dev_lifecycle* lc, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
void dev_lc_open_end(struct dev_lifecycle* lc);

/*
 * open_begin 后初始化失败: 撤销 opens++, 释放 open_begin 持有的锁.
 * 与 open_end 互斥 — 失败路径只调 abort, 勿再调 open_end.
 */
void dev_lc_open_abort(struct dev_lifecycle* lc);

/*
 * close: 持锁进入; 返回 1=末次 close, 0=仍有引用, <0 错误.
 */
int dev_lc_close_begin(struct dev_lifecycle* lc, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
void dev_lc_close_end(struct dev_lifecycle* lc);

/*
 * I/O: write/read/ioctl 临界区; begin 持锁, end 释放.
 * REMOVING 状态下返回 VFS_ERR_NODEV (非 TIMEOUT).
 */
int dev_lc_io_begin(struct dev_lifecycle* lc, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
void dev_lc_io_end(struct dev_lifecycle* lc);

/* remove 第一阶段: 标记 REMOVING (不持锁) */
void dev_lc_remove_start(struct dev_lifecycle* lc);

/*
 * remove 第二阶段: 轮询等待 opens 与 io_active 均为 0.
 *
 * ── 持锁返回契约 (所有驱动必须遵守) ──
 *
 * 成功 (VFS_OK) 时:
 *   - 调用方仍持有 lc->io_lock (由本函数在最后一次 lock 成功后直接返回,
 *     不 unlock).
 *   - opens == 0 且 io_active == 0.
 *
 * 必须与 dev_lc_remove_finish() 成对使用:
 *   - drain 与 finish 之间, 锁的生命周期跨越两次函数调用边界.
 *   - 中间禁止: dev_lc_io_begin / open_begin / close_begin 等会再次
 *     获取同一把 io_lock 的路径.
 *   - teardown 代码 (HAL close、清缓冲、destroy mutex 等) 须假定已持锁,
 *     且不得调用会同步/异步回调进驱动 fops 并再次抢 io_lock 的 API
 *     (例如带 completion handler 且 handler 内会 dev_lc_io_begin 的发送路径).
 *     若 HAL 为纯同步阻塞且无 re-enter, 则可在持锁窗口内安全调用.
 *
 * 失败时: 不保证持锁; 调用方不得调用 dev_lc_remove_finish() 除非明确
 * 知晓当前锁状态 (通常仅记录错误并保留资源).
 */
int dev_lc_remove_drain(struct dev_lifecycle* lc, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/*
 * remove 收尾: 释放 dev_lc_remove_drain() 成功返回时持有的 io_lock.
 * 不 destroy mutex — 由驱动在 unlock 后自行 osal_mutex_destroy.
 * 调用后 lc 重置为 DEV_LC_UNINITIALIZED.
 *
 * 仅在与成功的 dev_lc_remove_drain() 配对时调用.
 */
void dev_lc_remove_finish(struct dev_lifecycle* lc);

#ifdef __cplusplus
}
#endif

#endif /* DEV_LIFECYCLE_H */

