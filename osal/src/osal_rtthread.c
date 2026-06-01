#ifdef CONFIG_OSAL_RTTHREAD

#include "config.h"
#include "osal.h"
#include "board_config.h"

#include <rtthread.h>
#include <rthw.h>

#include <stdarg.h>
#include <stdlib.h>

/*
 * 最小堆大小 — 宿主工程可在 board_config.h 中用 RTT_HEAP_SIZE 覆盖.
 * 实际线程栈、IPC 对象等内存从此堆分配.
 */
#ifndef RTT_HEAP_SIZE
#define RTT_HEAP_SIZE  (32 * 1024)
#endif

static uint8_t s_rtt_heap[RTT_HEAP_SIZE];
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
    struct rt_mutex mutex;
};

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

void osal_spinlock_init(osal_spinlock_t* lock)
{
    if (!lock) return;
    ((struct osal_spinlock*)lock)->level = 0;
}

void osal_spinlock_lock(osal_spinlock_t* lock)
{
    if (!lock) return;
    ((struct osal_spinlock*)lock)->level = rt_hw_interrupt_disable();
}

void osal_spinlock_unlock(osal_spinlock_t* lock)
{
    if (!lock) return;
    rt_base_t level = ((struct osal_spinlock*)lock)->level;
    ((struct osal_spinlock*)lock)->level = 0;
    rt_hw_interrupt_enable(level);
}

/* ── 静态互斥锁池 ── */
static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE];
static uint8_t s_mutex_used[OSAL_MUTEX_POOL_SIZE];

int osal_pool_claim(volatile uint8_t* used_slots, size_t slot_count)
{
    if (!used_slots || slot_count == 0) return -1;

    int claimed_index = -1;
    rt_base_t level = rt_hw_interrupt_disable();
    for (size_t i = 0; i < slot_count; i++)
    {
        if (!used_slots[i])
        {
            used_slots[i] = 1;
            claimed_index = (int)i;
            break;
        }
    }
    rt_hw_interrupt_enable(level);
    return claimed_index;
}

void osal_pool_release(volatile uint8_t* used_slots, size_t slot_count, int slot_index)
{
    if (!used_slots || slot_index < 0 || (size_t)slot_index >= slot_count) return;
    rt_base_t level = rt_hw_interrupt_disable();
    used_slots[slot_index] = 0;
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

uint32_t osal_ticks_from_ms(uint32_t ms)
{
    return rt_tick_from_millisecond(ms);
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
int osal_mutex_create(osal_mutex_t** out)
{
    if (!out) return -1;
    *out = NULL;

    int index = osal_pool_claim(s_mutex_used, OSAL_MUTEX_POOL_SIZE);
    if (index < 0) return -1;

    struct osal_mutex* m = &s_mutex_pool[index];
    if (rt_mutex_init(&m->mutex, "osal_mtx", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        osal_pool_release(s_mutex_used, OSAL_MUTEX_POOL_SIZE, index);
        return -1;
    }
    *out = (osal_mutex_t*)m;
    return 0;
}

int osal_mutex_create_static(osal_mutex_t** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) return -1;
    *out = NULL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (rt_mutex_init(&m->mutex, "osal_static", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        return -1;
    }
    *out = (osal_mutex_t*)m;
    return 0;
}

void osal_mutex_destroy(osal_mutex_t* mutex)
{
    if (!mutex) return;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    rt_mutex_detach(&m->mutex);

    for (int i = 0; i < OSAL_MUTEX_POOL_SIZE; i++)
    {
        if (&s_mutex_pool[i] == m)
        {
            osal_pool_release(s_mutex_used, OSAL_MUTEX_POOL_SIZE, i);
            break;
        }
    }
}

int osal_mutex_lock(osal_mutex_t* mutex, uint32_t timeout_ms)
{
    if (!mutex) return -1;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    rt_tick_t ticks = (timeout_ms == OSAL_WAIT_FOREVER)
        ? RT_WAITING_FOREVER
        : rt_tick_from_millisecond(timeout_ms);
    return rt_mutex_take(&m->mutex, ticks) == RT_EOK ? 0 : -1;
}

int osal_mutex_unlock(osal_mutex_t* mutex)
{
    if (!mutex) return -1;
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    return rt_mutex_release(&m->mutex) == RT_EOK ? 0 : -1;
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
    rt_uint8_t stat = ((rt_thread_t)task)->stat & RT_THREAD_STAT_MASK;
    return stat != RT_THREAD_CLOSE && stat != RT_THREAD_INIT;
}

const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task) return "?";
    return ((rt_thread_t)task)->name;
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
    if (!queue || !item) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    rt_tick_t ticks = (timeout_ms == OSAL_WAIT_FOREVER)
        ? RT_WAITING_FOREVER
        : rt_tick_from_millisecond(timeout_ms);
    return rt_mq_send_wait(q->mq, item, q->item_size, ticks) == RT_EOK;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item)
{
    if (!queue || !item) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    return rt_mq_send(q->mq, item, q->item_size) == RT_EOK;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item) return false;
    struct osal_queue_obj* q = (struct osal_queue_obj*)queue;
    rt_tick_t ticks = (timeout_ms == OSAL_WAIT_FOREVER)
        ? RT_WAITING_FOREVER
        : rt_tick_from_millisecond(timeout_ms);
    /* rt_mq_recv 返回接收字节数 (>0) 成功, 负值表示超时/错误 */
    return rt_mq_recv(q->mq, item, q->item_size, ticks) >= 0;
}

/* ── 硬件安全关断 (weak, 板级可覆盖) ── */
__attribute__((weak)) void safety_hardware_shutdown(void)
{
    __builtin_trap();
}

/* ── Panic 安全互锁 (weak, 板级可覆盖) ── */
__attribute__((weak)) void osal_panic_interlock(void)
{
}

/* ── 日志 ── */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    (void)level;
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    printf("[%s] ", tag ? tag : "drv");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_RTTHREAD */