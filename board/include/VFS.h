/* SPDX-License-Identifier: Apache-2.0 */
/*
 * VFS.h — VFS 错误码与指针错误编码头文件
 *
 * 定义 VFS_ERR_xxx 错误码 (映射到 -EAGAIN/-ETIMEDOUT/-EHWPOISON 等 errno),
 * 提供 ERR_PTR/PTR_ERR/IS_ERR 内联函数 (Linux 内核风格指针-错误编码).
 * 错误指针落入 ERR_SECTION_BASE 之上的保留段, 与有效设备指针区分.
 */
#ifndef VFS_H
#define VFS_H
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

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

#define VFS_ERR_INVAL    (-EINVAL)       /* 无效参数 */
#define VFS_ERR_NOMEM    (-ENOMEM)       /* 内存不足 */
#define VFS_ERR_IO       (-EIO)          /* 物理 IO 错误 */
#define VFS_ERR_BUSY     (-EBUSY)        /* 设备忙 */
#define VFS_ERR_AGAIN    (-EAGAIN)       /* 重试 */
#define VFS_ERR_NOSPC    (-ENOSPC)       /* 无剩余空间/通道 */
#define VFS_ERR_TIMEOUT  (-ETIMEDOUT)    /* 锁获取/操作超时 */
#define VFS_ERR_HW_FATAL (-EHWPOISON)    /* 硬件物理故障, 不可恢复 */
#define VFS_ERR_DEFER    (-EPROBE_DEFER) /* 依赖未就绪, 稍后重试 */
#define VFS_ERR_NODEV    (-ENODEV)       /* 设备已拆除或不存在 */
#define VFS_ERR_NOTSUPP  (-ENOSYS)       /* 操作不支持/未实现 */

/* 指针的特殊处理 */
extern const char ERR_SECTION_BASE;
#define ERR_BASE ((uintptr_t)&ERR_SECTION_BASE)

static inline void* ERR_PTR(int err)
{
    int abs_err = (err < 0) ? -err : err;

    if (abs_err > VFS_ERR_MAX) {
        abs_err = EINVAL;
    }

    return (void *)(ERR_BASE + (uintptr_t)abs_err);
}
static inline int PTR_ERR(const void* PTR)
{
    return -(int)(((uintptr_t)PTR) - ERR_BASE);
}

static inline bool IS_ERR(const void* ptr)
{
    return (uintptr_t)ptr >= ERR_BASE;
}

#endif /* VFS_H */
