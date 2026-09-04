/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file status.h
 *@brief status 头文件
 *@author H-000-H
 *@details
 *   status.h — 栈公共状态码与指针错误编码 (层无关)
 *   HAL / bus / VFS / OSAL 共用。HAL 不得依赖 VFS.h；需要错误码时包含本头。
 *   VFS.h 为兼容包装，转发到本文件。
 */

#ifndef STATUS_H
#define STATUS_H

#include "compiler_inline.h"
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MINI_ERR_MAX 255
#define MINI_OK 0
#ifndef EHWPOISON
#define EHWPOISON 134
#endif

#ifndef EPROBE_DEFER
#define EPROBE_DEFER 140
#endif

#ifndef ENOSYS
#define ENOSYS 38
#endif

#define MINI_ERR_INVAL (-EINVAL)        /* 无效参数 */
#define MINI_ERR_ISR (-EPERM)           /* 中断上下文非法调用 */
#define MINI_ERR_NOMEM (-ENOMEM)        /* 内存不足 */
#define MINI_ERR_IO (-EIO)              /* 物理 IO 错误 */
#define MINI_ERR_BUSY (-EBUSY)          /* 设备忙 */
#define MINI_ERR_AGAIN (-EAGAIN)        /* 重试 */
#define MINI_ERR_NOSPC (-ENOSPC)        /* 无剩余空间/通道 */
#define MINI_ERR_TIMEOUT (-ETIMEDOUT)   /* 锁获取/操作超时 */
#define MINI_ERR_HW_FATAL (-EHWPOISON)  /* 硬件物理故障, 不可恢复 */
#define MINI_ERR_DEFER (-EPROBE_DEFER)  /* 依赖未就绪, 稍后重试 */
#define MINI_ERR_NODEV (-ENODEV)        /* 设备已拆除或不存在 */
#define MINI_ERR_NOTSUPP (-ENOSYS)      /* 操作不支持/未实现 */
#define MINI_IRQ_ENTRY_BOTTOM (0X01U)   /* 中断下部入口标识 */
#define MINI_IRQ_ENTRY_NOBOTTOM (0X00U) /* 中断上部入口标识 */

/**
 * @brief OSAL 错误码 — 语义与公共状态码对齐，数值相同
 */
#define OSAL_OK MINI_OK
#define OSAL_ERR_INVAL MINI_ERR_INVAL
#define OSAL_ERR_NOMEM MINI_ERR_NOMEM
#define OSAL_ERR_IO MINI_ERR_IO
#define OSAL_ERR_BUSY MINI_ERR_BUSY
#define OSAL_ERR_TIMEOUT MINI_ERR_TIMEOUT
#define OSAL_ERR_NODEV MINI_ERR_NODEV
#define OSAL_ERR_NOTSUPP MINI_ERR_NOTSUPP
#define OSAL_ERR_ISR (-EPERM) /* 中断上下文非法调用 */

/* 指针的特殊处理 */
extern const char ERR_SECTION_BASE;
#define ERR_BASE ((uintptr_t)&ERR_SECTION_BASE)

MINI_STATIC_INLINE void* ERR_PTR(int err)
{
    int abs_err = (err < 0) ? -err : err;

    if (abs_err > MINI_ERR_MAX)
        abs_err = EINVAL;

    return (void*)(ERR_BASE + (uintptr_t)abs_err);
}
/**
 * @brief 从 ERR_PTR 指针还原错误码 (与 ERR_PTR 互逆)
 * @param[in] PTR ERR_PTR 返回的指针
 * @return 负的错误码 (VFS_ERR_*)
 */
MINI_STATIC_INLINE int PTR_ERR(const void* PTR) { return -(int)(((uintptr_t)PTR) - ERR_BASE); }

/**
 * @brief 判断指针是否为 ERR_PTR 编码的错误指针
 * @param[in] ptr 待判断指针
 * @return 错误指针返回 true
 */
MINI_STATIC_INLINE bool IS_ERR(const void* ptr) { return (uintptr_t)ptr >= ERR_BASE; }

/**
 * @brief 判断指针为 NULL 或 ERR_PTR 编码的错误指针
 * @param[in] ptr 待判断指针
 * @return NULL 或错误指针返回 true
 */
MINI_STATIC_INLINE bool IS_ERR_OR_NULL(const void* ptr) { return (ptr == NULL) || IS_ERR(ptr); }

#endif /* STATUS_H */
