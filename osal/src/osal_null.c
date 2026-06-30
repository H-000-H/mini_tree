/* SPDX-License-Identifier: Apache-2.0 */
/*
 * osal_null.c — OSAL 裸机后端实现 (无 RTOS)
 *
 * 同步原语基于 C11 stdatomic 或 GCC __atomic 内建 (Kconfig OSAL_NULL_ATOMIC_*)
 * 队列采用 SPSC 环形缓冲 + 掩码, 互斥锁用 CAS + 递归深度计数
 * 任务创建固定失败, osal_delay_ms 为忙等, 适用早期移植与极简 MCU
 */
#ifdef CONFIG_OSAL_NULL

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "compiler_compat_poison.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  原子操作层 (Kconfig: OSAL_NULL_ATOMIC_*)
 * ═══════════════════════════════════════════════════════════════════════════ */

#if defined(CONFIG_OSAL_NULL_ATOMIC_STDATOMIC)
#  include <stdatomic.h>
#  if defined(__STDC_NO_ATOMICS__)
#    error "CONFIG_OSAL_NULL_ATOMIC_STDATOMIC requires C11 atomics"
#  endif
#  define OSAL_NULL_HAVE_STDATOMIC 1

#elif defined(CONFIG_OSAL_NULL_ATOMIC_GCC_BUILTIN)
#  define OSAL_NULL_HAVE_STDATOMIC 0

#else /* CONFIG_OSAL_NULL_ATOMIC_AUTO 或未生成 Kconfig 时自动探测 */
#  define OSAL_NULL_HAVE_STDATOMIC 0
#  if defined(__has_include)
#    if __has_include(<stdatomic.h>)
#      include <stdatomic.h>
#      if !defined(__STDC_NO_ATOMICS__)
#        undef OSAL_NULL_HAVE_STDATOMIC
#        define OSAL_NULL_HAVE_STDATOMIC 1
#      endif
#    endif
#  elif defined(__GNUC__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) \
        && !defined(__STDC_NO_ATOMICS__)
#    include <stdatomic.h>
#    undef OSAL_NULL_HAVE_STDATOMIC
#    define OSAL_NULL_HAVE_STDATOMIC 1
#  endif
#endif

#if OSAL_NULL_HAVE_STDATOMIC

typedef atomic_uint_least32_t osal_atomic_u32_t;
typedef atomic_int_least32_t  osal_atomic_i32_t;

#define OSAL_ATOMIC_U32_INIT(v) ATOMIC_VAR_INIT(v)
#define OSAL_ATOMIC_I32_INIT(v) ATOMIC_VAR_INIT(v)

static inline void osal_atomic_init_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    atomic_init(obj, value);
}

static inline void osal_atomic_init_i32(osal_atomic_i32_t* obj, int32_t value)
{
    atomic_init(obj, value);
}

static inline void osal_atomic_store_relaxed_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    atomic_store_explicit(obj, value, memory_order_relaxed);
}

static inline void osal_atomic_store_release_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    atomic_store_explicit(obj, value, memory_order_release);
}

static inline uint32_t osal_atomic_load_relaxed_u32(const osal_atomic_u32_t* obj)
{
    return atomic_load_explicit(obj, memory_order_relaxed);
}

static inline uint32_t osal_atomic_load_acquire_u32(const osal_atomic_u32_t* obj)
{
    return atomic_load_explicit(obj, memory_order_acquire);
}

static inline bool osal_atomic_cas_weak_acquire_u32(osal_atomic_u32_t* obj,
                                                    uint32_t* expected,
                                                    uint32_t desired)
{
    return atomic_compare_exchange_weak_explicit(obj, expected, desired,
                                                 memory_order_acquire,
                                                 memory_order_relaxed);
}

static inline void osal_atomic_fetch_add_relaxed_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    (void)atomic_fetch_add_explicit(obj, value, memory_order_relaxed);
}

static inline void osal_atomic_fetch_sub_relaxed_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    (void)atomic_fetch_sub_explicit(obj, value, memory_order_relaxed);
}

static inline void osal_atomic_fetch_add_relaxed_i32(osal_atomic_i32_t* obj, int32_t value)
{
    (void)atomic_fetch_add_explicit(obj, value, memory_order_relaxed);
}

static inline void osal_atomic_fetch_sub_relaxed_i32(osal_atomic_i32_t* obj, int32_t value)
{
    (void)atomic_fetch_sub_explicit(obj, value, memory_order_relaxed);
}

static inline int32_t osal_atomic_load_relaxed_i32(const osal_atomic_i32_t* obj)
{
    return atomic_load_explicit(obj, memory_order_relaxed);
}

#else /* GCC __atomic_* builtins */

typedef volatile uint32_t osal_atomic_u32_t;
typedef volatile int32_t  osal_atomic_i32_t;

#define OSAL_ATOMIC_U32_INIT(v) (v)
#define OSAL_ATOMIC_I32_INIT(v) (v)

static inline void osal_atomic_init_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_RELAXED);
}

static inline void osal_atomic_init_i32(osal_atomic_i32_t* obj, int32_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_RELAXED);
}

static inline void osal_atomic_store_relaxed_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_RELAXED);
}

static inline void osal_atomic_store_release_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    __atomic_store_n(obj, value, __ATOMIC_RELEASE);
}

static inline uint32_t osal_atomic_load_relaxed_u32(const osal_atomic_u32_t* obj)
{
    return __atomic_load_n(obj, __ATOMIC_RELAXED);
}

static inline uint32_t osal_atomic_load_acquire_u32(const osal_atomic_u32_t* obj)
{
    return __atomic_load_n(obj, __ATOMIC_ACQUIRE);
}

static inline bool osal_atomic_cas_weak_acquire_u32(osal_atomic_u32_t* obj,
                                                    uint32_t* expected,
                                                    uint32_t desired)
{
    return __atomic_compare_exchange_n(obj, expected, desired, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static inline void osal_atomic_fetch_add_relaxed_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    (void)__atomic_fetch_add(obj, value, __ATOMIC_RELAXED);
}

static inline void osal_atomic_fetch_sub_relaxed_u32(osal_atomic_u32_t* obj, uint32_t value)
{
    (void)__atomic_fetch_sub(obj, value, __ATOMIC_RELAXED);
}

static inline void osal_atomic_fetch_add_relaxed_i32(osal_atomic_i32_t* obj, int32_t value)
{
    (void)__atomic_fetch_add(obj, value, __ATOMIC_RELAXED);
}

static inline void osal_atomic_fetch_sub_relaxed_i32(osal_atomic_i32_t* obj, int32_t value)
{
    (void)__atomic_fetch_sub(obj, value, __ATOMIC_RELAXED);
}

static inline int32_t osal_atomic_load_relaxed_i32(const osal_atomic_i32_t* obj)
{
    return __atomic_load_n(obj, __ATOMIC_RELAXED);
}

#endif /* OSAL_NULL_HAVE_STDATOMIC */

/* ═══════════════════════════════════════════════════════════════════════════
 *  osal_null.c — 裸机适配层 (无 RTOS)
 *
 *  适用场景:
 *    1. 早期驱动移植 (先验证硬件, 后加 RTOS)
 *    2. 极简 MCU 项目 (无需多任务)
 *    3. Host 端单元测试 (配合模拟时钟)
 *
 *  约束:
 *    - 裸机无多任务; 同步原语原子后端由 Kconfig OSAL_NULL_ATOMIC_* 控制 (见本文件顶部)
 *    - 队列基于静态环形缓冲区 + 忙等
 *    - osal_delay_ms 为 CPU 忙等 (精度取决于主频)
 *    - osal_task_create 固定返回失败
 *    - osal_in_isr 依赖移植层在中断入口/出口维护 s_isr_nest
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── 内部常量 ── */
#define OSAL_NULL_MAX_QUEUES  4       /* 最大同时存在的队列数 */
#define OSAL_NULL_QUEUE_BUF_SZ 4096   /* 单队列环形缓冲区最大字节数 */

/* ── 队列内部结构 ── */
struct osal_queue_obj
{
    uint8_t*       buf;         /* 环形缓冲区 (由 create 分配) */
    size_t            item_size;
    size_t            queue_len;
    size_t            buf_mask;   /* 环形掩码 (保证为 2^n - 1) */
    osal_atomic_u32_t head;         /* 生产者写入位置 (单调递增) */
    osal_atomic_u32_t tail;         /* 消费者读取位置 (单调递增) */
    bool              used;
};

/* ── 队列控制块池 ── */
static struct osal_queue_obj s_queues[OSAL_NULL_MAX_QUEUES] COMPAT_ALIGNED(4);
static uint8_t s_queue_used[OSAL_NULL_MAX_QUEUES] COMPAT_ALIGNED(4);
static osal_pool_t s_queue_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(152)
static void osal_null_queue_pool_boot_init(void)
{
    osal_pool_init(&s_queue_pool_ctrl, s_queue_used, OSAL_NULL_MAX_QUEUES);
}

/* ── 裸机临界区: 关全局中断 (单核 ISR vs 主循环互斥) ── */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
    defined(__ARM_ARCH_8M_MAIN__)

static inline uint32_t osal_null_irq_disable(void)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask) :: "memory");
    return primask;
}

static inline void osal_null_irq_restore(uint32_t primask)
{
    __asm__ volatile("msr primask, %0" :: "r"(primask) : "memory");
}

#elif defined(__riscv)

static inline uint32_t osal_null_irq_disable(void)
{
    uintptr_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
    return (uint32_t)mstatus;
}

static inline void osal_null_irq_restore(uint32_t mstatus)
{
    __asm__ volatile("csrw mstatus, %0" :: "r"((uintptr_t)mstatus) : "memory");
}

#else

static inline uint32_t osal_null_irq_disable(void) { return 0U; }
static inline void osal_null_irq_restore(uint32_t primask) { (void)primask; }

#endif

/* ── 互斥锁 (原子 CAS + 递归深度) ── */
struct osal_mutex
{
    osal_mutex_type_t type;
    osal_atomic_u32_t lock;   /* 0=空闲, 1=已占用 */
    osal_atomic_u32_t depth;  /* 仅 OSAL_MUTEX_RECURSIVE 使用 */
};
_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "osal_null: OSAL_MUTEX_STORAGE_SIZE too small");

static int osal_mutex_init(struct osal_mutex* m, osal_mutex_type_t type)
{
    if (!m) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;

    m->type = type;
    osal_atomic_init_u32(&m->lock, 0U);
    osal_atomic_init_u32(&m->depth, 0U);
    return 0;
}

static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t           s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t       s_mutex_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(150)
static void osal_null_mutex_pool_boot_init(void)
{
    osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE);
}

/* ── 单调时钟 (由 SysTick 或定时器中断累加) ── */
static osal_atomic_u32_t s_sys_tick_ms = OSAL_ATOMIC_U32_INIT(0U);

/* ── ISR 嵌套计数: 0 = 非中断上下文 ── */
static osal_atomic_i32_t s_isr_nest = OSAL_ATOMIC_I32_INIT(0);

/* ── 池操作 ── */

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

    uint32_t irq = osal_null_irq_disable();
    int claimed = -1;
    for (size_t i = 0; i < pool->slot_count; i++)
    {
        size_t cur = (start_idx + i) % pool->slot_count;
        if (!pool->used_slots[cur])
        {
            pool->used_slots[cur] = 1;
            claimed = (int)cur;
            break;
        }
    }
    osal_null_irq_restore(irq);
    return claimed;
}

void osal_pool_release(osal_pool_t* pool, int idx)
{
    if (!pool || !pool->used_slots || idx < 0 || (size_t)idx >= pool->slot_count)
        return;

    uint32_t irq = osal_null_irq_disable();
    pool->used_slots[idx] = 0;
    osal_null_irq_restore(irq);
}

/* ── 查找队列索引 ── */
static int queue_index_of(osal_queue_handle_t q)
{
    if (!q) return -1;
    for (int i = 0; i < OSAL_NULL_MAX_QUEUES; i++)
    {
        if (s_queue_used[i] && &s_queues[i] == (struct osal_queue_obj*)q)
        {
            return i;
        }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  上下文检测
 * ═══════════════════════════════════════════════════════════════════════════ */

int osal_in_isr(void)
{
    return osal_atomic_load_relaxed_i32(&s_isr_nest) > 0;
}

/*
 * 以下两个函数由移植层在中断入口/出口调用.
 * 典型用法 (startup_xxx.s 或 C 中断处理):
 *   void SysTick_Handler(void)
 {
 *       osal_null_isr_enter();
 *       // ... 中断处理 ...
 *       osal_null_isr_exit();
 *   }
 */
void osal_null_isr_enter(void)
{
    osal_atomic_fetch_add_relaxed_i32(&s_isr_nest, 1);
}
void osal_null_isr_exit(void)
{
    osal_atomic_fetch_sub_relaxed_i32(&s_isr_nest, 1);
}

/*
 * 系统滴答中断 (默认 1 ms) 应从 SysTick_Handler 调用.
 * 移植时在中断处理中加: osal_null_systick_handler();
 */
void osal_null_systick_handler(void)
{
    osal_atomic_fetch_add_relaxed_u32(&s_sys_tick_ms, 1U);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Spinlock (裸机关中断)
 * ═══════════════════════════════════════════════════════════════════════════ */

struct osal_spinlock
{
    osal_atomic_u32_t locked;
    uint32_t          irq_saved;
    uint32_t          nest;
};

void osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return;
    osal_atomic_init_u32(&lock->locked, 0U);
    lock->irq_saved = 0U;
    lock->nest = 0U;
}

void osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return;

    if (!osal_in_isr())
    {
        uint32_t irq = osal_null_irq_disable();
        if (lock->nest == 0U)
            lock->irq_saved = irq;
        lock->nest++;
    }

    osal_atomic_store_release_u32(&lock->locked, 1U);
}

void osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return;

    osal_atomic_store_release_u32(&lock->locked, 0U);

    if (!osal_in_isr() && lock->nest > 0U)
    {
        lock->nest--;
        if (lock->nest == 0U)
            osal_null_irq_restore(lock->irq_saved);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  时间
 * ═══════════════════════════════════════════════════════════════════════════ */

uint32_t osal_time_ms(void)
{
    return osal_atomic_load_relaxed_u32(&s_sys_tick_ms);
}

void osal_delay_ms(uint32_t ms)
{
    if (ms == 0) return;
    uint32_t start = osal_time_ms();
    while ((osal_time_ms() - start) < ms)
    {
        /* 忙等 — 移植者可在循环中插入 WFI 以降低功耗 */
    }
}

osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return ms;  /* 裸机默认 1 tick = 1 ms */
}

osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return UINT32_MAX;
    return timeout_ms;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  内存
 * ═══════════════════════════════════════════════════════════════════════════ */

void* osal_calloc(size_t count, size_t size)
{
    void* ptr = calloc(count, size);
    if (!ptr)
    {
        ptr = NULL;
    }
    return ptr;
}

void osal_free(void* ptr)
{
    if (ptr)
    {
        free(ptr);
        ptr = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  互斥锁 (原子 CAS; 递归锁用 depth 计数)
 * ═══════════════════════════════════════════════════════════════════════════ */

int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out) return -1;
    if (osal_in_isr()) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;
    *out = NULL;

    int idx = osal_pool_claim(&s_mutex_pool_ctrl);
    if (idx < 0) return -1;

    if (osal_mutex_init(&s_mutex_pool[idx], type) != 0)
    {
        osal_pool_release(&s_mutex_pool_ctrl, idx);
        return -1;
    }
    *out = (struct osal_mutex*)&s_mutex_pool[idx];
    return 0;
}

int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,
                                 size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) return -1;
    if (osal_in_isr()) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type) != 0) return -1;
    *out = m;
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

    osal_atomic_store_relaxed_u32(&mutex->lock, 0U);
    osal_atomic_store_relaxed_u32(&mutex->depth, 0U);

    for (int i = 0; i < OSAL_MUTEX_POOL_SIZE; i++)
    {
        if (&s_mutex_pool[i] == (struct osal_mutex*)mutex)
        {
            osal_pool_release(&s_mutex_pool_ctrl, i);
            break;
        }
    }
}

static int osal_mutex_try_acquire(struct osal_mutex* mutex)
{
    uint32_t expected = 0U;
    if (osal_atomic_cas_weak_acquire_u32(&mutex->lock, &expected, 1U))
    {
        osal_atomic_store_relaxed_u32(&mutex->depth, 1U);
        return 0;
    }

    if (mutex->type == OSAL_MUTEX_RECURSIVE)
    {
        uint32_t depth = osal_atomic_load_relaxed_u32(&mutex->depth);
        if (depth > 0U)
        {
            osal_atomic_store_relaxed_u32(&mutex->depth, depth + 1U);
            return 0;
        }
    }

    return -1;
}

int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex) return -1;
    if (osal_in_isr()) return -1;

    if (osal_mutex_try_acquire(mutex) == 0)
        return 0;

    if (timeout_ms == 0U)
        return -1;

    uint32_t start = 0U;
    if (timeout_ms != OSAL_WAIT_FOREVER)
        start = osal_time_ms();

    for (;;)
    {
        if (osal_mutex_try_acquire(mutex) == 0)
            return 0;

        if (timeout_ms != OSAL_WAIT_FOREVER &&
            (osal_time_ms() - start) >= timeout_ms)
            return -1;
    }
}

int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex) return -1;
    if (osal_in_isr()) return -1;

    uint32_t depth = osal_atomic_load_relaxed_u32(&mutex->depth);
    if (depth == 0U)
        return -1;

    if (depth > 1U)
    {
        osal_atomic_store_relaxed_u32(&mutex->depth, depth - 1U);
        return 0;
    }

    osal_atomic_store_relaxed_u32(&mutex->depth, 0U);
    osal_atomic_store_release_u32(&mutex->lock, 0U);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  任务 (裸机不支持多任务, 创建固定失败)
 * ═══════════════════════════════════════════════════════════════════════════ */

int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id)
{
    (void)name; (void)stack_size; (void)priority;
    (void)entry; (void)param; (void)core_id;
    return -1;  /* 裸机不能创建任务 */
}

int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle) return -1;
    (void)name; (void)stack_size; (void)priority;
    (void)entry; (void)param; (void)core_id;
    *out_handle = NULL;
    return -1;
}

void osal_task_self_delete(void)
{
    /* 裸机无任务, 死循环 */
    while (1)
    { ; }
}

void osal_task_delete(osal_task_handle_t task)
{
    (void)task;
}

bool osal_task_is_running(osal_task_handle_t task)
{
    (void)task;
    return false;
}

const char* osal_task_get_name(osal_task_handle_t task)
{
    (void)task;
    return "baremetal";
}

uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    (void)task;
    return 0;
}

/* ── 二值信号量 (原子 CAS, ISR/主循环安全) ── */
struct osal_sem
{
    osal_atomic_u32_t signaled;  /* 0=未触发, 1=已触发 */
    bool              from_pool;
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE,
               "OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t       s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t   s_sem_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(151)
static void osal_null_sem_pool_boot_init(void)
{
    osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE);
}

int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return -1;

    int idx = osal_pool_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return -1;

    struct osal_sem* sem = &s_sem_pool[idx];
    osal_atomic_init_u32(&sem->signaled, 0U);
    sem->from_pool = true;
    *out = sem;
    return 0;
}

int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return -1;

    struct osal_sem* sem = (struct osal_sem*)storage;
    osal_atomic_init_u32(&sem->signaled, 0U);
    sem->from_pool = false;
    *out = sem;
    return 0;
}

void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem)
        return;

    osal_atomic_store_relaxed_u32(&sem->signaled, 0U);
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

static int osal_sem_try_wait(struct osal_sem* sem)
{
    uint32_t expected = 1U;
    if (osal_atomic_cas_weak_acquire_u32(&sem->signaled, &expected, 0U))
        return 0;
    return -1;
}

int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem)
        return -1;

    if (osal_sem_try_wait(sem) == 0)
        return 0;

    if (timeout_ms == 0U)
        return -1;

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (osal_sem_try_wait(sem) != 0)
        {
            /* 裸机忙等 */
        }
        return 0;
    }

    uint32_t start = osal_time_ms();
    while (osal_sem_try_wait(sem) != 0)
    {
        if ((osal_time_ms() - start) >= timeout_ms)
            return -1;
    }
    return 0;
}

bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem)
        return false;
    osal_atomic_store_release_u32(&sem->signaled, 1U);
    return true;
}

bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    (void)px_yield_required;
    return osal_sem_post(sem);
}

void osal_yield_from_isr(bool yield_required)
{
    (void)yield_required;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  队列 (基于环形缓冲区, ISR 安全)
 *
 *  环形缓冲区使用 2^n 大小 + 掩码, 避免取模.
 *  队列满时 osal_queue_send 返回 false, 无阻塞写入.
 *  队列空时 osal_queue_receive 支持 OSAL_WAIT_FOREVER 忙等.
 * ═══════════════════════════════════════════════════════════════════════════ */

osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    size_t needed = item_size * queue_len;
    if (needed == 0 || needed > OSAL_NULL_QUEUE_BUF_SZ) return NULL;

    int idx = osal_pool_claim(&s_queue_pool_ctrl);
    if (idx < 0) return NULL;

    struct osal_queue_obj* q = &s_queues[idx];

    /* 分配环形缓冲区 */
    q->buf = (uint8_t*)osal_calloc(1, needed);
    if (!q->buf)
    {
        osal_pool_release(&s_queue_pool_ctrl, idx);
        return NULL;
    }

    q->item_size = item_size;
    q->queue_len = queue_len;
    q->buf_mask  = queue_len - 1;  /* 调用方应保证 queue_len 为 2^n */
    osal_atomic_init_u32(&q->head, 0U);
    osal_atomic_init_u32(&q->tail, 0U);
    q->used = true;

    return (osal_queue_handle_t)q;
}

void osal_queue_delete(osal_queue_handle_t queue)
{
    int idx = queue_index_of(queue);
    if (idx < 0) return;

    struct osal_queue_obj* q = &s_queues[idx];
    if (q->buf)
    {
        osal_free(q->buf);
        q->buf = NULL;
    }
    q->used = false;
    osal_pool_release(&s_queue_pool_ctrl, idx);
}

bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (osal_in_isr())
        return false;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;
    uint32_t head    = osal_atomic_load_relaxed_u32(&q->head);
    uint32_t tail    = osal_atomic_load_acquire_u32(&q->tail);

    /* 判满 */
    if ((head - tail) >= q->queue_len) return false;

    __builtin_memcpy(&q->buf[(head & mask) * item_size], item, item_size);
    osal_atomic_store_release_u32(&q->head, head + 1U);

    return true;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;
    uint32_t head    = osal_atomic_load_relaxed_u32(&q->head);
    uint32_t tail    = osal_atomic_load_acquire_u32(&q->tail);

    if ((head - tail) >= q->queue_len) return false;

    __builtin_memcpy(&q->buf[(head & mask) * item_size], item, item_size);
    osal_atomic_store_release_u32(&q->head, head + 1U);

    return true;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;

    /* 等待直到有数据 */
    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (osal_atomic_load_acquire_u32(&q->head) ==
               osal_atomic_load_relaxed_u32(&q->tail))
        {
            /* 忙等 — 无 RTOS 无法阻塞 */
        }
    } else if (timeout_ms > 0)
    {
        uint32_t start = osal_time_ms();
        while (osal_atomic_load_acquire_u32(&q->head) ==
               osal_atomic_load_relaxed_u32(&q->tail))
        {
            if ((osal_time_ms() - start) >= timeout_ms) return false;
        }
    }

    uint32_t tail = osal_atomic_load_relaxed_u32(&q->tail);
    if (osal_atomic_load_acquire_u32(&q->head) == tail) return false;  /* 超时后仍空 */

    __builtin_memcpy(item, &q->buf[(tail & mask) * item_size], item_size);
    osal_atomic_store_release_u32(&q->tail, tail + 1U);

    return true;
}

bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    (void)px_yield_required;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;
    uint32_t tail    = osal_atomic_load_relaxed_u32(&q->tail);

    if (osal_atomic_load_acquire_u32(&q->head) == tail) return false;

    __builtin_memcpy(item, &q->buf[(tail & mask) * item_size], item_size);
    osal_atomic_store_release_u32(&q->tail, tail + 1U);

    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  硬件安全关断 & 日志 (与其他后端相同)
 * ═══════════════════════════════════════════════════════════════════════════ */

COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

COMPAT_WEAK void osal_panic_interlock(void)
{
}

/* ── 调度器挂起 / 中断禁用 ── */
void osal_sched_suspend(void)
{
    (void)osal_null_irq_disable();
}

void osal_int_disable(void)
{
    (void)osal_null_irq_disable();
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

#endif /* CONFIG_OSAL_NULL */
