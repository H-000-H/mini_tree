/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sys_arch.h
 *@brief sys arch 头文件
 *@author H-000-H
 *@details
 *   net/arch/sys_arch.h
 *   lwIP 操作系统抽象移植层头文件 (mini_tree 适配)
 *   位置约定: lwIP 的 lwip/sys.h 通过 #include "arch/sys_arch.h" 引用本文件,
 *   故必须置于 port_include_dir/arch/ 下 (与 arch/cc.h 同级, 即 net/arch/)。
 *   将 lwIP 的 sys_* 原语桥接到 OSAL (osal_mutex/osal_sem/osal_queue/osal_task)。
 */

#ifndef SYS_ARCH_H
#define SYS_ARCH_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lwip/opt.h"
#include "osal.h"

    /* -------------------------------------------------------------------------- */
    /* lwIP -> OSAL type mapping                                                  */
    /* NO_SYS=0 (RTOS): sys_* 原语为 OSAL 指针句柄;                               */
    /* NO_SYS=1 (裸机): lwip/sys.h 已将 sys_sem/mutex/mbox_t 定义为 u8_t 空桩,      */
    /* -------------------------------------------------------------------------- */
    typedef struct osal_mutex* osal_mutex_t; /* 互斥锁 */
    typedef struct osal_sem* osal_sem_t; /* 信号量 */
    typedef osal_task_handle_t osal_thread_t; /* 线程句柄 */
    typedef osal_queue_handle_t osal_mbox_t; /* 邮箱 */

#if NO_SYS == 0
    typedef osal_mutex_t sys_mutex_t; /* 互斥锁 */
    typedef osal_sem_t sys_sem_t; /* 信号量 */
    typedef osal_mbox_t sys_mbox_t; /* 邮箱 */
#endif /* NO_SYS == 0 */

    typedef osal_thread_t sys_thread_t; /* 线程句柄 */

/* -------------------------------------------------------------------------- */
/* Invalid handles                                                            */
/* -------------------------------------------------------------------------- */
#define SYS_MBOX_NULL NULL /* 邮箱空句柄 */
#define SYS_SEM_NULL NULL /* 信号量空句柄 */
#define SYS_MUTEX_NULL NULL /* 互斥锁空句柄 */
#define SYS_THREAD_NULL NULL /* 线程空句柄 */

/* -------------------------------------------------------------------------- */
/* valid / set_invalid: 指针句柄的 NULL 判定                                  */
/* 仅 NO_SYS=0 (RTOS) 模式由移植层提供: 该分支 lwip/sys.h 不自带这些宏。        */
/* NO_SYS=1 裸机分支 lwip/sys.h 已提供空桩宏, 此处不再定义以免重定义。          */
/* -------------------------------------------------------------------------- */
#if NO_SYS == 0
#define sys_sem_valid(sem) (((sem) != NULL) && (*(sem) != NULL))
#define sys_sem_set_invalid(sem)                                                                                                                                                                       \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if ((sem) != NULL)                                                                                                                                                                             \
        {                                                                                                                                                                                              \
            *(sem) = NULL;                                                                                                                                                                             \
        }                                                                                                                                                                                              \
    } while (0)

#define sys_mutex_valid(mutex) (((mutex) != NULL) && (*(mutex) != NULL))
#define sys_mutex_set_invalid(mutex)                                                                                                                                                                   \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if ((mutex) != NULL)                                                                                                                                                                           \
        {                                                                                                                                                                                              \
            *(mutex) = NULL;                                                                                                                                                                           \
        }                                                                                                                                                                                              \
    } while (0)

#define sys_mbox_valid(mbox) (((mbox) != NULL) && (*(mbox) != NULL))
#define sys_mbox_set_invalid(mbox)                                                                                                                                                                     \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if ((mbox) != NULL)                                                                                                                                                                            \
        {                                                                                                                                                                                              \
            *(mbox) = NULL;                                                                                                                                                                            \
        }                                                                                                                                                                                              \
    } while (0)

#define sys_sem_valid_val(sem) sys_sem_valid(&(sem))
#define sys_sem_set_invalid_val(sem) sys_sem_set_invalid(&(sem))
#define sys_mbox_valid_val(mbox) sys_mbox_valid(&(mbox))
#define sys_mbox_set_invalid_val(mbox) sys_mbox_set_invalid(&(mbox))

/* -------------------------------------------------------------------------- */
/* sys_msleep: RTOS 模式下用 osal_delay_ms 实现睡眠 (裸机 NO_SYS=1 由 lwIP 提供)*/
/* -------------------------------------------------------------------------- */
#define sys_msleep(ms) osal_delay_ms(ms)
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

/* -------------------------------------------------------------------------- */
/* lwIP -> OSA                                                                */
/* -------------------------------------------------------------------------- */
#define SYS_OK OSAL_OK /* lwIP 成功值 */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* SYS_ARCH_H */
