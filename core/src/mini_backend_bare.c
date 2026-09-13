/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file mini_backend_bare.c
 *@brief 裸机后端实现 (互斥锁 + 内存三函数 + ISR 出口)
 *@author H-000-H
 *@details
 *           - 互斥锁: 忙等锁, 含 CONFIG_CPU_CORES>1 (AMP) 双分支;
 *           - 队列:  fifo_spsc 静态池, EventBus 依赖它;
 *           - 内存三函数: 转发 libc 堆 (CONFIG_OS_BARE_MINI_OS_MEM 时转发 mini-os 内存模块);
 *           - 任务: 符号保留, 调用返回 MINI_ERR_NOTSUPP (裸机任务由 xtask 承担);
 *           - 调度器冻结 / ISR 出口: 关中断与空实现。
 *         刻意**不提供**信号量: 裸机下无调用点 (lwIP 的 NO_SYS=0 路径被 Kconfig
 *         关闭), 误用应在链接期报错。
 *         互斥锁用忙等而不是阻塞: 裸机没有可阻塞切换的线程语义。适用边界是
 *         "锁内不存在让出点" —— xtask 无论 COOP 还是 PREEMPT 都是回调
 *         run-to-completion, 持锁方必定跑完; OS 后端必须改用内核互斥锁。
 */

#if defined(CONFIG_OS_BARE)

#define ALLOW_HEAP_ALLOC /* 内存三函数默认转发 libc 堆 (开关开启时转发 mini-os 堆), 需豁免 poison 层 */

#include "mini_backend.h"

#include "buffer.h"
#include "compiler_compat.h"
#include "hal_amp.h"
#include "mini_critical.h"
#include "mini_slot.h"
#include "mini_time.h"
#include "status.h"
#include <stdlib.h>

#ifdef CONFIG_OS_BARE_MINI_OS_MEM
#include "err.h"    /* MINI_OS_OK */
#include "memory.h" /* mini-os 内存模块 (mini_malloc/calloc/free 的转发目标) */
#endif

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* AMP (CPU_CORES > 1) 编译期守卫                                              */
/* -------------------------------------------------------------------------- */
/* AMP 下互斥锁依赖跨核原子 CAS。ARMv6 (M0/M0+) 与无 A 扩展的 RISC-V 上,
 * compiler_compat.h 把 MINI_ATOMIC_CAS 降级为「关中断 + 读改写」
 * (MINI_ATOMIC_IRQ_SOFT_ATOMIC=1), 只对本核原子 —— 跨核互斥锁没有硬件保证。
 * 这是编译期可判定的事实, 故直接编译失败而不是留到现场; 板级确知不存在跨核
 * 共享锁时, 定义 MINI_AMP_NO_ATOMIC_OK 显式豁免。 */
#if (CONFIG_CPU_CORES > 1) && defined(MINI_ATOMIC_IRQ_SOFT_ATOMIC) && MINI_ATOMIC_IRQ_SOFT_ATOMIC && !defined(MINI_AMP_NO_ATOMIC_OK)
#error "CPU_CORES>1 (AMP): target has no inline atomic RMW (MINI_ATOMIC_IRQ_SOFT_ATOMIC=1), so the bare-metal mutex would only be core-local. Define MINI_AMP_NO_ATOMIC_OK only if no lock is shared across cores."
#endif

/* -------------------------------------------------------------------------- */
/* 互斥锁 (忙等: 关中断保护状态 + 原子 CAS 的 AMP 变体)                        */
/* -------------------------------------------------------------------------- */
typedef enum
{
    MINI_MUTEX_RECURSIVE = 0, /**< 可重入, 须显式 create_static_recursive */
    MINI_MUTEX_PLAIN = 1,     /**< 非递归, create_static 默认 */
} mini_mutex_type_t;

struct mini_mutex
{
    mini_mutex_type_t type;  /**< 创建期绑定, 运行期不变 */
    MINI_ATOMIC_UINT  lock;  /**< 持有标志 (0 = 空闲) */
    MINI_ATOMIC_UINT  depth; /**< 递归深度 */
};

_Static_assert(sizeof(struct mini_mutex) <= MINI_MUTEX_STORAGE_SIZE, "mini_backend_bare: MINI_MUTEX_STORAGE_SIZE too small");

static mt_err_t mini_mutex_init(struct mini_mutex* mutex, mini_mutex_type_t type)
{
    if (!mutex)
        return MINI_ERR_INVAL;
    if (type != MINI_MUTEX_RECURSIVE && type != MINI_MUTEX_PLAIN)
        return MINI_ERR_INVAL;

    mutex->type = type;
    MINI_ATOMIC_STORE(&mutex->lock, 0U, MINI_RELEASE);
    MINI_ATOMIC_STORE(&mutex->depth, 0U, MINI_RELEASE);
    return MINI_OK;
}

/* AMP: 原子 CAS + depth 递增, 支持跨核竞争; 普通: 关中断后判定, 单核下无竞争 */
static mt_err_t mini_mutex_try_acquire(struct mini_mutex* mutex)
{
#if CONFIG_CPU_CORES > 1
    uint32_t expected = 0;
    if (MINI_ATOMIC_CAS(&mutex->lock, &expected, 1, MINI_ACQUIRE, MINI_RELAXED))
    {
        MINI_ATOMIC_STORE(&mutex->depth, 1, MINI_RELAXED);
        return MINI_OK;
    }

    if (mutex->type == MINI_MUTEX_RECURSIVE)
    {
        uint32_t depth = MINI_ATOMIC_LOAD(&mutex->depth, MINI_RELAXED);
        if (depth == 0U)
            return MINI_ERR_BUSY;

        MINI_ATOMIC_STORE(&mutex->depth, depth + 1U, MINI_RELAXED);
        return MINI_OK;
    }

    return MINI_ERR_BUSY;
#else
    mini_irq_state_t irq = mini_critical_enter();
    if (mutex->lock == 0U)
    {
        mutex->lock = 1U;
        mutex->depth = 1U;
        mini_critical_exit(irq);
        return MINI_OK;
    }

    if (mutex->type == MINI_MUTEX_RECURSIVE && mutex->depth > 0U)
    {
        mutex->depth++;
        mini_critical_exit(irq);
        return MINI_OK;
    }

    mini_critical_exit(irq);
    return MINI_ERR_BUSY;
#endif
}

static mt_err_t mini_mutex_create_static_typed(mini_mutex_t** out, void* storage, size_t storage_size, mini_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct mini_mutex))
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;
    if (type != MINI_MUTEX_RECURSIVE && type != MINI_MUTEX_PLAIN)
        return MINI_ERR_INVAL;

    struct mini_mutex* mutex_obj = (struct mini_mutex*)storage;
    if (mini_mutex_init(mutex_obj, type) != MINI_OK)
        return MINI_ERR_INVAL;

    *out = (mini_mutex_t*)mutex_obj;
    return MINI_OK;
}

mt_err_t mini_mutex_create_static(mini_mutex_t** out, void* storage, size_t storage_size)
{
    return mini_mutex_create_static_typed(out, storage, storage_size, MINI_MUTEX_PLAIN);
}

mt_err_t mini_mutex_create_static_recursive(mini_mutex_t** out, void* storage, size_t storage_size)
{
    return mini_mutex_create_static_typed(out, storage, storage_size, MINI_MUTEX_RECURSIVE);
}

mt_err_t mini_mutex_lock(mini_mutex_t* mtx, uint32_t timeout_ms)
{
    if (!mtx)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR; /* 中断中不能阻塞等锁 */

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;

    if (mini_mutex_try_acquire(mutex) == MINI_OK)
        return MINI_OK;

    if (timeout_ms == 0U)
        return MINI_ERR_TIMEOUT;

    uint32_t start = 0U;
    if (timeout_ms != MINI_WAIT_FOREVER)
        start = mini_time_ms();

    for (;;)
    {
        if (mini_mutex_try_acquire(mutex) == MINI_OK)
            return MINI_OK;

        if (timeout_ms != MINI_WAIT_FOREVER && (mini_time_ms() - start) >= timeout_ms)
            return MINI_ERR_TIMEOUT;

#ifdef CONFIG_OS_BARE_WFI
        /* 省电但响应依赖中断唤醒; 裸机抢占调度器下会让系统空睡, 默认关闭 */
        mini_wfi();
#endif
    }
}

mt_err_t mini_mutex_unlock(mini_mutex_t* mtx)
{
    if (!mtx)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;

#if CONFIG_CPU_CORES > 1
    uint32_t depth = MINI_ATOMIC_LOAD(&mutex->depth, MINI_RELAXED);
    if (depth == 0U)
        return MINI_ERR_IO;

    if (depth > 1U)
    {
        MINI_ATOMIC_STORE(&mutex->depth, depth - 1U, MINI_RELAXED);
        return MINI_OK;
    }

    MINI_ATOMIC_STORE(&mutex->depth, 0U, MINI_RELAXED);
    MINI_ATOMIC_STORE(&mutex->lock, 0U, MINI_RELEASE);
    return MINI_OK;
#else
    mini_irq_state_t irq = mini_critical_enter();
    if (mutex->depth == 0U)
    {
        mini_critical_exit(irq);
        return MINI_ERR_IO;
    }

    if (mutex->depth > 1U)
    {
        mutex->depth--;
        mini_critical_exit(irq);
        return MINI_OK;
    }

    mutex->depth = 0U;
    mutex->lock = 0U;
    mini_critical_exit(irq);
    return MINI_OK;
#endif
}

void mini_mutex_destroy(mini_mutex_t* mtx)
{
    if (!mtx)
        return;
    if (hal_is_in_isr())
        return;

    /* 裸机忙等锁没有等待队列, 清状态即可 (对齐 mini-os 的 delete_static 语义) */
    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    MINI_ATOMIC_STORE(&mutex->lock, 0U, MINI_RELEASE);
    MINI_ATOMIC_STORE(&mutex->depth, 0U, MINI_RELEASE);
}

/* -------------------------------------------------------------------------- */
/* 内存 (默认转发 libc 堆, 堆区由板级 _sbrk + 链接脚本提供;                    */
/*      CONFIG_OS_BARE_MINI_OS_MEM 时转发 mini-os 内存模块, 堆区来自           */
/*      __mini_os_heap_start/__mini_os_heap_end)                               */
/* -------------------------------------------------------------------------- */
/* 本文件是全仓禁止动态分配 (compiler_compat_poison.h) 的少数豁免点之一。 */
/* 开关开启时堆本身在可嵌套关中断临界区内, ISR 内并发调用安全; 但惰性接管 */
/* (mini_os_heap_ensure_init) 不是 ISR 安全的, 首次分配须在启动/线程上下文完成。 */

void* mini_malloc(size_t size)
{
#ifdef CONFIG_OS_BARE_MINI_OS_MEM
    if (mini_os_heap_ensure_init() != MINI_OS_OK)
        return NULL; /* 堆区缺失: 链接脚本未提供 __mini_os_heap_* */
    return mini_os_malloc(size);
#else
    return malloc(size);
#endif
}

void* mini_calloc(size_t count, size_t size)
{
#ifdef CONFIG_OS_BARE_MINI_OS_MEM
    /* 首次调用惰性接管链接脚本堆区 */
    if (mini_os_heap_ensure_init() != MINI_OS_OK)
        return NULL;
    return mini_os_calloc(count, size);
#else
    return calloc(count, size);
#endif
}

mt_err_t mini_free(void* ptr)
{
#ifdef CONFIG_OS_BARE_MINI_OS_MEM
    MINI_IGNORE_RESULT(mini_os_free(ptr)); /* magic 校验, double-free 静默拒绝 */
#else
    free(ptr);
#endif
    return MINI_OK;
}

/* -------------------------------------------------------------------------- */
/* 任务 (裸机不支持: 保留符号, 运行期返回 NOTSUPP)                              */
/* -------------------------------------------------------------------------- */
/* 仓内 board/src/task_utils.c 与 system 的 task_manager 在裸机配置下也会编入, 所以
 * 符号必须存在; 语义是 NOTSUPP 桩 (返回 NOTSUPP / 空实现), 不是链接报错。
 * 裸机的"任务"是 xtask 的 x_scheduler_task_create() (周期回调模型), 与线程入口语义
 * 不通用, 故这里刻意不做转发 —— 转发会让调用方以为拿到的是一个可阻塞的线程。 */

mt_err_t mini_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, mini_task_entry_t entry, void* param, int core_id,
                            mini_task_handle_t* out_handle)
{
    MINI_UNUSED_PARAM(name);
    MINI_UNUSED_PARAM(stack_size);
    MINI_UNUSED_PARAM(priority);
    MINI_UNUSED_PARAM(entry);
    MINI_UNUSED_PARAM(param);
    MINI_UNUSED_PARAM(core_id);

    if (!out_handle)
        return MINI_ERR_INVAL;
    *out_handle = NULL;
    return MINI_ERR_NOTSUPP;
}

void mini_task_self_delete(void)
{
    /* xtask 无"自我删除"语义; 与原实现一致: 永久 WFI 占位 */
    for (;;)
        mini_wfi();
}

void mini_task_delete(mini_task_handle_t task) { MINI_UNUSED_PARAM(task); }

bool mini_task_is_running(mini_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return false;
}

const char* mini_task_get_name(mini_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return "?";
}

uint32_t mini_task_get_stack_watermark(mini_task_handle_t task)
{
    MINI_UNUSED_PARAM(task);
    return 0U;
}

/* -------------------------------------------------------------------------- */
/* 调度器冻结 (fail-fast 单向冻结)                                             */
/* -------------------------------------------------------------------------- */
/* 裸机没有"挂起调度器"这一层: 关中断后 PendSV/SysTick 都被屏蔽, 调度器本就不会再切
 * 上下文, 所以与中断冻结是同一件事。 */
void mini_sched_freeze(void) { mini_irq_disable(); }

/* -------------------------------------------------------------------------- */
/* 队列 (fifo_spsc 静态池)                                                     */
/* -------------------------------------------------------------------------- */
/* 队列族在裸机下必须是**真实现**而不是 NOTSUPP 桩: EventBus 在裸机配置下也编入,
 * 用的就是这条队列 (Kconfig OS_BARE_MAX_QUEUES: "开启 EVENT_BUS 时自动 +1")。
 * 每个队列静态内嵌一个 fifo_spsc + 元素缓冲, item_size 按 sizeof(fifo_data_type)
 * 向下整除, 多元素项用 fifo_write_block / fifo_read_block 原子块读写。 */
#ifndef MINI_BARE_MAX_QUEUES
#ifdef CONFIG_OS_BARE_MAX_QUEUES
#define MINI_BARE_MAX_QUEUES CONFIG_OS_BARE_MAX_QUEUES
#else
#define MINI_BARE_MAX_QUEUES 0
#endif
#endif

/* 基础队列数 + EventBus 自动 +1 (EventBus 需要一个队列) */
#ifdef CONFIG_EVENT_BUS
#define MINI_BARE_QUEUE_POOL_SIZE (MINI_BARE_MAX_QUEUES + 1)
#else
#define MINI_BARE_QUEUE_POOL_SIZE MINI_BARE_MAX_QUEUES
#endif

#ifndef MINI_BARE_QUEUE_BUF_SZ
#ifdef CONFIG_OS_BARE_QUEUE_BUF_SZ
#define MINI_BARE_QUEUE_BUF_SZ CONFIG_OS_BARE_QUEUE_BUF_SZ
#else
#define MINI_BARE_QUEUE_BUF_SZ 2048
#endif
#endif

#define MINI_BARE_QUEUE_ELEM_COUNT (MINI_BARE_QUEUE_BUF_SZ / sizeof(fifo_data_type))

struct mini_queue
{
    struct fifo_spsc fifo;
    fifo_data_type   buf[MINI_BARE_QUEUE_ELEM_COUNT] MINI_ALIGNED(32);
    size_t           elements_per_item;
};

#if MINI_BARE_QUEUE_POOL_SIZE > 0
static struct mini_queue s_queues[MINI_BARE_QUEUE_POOL_SIZE] MINI_ALIGNED(64);
static uint8_t            s_queue_used[MINI_BARE_QUEUE_POOL_SIZE] MINI_ALIGNED(4);
static mini_slot_t        s_queue_pool_ctrl MINI_ALIGNED(4);

mini_pre_execution(MINI_PRE_EXEC_PRIO_QUEUE_POOL) static void mini_queue_pool_boot(void)
{
    MINI_IGNORE_RESULT(mini_slot_init(&s_queue_pool_ctrl, s_queue_used, MINI_BARE_QUEUE_POOL_SIZE));
}

/* 句柄 -> 池下标 (同时校验该槽位确实被占用) */
static int mini_queue_index_of(mini_queue_t* queue)
{
    if (!queue)
        return -1;
    int idx = (int)((struct mini_queue*)queue - s_queues);
    if (idx < 0 || idx >= MINI_BARE_QUEUE_POOL_SIZE || !s_queue_used[idx])
        return -1;
    return idx;
}
#else
static int mini_queue_index_of(mini_queue_t* queue)
{
    MINI_UNUSED_PARAM(queue);
    return -1; /* 队列池未启用 (基础队列数为 0 且未开 EVENT_BUS) */
}
#endif /* MINI_BARE_QUEUE_POOL_SIZE > 0 */

mini_queue_t* mini_queue_create(size_t queue_len, size_t item_size)
{
    if (queue_len == 0 || item_size == 0)
        return NULL;

    /* 底层 fifo_spsc 的 size 必须是 2 的幂 */
    if ((queue_len & (queue_len - 1)) != 0)
        MINI_TRAP();

    /* item_size 必须是 fifo_data_type 的整数倍, 否则无法按块直接读写 */
    if (item_size % sizeof(fifo_data_type) != 0)
        return NULL;

    size_t elements_per_item = item_size / sizeof(fifo_data_type);
    size_t total_elements    = queue_len * elements_per_item;
    if ((total_elements & (total_elements - 1)) != 0)
        MINI_TRAP();

    if (total_elements > MINI_BARE_QUEUE_ELEM_COUNT)
        return NULL;

#if MINI_BARE_QUEUE_POOL_SIZE > 0
    int idx = mini_slot_claim(&s_queue_pool_ctrl);
    if (idx < 0)
        return NULL;

    struct mini_queue* queue = &s_queues[idx];
    queue->elements_per_item  = elements_per_item;
    if (fifo_init(&queue->fifo, queue->buf, (uint16_t)total_elements) != BUFF_OK)
    {
        MINI_IGNORE_RESULT(mini_slot_release(&s_queue_pool_ctrl, idx));
        return NULL;
    }

    return (mini_queue_t*)queue;
#else
    MINI_UNUSED_PARAM(total_elements);
    return NULL; /* 队列池未启用: 需在 Kconfig 设基础队列数或开启 EVENT_BUS */
#endif
}

void mini_queue_delete(mini_queue_t* queue)
{
#if MINI_BARE_QUEUE_POOL_SIZE > 0
    int idx = mini_queue_index_of(queue);
    if (idx < 0)
        return;
    MINI_IGNORE_RESULT(mini_slot_release(&s_queue_pool_ctrl, idx));
#else
    MINI_UNUSED_PARAM(queue);
#endif
}

/* SPSC 安全: 消费者只释放空间不缩减, 故容量检查通过后写入必然完整 */
static bool mini_queue_send_internal(mini_queue_t* queue, const void* item)
{
#if MINI_BARE_QUEUE_POOL_SIZE > 0
    int idx = mini_queue_index_of(queue);
    if (idx < 0 || !item)
        return false;

    struct mini_queue* obj     = &s_queues[idx];
    uint16_t           epi     = (uint16_t)obj->elements_per_item;
    uint16_t           count   = 0;
    uint16_t           written = 0;

    MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
    if ((uint16_t)(obj->fifo.size - count) < epi)
        return false;

    if (fifo_write_block(&obj->fifo, (const fifo_data_type*)item, epi, &written) != BUFF_OK)
        return false;
    return written == epi;
#else
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
#endif
}

bool mini_queue_send(mini_queue_t* queue, const void* item, uint32_t timeout_ms)
{
    MINI_UNUSED_PARAM(timeout_ms); /* 裸机入队非阻塞 */
    if (hal_is_in_isr())
        return false;

    return mini_queue_send_internal(queue, item);
}

bool mini_queue_send_from_isr(mini_queue_t* queue, const void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required); /* 裸机不在中断内切换上下文 */
    return mini_queue_send_internal(queue, item);
}

bool mini_queue_receive(mini_queue_t* queue, void* item, uint32_t timeout_ms)
{
#if MINI_BARE_QUEUE_POOL_SIZE > 0
    if (hal_is_in_isr())
        return false;

    int idx = mini_queue_index_of(queue);
    if (idx < 0 || !item)
        return false;

    struct mini_queue* obj   = &s_queues[idx];
    uint16_t           epi   = (uint16_t)obj->elements_per_item;
    uint16_t           count = 0;
    uint16_t           rd    = 0;

    if (timeout_ms == MINI_WAIT_FOREVER)
    {
        MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
        while (count < epi)
        {
#ifdef CONFIG_OS_BARE_WFI
            mini_wfi();
#endif
            MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
        }
    }
    else if (timeout_ms > 0)
    {
        uint32_t start = mini_time_ms();
        MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
        while (count < epi)
        {
            if ((mini_time_ms() - start) >= timeout_ms)
                return false;
#ifdef CONFIG_OS_BARE_WFI
            mini_wfi();
#endif
            MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
        }
    }

    MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
    if (count < epi)
        return false;

    if (fifo_read_block(&obj->fifo, (fifo_data_type*)item, epi, &rd) != BUFF_OK)
        return false;
    return rd == epi;
#else
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    MINI_UNUSED_PARAM(timeout_ms);
    return false;
#endif
}

bool mini_queue_receive_from_isr(mini_queue_t* queue, void* item, bool* px_yield_required)
{
    MINI_UNUSED_PARAM(px_yield_required);

#if MINI_BARE_QUEUE_POOL_SIZE > 0
    int idx = mini_queue_index_of(queue);
    if (idx < 0 || !item)
        return false;

    struct mini_queue* obj   = &s_queues[idx];
    uint16_t           epi   = (uint16_t)obj->elements_per_item;
    uint16_t           count = 0;
    uint16_t           rd    = 0;

    MINI_IGNORE_RESULT(fifo_get_count(&obj->fifo, &count));
    if (count < epi)
        return false;

    if (fifo_read_block(&obj->fifo, (fifo_data_type*)item, epi, &rd) != BUFF_OK)
        return false;
    return rd == epi;
#else
    MINI_UNUSED_PARAM(queue);
    MINI_UNUSED_PARAM(item);
    return false;
#endif
}

/* -------------------------------------------------------------------------- */
/* ISR 出口上下文切换 (裸机空实现)                                             */
/* -------------------------------------------------------------------------- */
/* xtask 的 SysTick ISR 只累加 tick 并唤醒到期任务, 不在中断内切换上下文, 故无需
 * yield; 保留符号是为了让 _from_isr 系列的回传标志有统一消费点, 后端间接口对称。 */
void mini_yield_from_isr(bool yield_required) { MINI_IGNORE_RESULT(yield_required); }

#endif /* CONFIG_OS_BARE */
