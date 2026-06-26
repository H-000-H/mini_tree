#ifdef CONFIG_OSAL_RTTHREAD

#define ALLOW_STDIO_OUTPUT

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"

#include <rtthread.h>
#include <rthw.h>

#include <stdarg.h>
#include <stdlib.h>
#include "compiler_compat_poison.h"

/*
 * 最小堆大小 — 用户工程可在 board_config.h 中用 RTT_HEAP_SIZE 覆盖.
 * 实际线程栈、IPC 对象等内存从此堆分配.
 */
#ifndef RTT_HEAP_SIZE
#define RTT_HEAP_SIZE  (32 * 1024)
#endif

static uint8_t s_rtt_heap[RTT_HEAP_SIZE] COMPAT_ALIGNED(4);
static volatile int s_rtt_heap_inited = 0;

/* 确保 RT-Thread 系统堆在第一次调用前完成初始化 */
static void rtt_heap_init_once(void)
{
    if (!s_rtt_heap_inited)
    {
        rt_system_heap_init(s_rtt_heap, s_rtt_heap + sizeof(s_rtt_heap));
        s_rtt_heap_inited = 1;
    }
}

/* ── 互斥锁内部存储 ── */
struct osal_mutex
{
    osal_mutex_type_t type;
    union
    {
        struct rt_mutex     mutex;
        struct rt_semaphore sem;
    } u;
};

static int osal_mutex_init(struct osal_mutex* m, osal_mutex_type_t type, const char* name)
{
    if (!m) return -1;

    m->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_init(&m->u.mutex, name, RT_IPC_FLAG_PRIO) == RT_EOK ? 0 : -1;
    if (type == OSAL_MUTEX_PLAIN)
        return rt_sem_init(&m->u.sem, name, 1, RT_IPC_FLAG_PRIO) == RT_EOK ? 0 : -1;
    return -1;
}

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "OSAL_MUTEX_STORAGE_SIZE too small");

/* ── ISR 上下文检测 ── */
int osal_in_isr(void)
{
    return rt_interrupt_get_nest() > 0;
}

/* ── Spinlock: 关中断临界区 ── */
struct osal_spinlock
{
    rt_base_t level;
};

void osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return;
    ((struct osal_spinlock*)lock)->level = 0;
}

void osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return;
    ((struct osal_spinlock*)lock)->level = rt_hw_interrupt_disable();
}

void osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return;
    rt_base_t level = ((struct osal_spinlock*)lock)->level;
    ((struct osal_spinlock*)lock)->level = 0;
    rt_hw_interrupt_enable(level);
}

/* ── 静态互斥锁池 ── */
static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t           s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t       s_mutex_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(150)
static void osal_mutex_pool_boot_init(void)
{
    osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE);
}

int osal_pool_init(osal_pool_t* pool, volatile uint8_t* buffer, size_t count)
{
    if (!pool || !buffer || count == 0)
        return -1;

    pool->used_slots = buffer;
    pool->slot_count = count;

    for (size_t i = 0; i < count; i++)
        buffer[i] = 0;

    return 0;
}

int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return -1;

    uint32_t rand_val = COMPAT_RAND(0x43U, 0x32U, 0x43U, 0x32U);
    size_t start_idx = rand_val % pool->slot_count;

    int claimed_index = -1;
    rt_base_t level = rt_hw_interrupt_disable();
    for (size_t i = 0; i < pool->slot_count; i++)
    {
        size_t cur = (start_idx + i) % pool->slot_count;
        if (!pool->used_slots[cur])
        {
            pool->used_slots[cur] = 1;
            claimed_index = (int)cur;
            break;
        }
    }
    rt_hw_interrupt_enable(level);
    return claimed_index;
}

void osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 ||
        (size_t)slot_index >= pool->slot_count)
        return;

    rt_base_t level = rt_hw_interrupt_disable();
    pool->used_slots[slot_index] = 0;
    rt_hw_interrupt_enable(level);
}

/* ── 时间 ── */
uint32_t osal_time_ms(void)
{
    return rt_tick_get() * 1000 / RT_TICK_PER_SECOND;
}

void osal_delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return rt_tick_from_millisecond(ms);
}

osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return RT_WAITING_FOREVER;
    return rt_tick_from_millisecond(timeout_ms);
}

/* ── 内存 ── */
void* osal_calloc(size_t count, size_t size)
{
    rtt_heap_init_once();
    return rt_calloc(count, size);
}

void osal_free(void* ptr)
{
    rt_free(ptr);
}

/* ── 互斥锁 ── */
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out) return -1;
    if (osal_in_isr()) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;
    *out = NULL;

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0) return -1;

    struct osal_mutex* m = &s_mutex_pool[index];
    if (osal_mutex_init(m, type, "osal_mtx") != 0)
    {
        osal_pool_release(&s_mutex_pool_ctrl, index);
        return -1;
    }
    *out = (struct osal_mutex*)m;
    return 0;
}

int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,
                                 size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) return -1;
    if (osal_in_isr()) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;
    *out = NULL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type, "osal_static") != 0)
        return -1;

    *out = (struct osal_mutex*)m;
    return 0;
}

int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex) return;
    if (osal_in_isr()) return;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    if (m->type == OSAL_MUTEX_RECURSIVE)
        rt_mutex_detach(&m->u.mutex);
    else
        rt_sem_detach(&m->u.sem);

    for (int i = 0; i < OSAL_MUTEX_POOL_SIZE; i++)
    {
        if (&s_mutex_pool[i] == m)
        {
            osal_pool_release(&s_mutex_pool_ctrl, i);
            break;
        }
    }
}

int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex) return -1;
    if (osal_in_isr()) return -1;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    if (m->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_take(&m->u.mutex, ticks) == RT_EOK ? 0 : -1;
    return rt_sem_take(&m->u.sem, ticks) == RT_EOK ? 0 : -1;
}

int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex) return -1;
    if (osal_in_isr()) return -1;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    if (m->type == OSAL_MUTEX_RECURSIVE)
        return rt_mutex_release(&m->u.mutex) == RT_EOK ? 0 : -1;
    return rt_sem_release(&m->u.sem) == RT_EOK ? 0 : -1;
}

/* ── 二值信号量 ── */
struct osal_sem
{
    struct rt_semaphore sem;
    bool                from_pool;
    bool                inited;
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE,
               "OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t       s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t   s_sem_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(151)
static void osal_sem_pool_boot_init(void)
{
    osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE);
}

static int osal_sem_init_binary(struct osal_sem* sem)
{
    if (!sem)
        return -1;

    if (rt_sem_init(&sem->sem, "osal", 0, RT_IPC_FLAG_PRIO) != RT_EOK)
        return -1;

    sem->inited = true;
    return 0;
}

int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return -1;

    int idx = osal_pool_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return -1;

    struct osal_sem* sem = &s_sem_pool[idx];
    if (osal_sem_init_binary(sem) != 0)
    {
        osal_pool_release(&s_sem_pool_ctrl, idx);
        return -1;
    }

    sem->from_pool = true;
    *out = sem;
    return 0;
}

int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return -1;

    struct osal_sem* sem = (struct osal_sem*)storage;
    if (osal_sem_init_binary(sem) != 0)
        return -1;

    sem->from_pool = false;
    *out = sem;
    return 0;
}

void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem || !sem->inited)
        return;

    rt_sem_detach(&sem->sem);
    sem->inited = false;

    if (sem->from_pool)
    {
        for (size_t i = 0; i < OSAL_SEM_POOL_SIZE; i++)
        {
            if (&s_sem_pool[i] == sem)
            {
                osal_pool_release(&s_sem_pool_ctrl, (int)i);
                break;
            }
        }
    }
}

int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem || !sem->inited || osal_in_isr())
        return -1;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_sem_take(&sem->sem, ticks) == RT_EOK ? 0 : -1;
}

bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem || !sem->inited || osal_in_isr())
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    (void)px_yield_required;

    if (!sem || !sem->inited)
        return false;

    return rt_sem_release(&sem->sem) == RT_EOK;
}

void osal_yield_from_isr(bool yield_required)
{
    (void)yield_required;
}

/* ── 任务创建 (无句柄版本, 保留兼容) ── */
int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id)
{
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param,
                                          stack_size, priority, 10);
    if (!thread) return -1;

#ifdef RT_USING_SMP
    if (core_id >= 0)
    {
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
    }
#else
    (void)core_id;
#endif

    rt_thread_startup(thread);
    return 0;
}

/* ── 任务句柄 API ── */
int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle) return -1;
    rtt_heap_init_once();

    rt_thread_t thread = rt_thread_create(name, entry, param,
                                          stack_size, priority, 10);
    if (!thread) return -1;

#ifdef RT_USING_SMP
    if (core_id >= 0)
    {
        rt_thread_control(thread, RT_THREAD_CTRL_BIND_CPU, (void*)(long)core_id);
    }
#else
    (void)core_id;
#endif

    rt_thread_startup(thread);
    *out_handle = (osal_task_handle_t)thread;
    return 0;
}

void osal_task_self_delete(void)
{
    rt_thread_delete(rt_thread_self());
    rt_schedule();
}

void osal_task_delete(osal_task_handle_t task)
{
    if (!task) return;
    rt_thread_delete((rt_thread_t)task);
}

bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task) return false;
    rt_uint8_t stat = RT_SCHED_CTX((rt_thread_t)task).stat & RT_THREAD_STAT_MASK;
    return stat != RT_THREAD_CLOSE && stat != RT_THREAD_INIT;
}

const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task) return "?";
    return ((struct rt_object*)((rt_thread_t)task))->name;
}

/* RTT 使用 '#' (0x23) 填充线程栈, 从栈底扫描连续填充字节即得空闲栈大小 */
static uint32_t osal_rtt_stack_watermark(rt_thread_t thread)
{
    const uint8_t* stack = (const uint8_t*)thread->stack_addr;
    uint32_t size = thread->stack_size;
    uint32_t count = 0;
    for (uint32_t i = 0; i < size; i++)
    {
        if (stack[i] == '#')
        {
            count++;
        }
        else
        {
            break;
        }
    }
    return count; /* 剩余空闲栈 (字节) */
}

uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task) return 0;
    return osal_rtt_stack_watermark((rt_thread_t)task);
}

/* ── 队列 (基于 rt_mq 消息队列, 支持任意定长消息) ── */
#ifdef RT_USING_MESSAGEQUEUE
struct osal_queue_obj
{
    rt_mq_t mq;
    size_t   item_size;
};

osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    rtt_heap_init_once();

    struct osal_queue_obj* q = rt_malloc(sizeof(struct osal_queue_obj));
    if (!q) return NULL;

    q->mq = rt_mq_create("osmq", item_size, queue_len, RT_IPC_FLAG_PRIO);
    if (!q->mq)
    {
        rt_free(q);
        return NULL;
    }
    q->item_size = item_size;
    return (osal_queue_handle_t)q;
}

void osal_queue_delete(osal_queue_handle_t queue)
{
    if (!queue) return;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    rt_mq_delete(q->mq);
    rt_free(q);
}

bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr()) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_send_wait(q->mq, item, q->item_size, ticks) == RT_EOK;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;

    if (!queue || !item) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    return rt_mq_send(q->mq, item, q->item_size) == RT_EOK;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item || osal_in_isr()) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return rt_mq_recv(q->mq, item, q->item_size, ticks) >= 0;
}

bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}
#else
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    (void)queue_len;
    (void)item_size;
    return NULL;
}

void osal_queue_delete(osal_queue_handle_t queue)
{
    (void)queue;
}

bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return false;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return false;
}

bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    (void)px_yield_required;
    (void)queue;
    (void)item;
    return false;
}
#endif /* RT_USING_MESSAGEQUEUE */

/* ── 硬件安全关断 (weak, 板级可覆盖) ── */
COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

/* ── Panic 安全互锁 (weak, 板级可覆盖) ── */
COMPAT_WEAK void osal_panic_interlock(void)
{
}

/* ── 调度器挂起 / 中断禁用 ── */
void osal_sched_suspend(void)
{
    rt_enter_critical();
}

void osal_int_disable(void)
{
    rt_hw_interrupt_disable();
}

/* ── 日志 ── */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    (void)level;
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("[%s] ", tag ? tag : "drv");
    vprintf(fmt, args);
    my_printf_output("\n");
    va_end(args);
}

void osal_log_fatal(const char* fmt, ...)
{
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[FATAL ERROR] ");
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
{
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[CRITICAL_ASSERT FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_RTTHREAD */
