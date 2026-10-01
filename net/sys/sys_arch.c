/**
 * @file sys_arch.c
 * @author H-000-H
 * @brief sys arch 实现
 * @note net/sys/sys_arch.c
 * @note lwIP 操作系统抽象移植层实现 (mini_tree 适配)
 * @note 双模式:
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "arch/sys_arch.h"

#include "lwip/sys.h"
#include "system_log.h"
#include "mini_time.h"
#include <stdarg.h>
#include <stdio.h>

/* 裸机轻量临界区 (SYS_LIGHTWEIGHT_PROT): 可嵌套关中断 */
#include "mini_critical.h"

#if NO_SYS == 0
#include "lwip/err.h"
#endif /* NO_SYS == 0 */

/* -------------------------------------------------------------------------- */
/* 通用: 初始化 / 时钟 / 诊断                                                 */
/* -------------------------------------------------------------------------- */
void sys_init(void) { /* lwip 要求 tcpip 第一步调用; 系统两阶段初始化已完成, 此处无事可做 */ }

u32_t sys_now(void) { return (u32_t)mini_time_ms(); }

void lwip_diag(const char* fmt, ...)
{
    char    buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    MT_LOG_INFO("lwIP", "%s", buf);
}

/* -------------------------------------------------------------------------- */
/* 裸机 NO_SYS=1: 仅轻量临界区 (SYS_LIGHTWEIGHT_PROT)                          */
/* lwIP 自带 sys_sem/mutex/mbox/thread 空桩, 无需本文件实现。                  */
/* -------------------------------------------------------------------------- */
#if NO_SYS == 1
#if defined(CONFIG_OS_BARE)
sys_prot_t sys_arch_protect(void) { return (sys_prot_t)mini_critical_enter(); }

void sys_arch_unprotect(sys_prot_t pval) { mini_critical_exit((mini_irq_state_t)pval); }
#endif /* CONFIG_OS_BARE */
#endif /* NO_SYS == 1 */

/* -------------------------------------------------------------------------- */
/* RTOS NO_SYS=0: 完整 sys_* 原语桥接到统一接口                                */
/* -------------------------------------------------------------------------- */
#if NO_SYS == 0

/* -------------------------------------------------------------------------- */
/* Semaphore                                                                  */
/* -------------------------------------------------------------------------- */
err_t sys_sem_new(sys_sem_t* sem, u8_t count)
{
    if (sem == NULL)
        return ERR_VAL;

    /* 要求初始计数 count(0 或 1) 所以直接调用二值信号量反正内容都一样*/
    if (mini_sem_create_binary(sem) != MINI_OK)
        return ERR_MEM;
    if (count >= 1) /*count =1补一次信号量就行*/
        MINI_IGNORE_RESULT(mini_sem_post(*sem));
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t* sem)
{
    if (sem == NULL || *sem == NULL)
        return;
    MINI_IGNORE_RESULT(mini_sem_post(*sem));
}

void sys_sem_free(sys_sem_t* sem)
{
    if (sem == NULL || *sem == NULL)
        return;
    mini_sem_destroy(*sem);
    *sem = SYS_SEM_NULL;
}

/* -------------------------------------------------------------------------- */
/* Mutex                                                                      */
/* -------------------------------------------------------------------------- */
err_t sys_mutex_new(sys_mutex_t* mutex)
{
    if (mutex == NULL)
        return ERR_VAL;
    /* 池化普通锁; lwIP 对锁的递归性无要求, 默认非递归即可 */
    if (mini_mutex_create(mutex) != MINI_OK)
        return ERR_MEM;
    return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t* mutex)
{
    if (mutex == NULL || *mutex == NULL)
        return;
    MINI_IGNORE_RESULT(mini_mutex_lock(*mutex, MINI_WAIT_FOREVER));
}

void sys_mutex_unlock(sys_mutex_t* mutex)
{
    if (mutex == NULL || *mutex == NULL)
        return;
    MINI_IGNORE_RESULT(mini_mutex_unlock(*mutex));
}

void sys_mutex_free(sys_mutex_t* mutex)
{
    if (mutex == NULL || *mutex == NULL)
        return;
    mini_mutex_destroy(*mutex);
    *mutex = SYS_MUTEX_NULL;
}

/* -------------------------------------------------------------------------- */
/* Mailbox (元素为 void*, 经定长队列承载, size = sizeof(void*))                */
/* -------------------------------------------------------------------------- */
err_t sys_mbox_new(sys_mbox_t* mbox, int size)
{
    if (mbox == NULL || size <= 0)
        return ERR_VAL;
    *mbox = mini_queue_create(size, sizeof(void*));
    if (*mbox == SYS_MBOX_NULL)
        return ERR_MEM;
    return ERR_OK;
}

void sys_mbox_post(sys_mbox_t* mbox, void* msg)
{
    if (mbox == NULL || *mbox == NULL)
        return;
    /*简单点直接不准失败，阻塞式*/
    MINI_IGNORE_RESULT(mini_queue_send(*mbox, &msg, MINI_WAIT_FOREVER));
}

err_t sys_mbox_trypost(sys_mbox_t* mbox, void* msg)
{
    if (mbox == NULL || *mbox == NULL)
        return ERR_VAL;
    /* mini_queue_send 返回 bool; 原先与 MINI_OK(=0) 比较把成功判成了失败 */
    return mini_queue_send(*mbox, &msg, 0) ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t* mbox, void* msg)
{
    bool yield_required = false;
    if (mbox == NULL || *mbox == NULL)
        return ERR_VAL;
    if (mini_queue_send_from_isr(*mbox, &msg, &yield_required))
    {
        mini_yield_from_isr(yield_required); /* ISR 最外层出口 */
        return ERR_OK;
    }
    else
        return ERR_MEM;
}

void sys_mbox_free(sys_mbox_t* mbox)
{
    if (mbox == NULL || *mbox == NULL)
        return;
    mini_queue_delete(*mbox);
    *mbox = SYS_MBOX_NULL;
}

/* -------------------------------------------------------------------------- */
/* Thread                                                                     */
/* -------------------------------------------------------------------------- */
/*裸机不可能线程不需要想为什么我调度器有了两个但是依然只有os的时候才有这个东西*/
sys_thread_t sys_thread_new(const char* name, lwip_thread_fn thread, void* arg, int stacksize, int prio)
{
    mini_task_handle_t task_handle = NULL;
    int                ret = mini_task_create_handle(name, stacksize, prio, thread, arg, -1, &task_handle);
    if (ret != MINI_OK)
    {
        MT_LOG_ERROR("lwIP", "Failed to create thread %s: %d", name, ret);
        return SYS_THREAD_NULL;
    }
    return (sys_thread_t)task_handle;
}

uint32_t sys_arch_sem_wait(sys_sem_t* sem, uint32_t timeout)
{
    if (sem == NULL || *sem == NULL)
        return SYS_ARCH_TIMEOUT;

    uint32_t wait_ms = (timeout == 0) ? MINI_WAIT_FOREVER : timeout;
    uint32_t start_ticks = mini_time_ms();

    if (mini_sem_wait(*sem, wait_ms) == MINI_OK)
    {
        uint32_t elapsed_ticks = mini_time_ms() - start_ticks;
        if (timeout > 0 && elapsed_ticks > timeout)
            return timeout;
        return elapsed_ticks;
    }
    else
        return SYS_ARCH_TIMEOUT;
}

uint32_t sys_arch_mbox_fetch(sys_mbox_t* mbox, void** msg, uint32_t timeout)
{
    if (mbox == NULL || *mbox == NULL)
        return SYS_ARCH_TIMEOUT;

    void*  dummy_msg = NULL;
    void** msg_ptr = (msg != NULL) ? msg : &dummy_msg;

    uint32_t wait_ms = (timeout == 0) ? MINI_WAIT_FOREVER : timeout;
    uint32_t start_ticks = mini_time_ms();

    /* 返回 bool; 原先与 MINI_OK(=0) 比较导致本函数永远返回 SYS_ARCH_TIMEOUT */
    if (mini_queue_receive(*mbox, msg_ptr, wait_ms))
    {
        uint32_t elapsed_ticks = mini_time_ms() - start_ticks;
        if (timeout > 0 && elapsed_ticks > timeout)
            return timeout;
        return elapsed_ticks;
    }
    else
        return SYS_ARCH_TIMEOUT;
}

uint32_t sys_arch_mbox_tryfetch(sys_mbox_t* mbox, void** msg)
{
    if (mbox == NULL || *mbox == NULL)
        return SYS_MBOX_EMPTY;

    void*  dummy_msg = NULL;
    void** msg_ptr = (msg != NULL) ? msg : &dummy_msg;

    /* 返回 bool; 原先与 MINI_OK(=0) 比较导致本函数永远返回 SYS_MBOX_EMPTY */
    if (mini_queue_receive(*mbox, msg_ptr, 0))
        return 0;
    else
        return SYS_MBOX_EMPTY;
}

#endif /* NO_SYS == 0 */
