/**
 * @file sys_arch.h
 * @author H-000-H
 * @brief sys arch 头文件
 * @note net/arch/sys_arch.h
 * @note lwIP 操作系统抽象移植层头文件 (mini_tree 适配)
 * @note 位置约定: lwIP 的 lwip/sys.h 通过 #include "arch/sys_arch.h" 引用本文件,
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYS_ARCH_H
#define SYS_ARCH_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lwip/opt.h"
#include "mini_backend.h"
#include "mini_time.h"

/* -------------------------------------------------------------------------- */
/* lwIP -> 统一接口 type mapping                                              */
/* NO_SYS=0 (RTOS): sys_* 原语为指针句柄;                                     */
/* NO_SYS=1 (裸机): lwip/sys.h 已将 sys_sem/mutex/mbox_t 定义为 u8_t 空桩,     */
/*   故信号量类型 (裸机后端不提供) 仅在 OS 配置下给出。                        */
/* -------------------------------------------------------------------------- */
typedef mini_mutex_t*      port_mutex_t;  /* 互斥锁 */
#if !defined(CONFIG_OS_BARE)
typedef mini_sem_t*        port_sem_t;    /* 信号量 */
#endif
typedef mini_task_handle_t port_thread_t; /* 线程句柄 */
typedef mini_queue_t*      port_mbox_t;   /* 邮箱 */

#if NO_SYS == 0
typedef port_mutex_t sys_mutex_t; /* 互斥锁 */
typedef port_sem_t   sys_sem_t;   /* 信号量 */
typedef port_mbox_t  sys_mbox_t;  /* 邮箱 */
#endif                            /* NO_SYS == 0 */

typedef port_thread_t sys_thread_t; /* 线程句柄 */

/* -------------------------------------------------------------------------- */
/* Invalid handles                                                            */
/* -------------------------------------------------------------------------- */
#define SYS_MBOX_NULL NULL   /* 邮箱空句柄 */
#define SYS_SEM_NULL NULL    /* 信号量空句柄 */
#define SYS_MUTEX_NULL NULL  /* 互斥锁空句柄 */
#define SYS_THREAD_NULL NULL /* 线程空句柄 */

/* -------------------------------------------------------------------------- */
/* valid / set_invalid: 指针句柄的 NULL 判定                                  */
/* 仅 NO_SYS=0 (RTOS) 模式由移植层提供: 该分支 lwip/sys.h 不自带这些宏。        */
/* NO_SYS=1 裸机分支 lwip/sys.h 已提供空桩宏, 此处不再定义以免重定义。          */
/* -------------------------------------------------------------------------- */
#if NO_SYS == 0
#define sys_sem_valid(sem) (((sem) != NULL) && (*(sem) != NULL))
#define sys_sem_set_invalid(sem)                                                                                                                     \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if ((sem) != NULL)                                                                                                                           \
        {                                                                                                                                            \
            *(sem) = NULL;                                                                                                                           \
        }                                                                                                                                            \
    } while (0)

#define sys_mutex_valid(mutex) (((mutex) != NULL) && (*(mutex) != NULL))
#define sys_mutex_set_invalid(mutex)                                                                                                                 \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if ((mutex) != NULL)                                                                                                                         \
        {                                                                                                                                            \
            *(mutex) = NULL;                                                                                                                         \
        }                                                                                                                                            \
    } while (0)

#define sys_mbox_valid(mbox) (((mbox) != NULL) && (*(mbox) != NULL))
#define sys_mbox_set_invalid(mbox)                                                                                                                   \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if ((mbox) != NULL)                                                                                                                          \
        {                                                                                                                                            \
            *(mbox) = NULL;                                                                                                                          \
        }                                                                                                                                            \
    } while (0)

#define sys_sem_valid_val(sem) sys_sem_valid(&(sem))
#define sys_sem_set_invalid_val(sem) sys_sem_set_invalid(&(sem))
#define sys_mbox_valid_val(mbox) sys_mbox_valid(&(mbox))
#define sys_mbox_set_invalid_val(mbox) sys_mbox_set_invalid(&(mbox))

/* -------------------------------------------------------------------------- */
/* sys_msleep: RTOS 模式下用 mini_delay_ms 实现睡眠 (裸机 NO_SYS=1 由 lwIP 提供)*/
/* -------------------------------------------------------------------------- */
#define sys_msleep(ms) mini_delay_ms(ms)
#endif /* NO_SYS == 0 */

/* -------------------------------------------------------------------------- */
/* lwIP constants                                                             */
/* -------------------------------------------------------------------------- */
#ifndef SYS_ARCH_TIMEOUT
#define SYS_ARCH_TIMEOUT (UINT32_MAX) /* lwIP 超时值 */
#endif

#ifndef SYS_MBOX_EMPTY
#define SYS_MBOX_EMPTY (UINT32_MAX) /* 邮箱空消息 */
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* SYS_ARCH_H */
