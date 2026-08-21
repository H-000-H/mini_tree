/* SPDX-License-Identifier: Apache-2.0 */
/*
 * net/sys/sys_arch.c
 * lwIP 操作系统抽象移植层实现 (mini_tree 适配)
 *
 * 双模式:
 *   NO_SYS == 0 (RTOS 后端: FreeRTOS / RT-Thread 等): 提供完整 sys_sem /
 *     sys_mutex / sys_mbox / sys_thread_new 等原语, 桥接到 OSAL。
 *   NO_SYS == 1 (裸机后端: OSAL_NULL + xtask): lwIP 自带空桩, 本文件仅提供
 *     裸机必需的 sys_init / sys_now / sys_arch_protect / sys_arch_unprotect
 *     (SYS_LIGHTWEIGHT_PROT 用关中断临界区) 与诊断输出。
 */
#include "arch/sys_arch.h"
#include "lwip/sys.h"
#include "system_log.h"
#include <stdarg.h>
#include <stdio.h>

/* 裸机临界区 API (osal_null_irq_disable/restore) 在两种模式下 protect 段均可能用到 */
#if defined(CONFIG_OSAL_NULL)
#include "osal_null.h"
#endif

#if NO_SYS == 0
#include "lwip/err.h"
#endif /* NO_SYS == 0 */

/* -------------------------------------------------------------------------- */
/* 通用: 初始化 / 时钟 / 诊断                                                 */
/* -------------------------------------------------------------------------- */
void sys_init(void)
{
    /*lwip必须tcpip第一步调用,但是系统两阶段初始化已经将 OSAL 初始化完成 此处什么都不需要做*/
}

u32_t sys_now(void)
{
    return (u32_t)osal_time_ms();
}

void lwip_diag(const char* fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SYS_LOGI("lwIP", "%s", buf);
}

/* -------------------------------------------------------------------------- */
/* 裸机 NO_SYS=1: 仅轻量临界区 (SYS_LIGHTWEIGHT_PROT)                          */
/* lwIP 自带 sys_sem/mutex/mbox/thread 空桩, 无需本文件实现。                  */
/* -------------------------------------------------------------------------- */
#if NO_SYS == 1
#if defined(CONFIG_OSAL_NULL)
sys_prot_t sys_arch_protect(void)
{
    return (sys_prot_t)osal_null_irq_disable();
}

void sys_arch_unprotect(sys_prot_t pval)
{
    osal_null_irq_restore((uint32_t)pval);
}
#endif /* CONFIG_OSAL_NULL */
#endif /* NO_SYS == 1 */

/* -------------------------------------------------------------------------- */
/* RTOS NO_SYS=0: 完整 sys_* 原语桥接到 OSAL                                   */
/* -------------------------------------------------------------------------- */
#if NO_SYS == 0

/* -------------------------------------------------------------------------- */
/* Semaphore                                                                  */
/* -------------------------------------------------------------------------- */
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    if (sem == NULL)
        return ERR_VAL;

    /* 要求初始计数 count(0 或 1) 所以直接调用二值信号量反正内容都一样*/
    if(osal_sem_create_binary(sem)!= OSAL_OK)
        return ERR_MEM;
    if(count >=1)/*count =1补一次信号量就行*/
        COMPAT_IGNORE_RESULT(osal_sem_post(*sem));
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t *sem)
{
    if (sem == NULL || *sem == NULL)
        return;
    COMPAT_IGNORE_RESULT(osal_sem_post(*sem));
}

void sys_sem_free(sys_sem_t *sem)
{
    if (sem == NULL || *sem == NULL)
        return;
    osal_sem_destroy(*sem);
    *sem = SYS_SEM_NULL;
}

/* -------------------------------------------------------------------------- */
/* Mutex                                                                      */
/* -------------------------------------------------------------------------- */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
    if (mutex == NULL)
        return ERR_VAL;
    /* osal本身默认非递归但是lwip其实递归或者不递归其实无所谓所以为了简单我直接调用默认了 */
    if (osal_mutex_create(mutex) != OSAL_OK)
        return ERR_MEM;
    return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    if (mutex == NULL || *mutex == NULL)
        return;
    COMPAT_IGNORE_RESULT(osal_mutex_lock(*mutex, OSAL_WAIT_FOREVER));
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    if (mutex == NULL || *mutex == NULL)
        return;
    COMPAT_IGNORE_RESULT(osal_mutex_unlock(*mutex));
}

void sys_mutex_free(sys_mutex_t *mutex)
{
    if (mutex == NULL || *mutex == NULL)
        return;
    osal_mutex_destroy(*mutex);
    *mutex = SYS_MUTEX_NULL;
}

/* -------------------------------------------------------------------------- */
/* Mailbox (元素为 void*, 经定长队列承载, size = sizeof(void*))                */
/* -------------------------------------------------------------------------- */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    if(mbox == NULL || size <= 0)
        return ERR_VAL;
    *mbox  = osal_queue_create(size, sizeof(void*));
    if(*mbox == SYS_MBOX_NULL)
        return ERR_MEM;
    return ERR_OK;
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    if (mbox == NULL || *mbox == NULL)
        return;
    /*简单点直接不准失败，阻塞式*/
    COMPAT_IGNORE_RESULT(osal_queue_send(*mbox, &msg, OSAL_WAIT_FOREVER));
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    if (mbox == NULL || *mbox == NULL)
        return ERR_VAL;
    return (osal_queue_send(*mbox, &msg, 0) == OSAL_OK) ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    bool yield_required = false;
    if (mbox == NULL || *mbox == NULL)
        return ERR_VAL;
    if(osal_queue_send_from_isr(*mbox, &msg, &yield_required))
    {
        osal_yield_from_isr(yield_required);/* ISR 最外层出口 */
        return ERR_OK;
    }
    else
        return ERR_MEM;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    if (mbox == NULL || *mbox == NULL)
        return;
    osal_queue_delete(*mbox);
    *mbox = SYS_MBOX_NULL;
}

/* -------------------------------------------------------------------------- */
/* Thread                                                                     */
/* -------------------------------------------------------------------------- */
/*裸机不可能线程不需要想为什么我调度器有了两个但是依然只有os的时候才有这个东西*/
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,int stacksize, int prio)
{
    osal_task_handle_t task_handle = NULL;
    int ret = osal_task_create_handle(name, stacksize, prio, thread, arg, -1, &task_handle);
    if (ret != OSAL_OK)
    {
        SYS_LOGE("lwIP", "Failed to create thread %s: %d", name, ret);
        return SYS_THREAD_NULL;
    }
    return (sys_thread_t)task_handle;
}

uint32_t sys_arch_sem_wait(sys_sem_t *sem, uint32_t timeout)
{
    if (sem == NULL || *sem == NULL)
        return SYS_ARCH_TIMEOUT;

    uint32_t osal_timeout = (timeout == 0) ? UINT32_MAX : timeout;
    uint32_t start_ticks = osal_time_ms();

    if (osal_sem_wait(*sem, osal_timeout) == OSAL_OK)
    {
        uint32_t elapsed_ticks = osal_time_ms() - start_ticks;
        if (timeout > 0 && elapsed_ticks > timeout)
            return timeout;
        return elapsed_ticks;
    }
    else
        return SYS_ARCH_TIMEOUT;
}

uint32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, uint32_t timeout)
{
    if (mbox == NULL || *mbox == NULL)
        return SYS_ARCH_TIMEOUT;

    void *dummy_msg = NULL;
    void **msg_ptr = (msg != NULL) ? msg : &dummy_msg;

    uint32_t osal_timeout = (timeout == 0) ? UINT32_MAX : timeout;
    uint32_t start_ticks = osal_time_ms();

    if (osal_queue_receive(*mbox, msg_ptr, osal_timeout) == OSAL_OK)
    {
        uint32_t elapsed_ticks = osal_time_ms() - start_ticks;
        if (timeout > 0 && elapsed_ticks > timeout)
            return timeout;
        return elapsed_ticks;
    }
    else
        return SYS_ARCH_TIMEOUT;
}

uint32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    if (mbox == NULL || *mbox == NULL)
        return SYS_MBOX_EMPTY;

    void *dummy_msg = NULL;
    void **msg_ptr = (msg != NULL) ? msg : &dummy_msg;

    if (osal_queue_receive(*mbox, msg_ptr, 0) == OSAL_OK)
        return 0;
    else
        return SYS_MBOX_EMPTY;
}

#endif /* NO_SYS == 0 */
