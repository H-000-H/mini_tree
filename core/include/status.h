/* SPDX-License-Identifier: Apache-2.0 */
/*
 * status.h — 栈公共状态码与指针错误编码 (层无关)
 *
 * HAL / bus / VFS / OSAL 共用。HAL 不得依赖 VFS.h；需要错误码时包含本头。
 * VFS.h 为兼容包装，转发到本文件。
 */
#ifndef STATUS_H
#define STATUS_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include "compiler_inline.h"

#define VFS_ERR_MAX 255
#define VFS_OK 0
#ifndef EHWPOISON
#define EHWPOISON 134
#endif

#ifndef EPROBE_DEFER
#define EPROBE_DEFER 140
#endif

#ifndef ENOSYS
#define ENOSYS 38
#endif

#define VFS_ERR_INVAL    (-EINVAL)          /* 无效参数 */
#define VFS_ERR_ISR      (-EPERM)           /* 中断上下文非法调用 */
#define VFS_ERR_NOMEM    (-ENOMEM)          /* 内存不足 */
#define VFS_ERR_IO       (-EIO)             /* 物理 IO 错误 */
#define VFS_ERR_BUSY     (-EBUSY)           /* 设备忙 */
#define VFS_ERR_AGAIN    (-EAGAIN)          /* 重试 */
#define VFS_ERR_NOSPC    (-ENOSPC)          /* 无剩余空间/通道 */
#define VFS_ERR_TIMEOUT  (-ETIMEDOUT)       /* 锁获取/操作超时 */
#define VFS_ERR_HW_FATAL (-EHWPOISON)       /* 硬件物理故障, 不可恢复 */
#define VFS_ERR_DEFER    (-EPROBE_DEFER)    /* 依赖未就绪, 稍后重试 */
#define VFS_ERR_NODEV    (-ENODEV)          /* 设备已拆除或不存在 */
#define VFS_ERR_NOTSUPP  (-ENOSYS)          /* 操作不支持/未实现 */
#define VFS_IRQ_ENTRY_BOTTOM (0X01U)        /* 中断下部入口标识 */
#define VFS_IRQ_ENTRY_NOBOTTOM    (0X00U)   /* 中断上部入口标识 */

/* OSAL 错误码 — 语义与公共状态码对齐，数值相同，可零开销互转 */
#define OSAL_OK          VFS_OK
#define OSAL_ERR_INVAL   VFS_ERR_INVAL
#define OSAL_ERR_NOMEM   VFS_ERR_NOMEM
#define OSAL_ERR_IO      VFS_ERR_IO
#define OSAL_ERR_BUSY    VFS_ERR_BUSY
#define OSAL_ERR_TIMEOUT VFS_ERR_TIMEOUT
#define OSAL_ERR_NODEV   VFS_ERR_NODEV
#define OSAL_ERR_NOTSUPP VFS_ERR_NOTSUPP
#define OSAL_ERR_ISR     (-EPERM)        /* 中断上下文非法调用 */

/* 指针的特殊处理 */
extern const char ERR_SECTION_BASE;
#define ERR_BASE ((uintptr_t)&ERR_SECTION_BASE)

COMPAT_STATIC_INLINE void* ERR_PTR(int err)
{
    int abs_err = (err < 0) ? -err : err;

    if (abs_err > VFS_ERR_MAX) {
        abs_err = EINVAL;
    }

    return (void *)(ERR_BASE + (uintptr_t)abs_err);
}
COMPAT_STATIC_INLINE int PTR_ERR(const void* PTR)
{
    return -(int)(((uintptr_t)PTR) - ERR_BASE);
}

COMPAT_STATIC_INLINE bool IS_ERR(const void* ptr)
{
    return (uintptr_t)ptr >= ERR_BASE;
}

COMPAT_STATIC_INLINE bool IS_ERR_OR_NULL(const void* ptr)
{
    return (ptr == NULL) || IS_ERR(ptr);
}

#endif /* STATUS_H */
