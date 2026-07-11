/* SPDX-License-Identifier: Apache-2.0 */
/*
 * osal_freertos.c — OSAL FreeRTOS 后端实现
 *
 * 将 OSAL API 映射到 xSemaphore/xQueue/xTaskCreate 等 FreeRTOS 原语
 * 静态互斥锁/信号量池 + 槽位池 (osal_pool), ISR 检测按 ARM/RISC-V 架构分支
 * ESP32 平台额外嵌入 portMUX 适配 taskENTER_CRITICAL_ISR 路径而且esp32不要直接移植freertos他自带了freertos
 */
#ifdef  CONFIG_OSAL_FREERTOS

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"
#include "VFS.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#else
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include "compiler_compat_poison.h"

/* ── 队列 / 信号量内部存储 ── */
struct osal_mutex
{
    SemaphoreHandle_t   handle;
    StaticSemaphore_t   sem_buf;
    osal_mutex_type_t   type;
};

/**
 * @brief 初始化互斥锁
 * @param mutex 互斥锁指针
 * @param type 互斥锁类型
 * @return 结果
 */
static int osal_mutex_init(struct osal_mutex* mutex, osal_mutex_type_t type)
{
    if (!mutex) 
        return OSAL_ERR_INVAL;

    mutex->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
        mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&mutex->sem_buf);
    else if (type == OSAL_MUTEX_PLAIN)
        mutex->handle = xSemaphoreCreateMutexStatic(&mutex->sem_buf);
    else
        return OSAL_ERR_INVAL;

    return mutex->handle ? OSAL_OK : OSAL_ERR_NOMEM;
}

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,"OSAL_MUTEX_STORAGE_SIZE too small");

/* ── ISR 上下文检测 (平台适配层) ──
 * ARMv7-M/v8-M: 读 IPSR 寄存器低8位
 * RISC-V:       读 mcause, 检查 bit31
 * 其他:         默认 0 (保守, 不在 ISR 中调用 FreeRTOS FromISR API)
 */
COMPAT_STATIC_INLINE int osal_in_isr(void)
{
#if defined(__ARM_ARCH_7EM__) || defined(__CORTEX_M)
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    return (ipsr & 0xFF) != 0;
#elif defined(__riscv)
    uintptr_t mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    return (int)(mcause >> 31);
#else
    return 0;
#endif
}

/* ── Spinlock ── */
/**
 * @details 默认 CONFIG_OSAL_SPINLOCK_IRQ_DISABLE: 使用临界区 (ESP-IDF portMUX
 * @details 或 FreeRTOS taskENTER_CRITICAL). CONFIG_OSAL_SPINLOCK_ATOMIC:非 ESP 平台使用原子 test-and-set 忙等自旋锁 (仅适合 SMP).
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    /**< ESP-IDF portMUX 已包含嵌套计数; 非 ESP FreeRTOS 用全局临界区 */
#ifdef ESP_PLATFORM
    portMUX_TYPE mux;
#endif
#else
    volatile int locked;
#endif
};

/**
 * @brief 初始化自旋锁
 * @param lock 自旋锁指针
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(&lock->mux);
#endif
#else
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 锁定自旋锁
 * @param lock 自旋锁指针
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
#ifdef ESP_PLATFORM
    if (osal_in_isr())
        portENTER_CRITICAL_ISR(&lock->mux);
    else
        taskENTER_CRITICAL(&lock->mux);
#else
    taskENTER_CRITICAL();
#endif
#else
#ifdef ESP_PLATFORM
    /**< ESP 平台 portMUX 已经是关中断临界区, 原子模式在此不单独实现 */
    if (osal_in_isr())
        portENTER_CRITICAL_ISR(&lock->mux);
    else
        taskENTER_CRITICAL(&lock->mux);
#else
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE));
    __atomic_store_n(&lock->locked, 1, __ATOMIC_RELEASE);
#endif
#endif
    return OSAL_OK;
}

/**
 * @brief 解锁自旋锁
 * @param lock 自旋锁指针
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return OSAL_ERR_INVAL;
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
#ifdef ESP_PLATFORM
    if (osal_in_isr())
        portEXIT_CRITICAL_ISR(&lock->mux);
    else
        taskEXIT_CRITICAL(&lock->mux);
#else
    taskEXIT_CRITICAL();
#endif
#else
#ifdef ESP_PLATFORM
    if (osal_in_isr())
        portEXIT_CRITICAL_ISR(&lock->mux);
    else
        taskEXIT_CRITICAL(&lock->mux);
#else
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
#endif
#endif
    return OSAL_OK;
}

/**
 * @brief 检查自旋锁是否锁定
 * @param lock 自旋锁指针
 * @return 是否锁定
 */
COMPAT_STATIC_INLINE bool osal_spinlock_is_locked(struct osal_spinlock* lock)
{
    (void)lock;
    /**< 临界区模式下不暴露内部计数, 统一返回 false; 调用方不应依赖此状态 */
    return false;
}

/* ── 槽位池 (每池独立临界区锁) ── */


#ifdef ESP_PLATFORM
_Static_assert(sizeof(portMUX_TYPE) <= OSAL_POOL_MUX_STORAGE_SIZE,"OSAL_POOL_MUX_STORAGE_SIZE too small for portMUX_TYPE");
/**
 * @param pool 池指针
 * @return 临界区锁指针
 * @brief ESP平台使用portMUX_TYPE作为临界区锁 把字节缓冲区指针强制转换成 portMUX_TYPE 结构体指针, 然后传给 portENTER_CRITICAL_ISR 或 taskENTER_CRITICAL
 */
COMPAT_STATIC_INLINE portMUX_TYPE* osal_pool_mux(osal_pool_t* pool)
{
    return (portMUX_TYPE*)pool->mux_storage;
}
#else
typedef int portMUX_TYPE;
#endif

COMPAT_STATIC_INLINE void osal_pool_lock(osal_pool_t* pool)
{
#ifdef ESP_PLATFORM
    portMUX_TYPE* mux = osal_pool_mux(pool);
    if (osal_in_isr())
        portENTER_CRITICAL_ISR(mux);
    else
        taskENTER_CRITICAL(mux);
#else
    taskENTER_CRITICAL();
    (void)pool;
#endif
}

COMPAT_STATIC_INLINE void osal_pool_unlock(osal_pool_t* pool)
{
#ifdef ESP_PLATFORM
    portMUX_TYPE* mux = osal_pool_mux(pool);
    if (osal_in_isr())
        portEXIT_CRITICAL_ISR(mux);
    else
        taskEXIT_CRITICAL(mux);
#else
    taskEXIT_CRITICAL();
    (void)pool;
#endif
}

/**
 * @brief 初始化槽位池
 * @param pool 槽位池结构体指针
 * @param used_slots 槽位使用情况指针
 * @param slot_count 槽位数量
 * @return 结果
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* used_slots, size_t slot_count)
{
    if (!pool || !used_slots || slot_count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = used_slots;
    pool->slot_count = slot_count;

    for (size_t i = 0; i < slot_count; i++)
        used_slots[i] = 0;

#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(osal_pool_mux(pool));
#endif

    return 0;
}

/**
 * @brief 申请槽位
 * @param pool 槽位池结构体指针
 * @return 结果
 * @details 申请槽位时, 先随机选择一个起始槽位, 然后从起始槽位开始遍历, 找到第一个未使用的槽位, 然后返回槽位索引
 * @details 如果遍历完所有槽位都没有找到未使用的槽位, 则返回-1
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;

    uint32_t rand_val = COMPAT_RAND(0x43U, 0x32U, 0x43U, 0x32U);
    size_t start_idx = rand_val % pool->slot_count;

    osal_pool_lock(pool);

    int ret_idx = -1;
    for (size_t i = 0; i < pool->slot_count; i++)
    {
        size_t cur = (start_idx + i) % pool->slot_count;
        if (!pool->used_slots[cur])
        {
            pool->used_slots[cur] = 1;
            ret_idx = (int)cur;
            break;
        }
    }

    osal_pool_unlock(pool);
    return ret_idx;
}

/**
 * @brief 释放槽位
 * @param pool 槽位池结构体指针
 * @param slot_index 槽位索引
 * @return 结果
 * @details 释放槽位时, 直接将槽位使用情况指针设置为0
 * @details 如果槽位索引无效, 则返回
 */
int osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 ||
        (size_t)slot_index >= pool->slot_count)
        return OSAL_ERR_INVAL;

    osal_pool_lock(pool);
    pool->used_slots[slot_index] = 0;
    osal_pool_unlock(pool);
    return OSAL_OK;
}

/**
 * @brief 静态互斥锁池
 * @param s_mutex_pool 互斥锁池结构体指针
 * @param s_mutex_used 互斥锁使用情况指针
 * @param s_mutex_pool_ctrl 互斥锁池控制结构体指针
 */
static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t           s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t       s_mutex_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 * @return void
 * @details 初始化静态互斥锁池时, 使用 osal_pool_init 初始化互斥锁池控制结构体, 上电时执行
 */
pre_execution(150)
static void osal_mutex_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE));
}

/**
 * @brief 获取现在时间
 * @return 时间
 * @details 获取现在时间时, 使用 xTaskGetTickCount 获取系统滴答数, 然后转换为毫秒
 */
uint32_t osal_time_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief 延迟毫秒
 * @param ms 延迟时间
 * @return void
 * @details 延迟毫秒时, 使用 vTaskDelay 延迟
 * @details 如果在中断中, 则不延迟
 */
void osal_delay_ms(uint32_t ms)
{
    if (osal_in_isr()) return;  /* 中断中不能阻塞 */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief 单纯毫秒转 tick
 * @param ticks 滴答数
 * @return 毫秒
 * @details 将滴答数转换为毫秒时, 使用 TICKS_TO_MS 转换
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

/**
 * @brief 将毫秒转换为滴答数
 * @param timeout_ms 超时时间
 * @return 滴答数
 * @details 将毫秒转换为滴答数时, 使用 pdMS_TO_TICKS 转换
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return portMAX_DELAY;
    return pdMS_TO_TICKS(timeout_ms);
}

/* ── 内存 ── */
/**
 * @brief 分配内存
 * @param count 数量
 * @param size 大小
 * @return 内存指针
 * @details 分配内存时, 使用 calloc 分配内存
 */
void* osal_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

/**
 * @brief 释放内存
 * @param ptr 内存指针
 * @return void
 * @details 释放内存时, 使用 free 释放内存
 */
int osal_free(void* ptr)
{
    free(ptr);
    return OSAL_OK;
}

/* ── 互斥锁 ── */
/**
 * @brief 创建互斥锁
 * @param out 互斥锁指针
 * @param type 互斥锁类型
 * @return 结果
 * @details 创建互斥锁时, 使用 osal_pool_claim 申请互斥锁池中的一个槽位, 然后使用 osal_mutex_init 初始化互斥锁
 * @details 如果申请失败, 则返回 OSAL_ERR_NOMEM
 */
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out) return OSAL_ERR_INVAL;
    if (osal_in_isr()) 
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return OSAL_ERR_INVAL;
    *out = NULL;

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0) return OSAL_ERR_NOMEM;

    struct osal_mutex* m = &s_mutex_pool[index];
    if (osal_mutex_init(m, type) != OSAL_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, index));
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)m;
    return OSAL_OK;
}

/**
 * @brief 创建静态互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @param type 互斥锁类型
 * @details 需要手动分配存储空间,不占用池化资源
 * @return 结果
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) 
        return OSAL_ERR_INVAL;
    if (osal_in_isr()) 
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) 
        return OSAL_ERR_INVAL;
    *out = NULL;

    struct osal_mutex* mutex = (struct osal_mutex*)storage;
    if (osal_mutex_init(mutex, type) != OSAL_OK) 
        return OSAL_ERR_NOMEM;

    *out = (struct osal_mutex*)mutex;
    return OSAL_OK;
}

/**
 * @brief 创建互斥锁
 * @param out 互斥锁指针
 * @return 结果
 * @details 创建互斥锁时, 使用 osal_mutex_create_typed 创建互斥锁
 */
int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建静态互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @return 结果
 * @details 创建静态互斥锁时, 使用 osal_mutex_create_static_typed 创建静态互斥锁
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建递归互斥锁
 * @param out 互斥锁指针
 * @return 结果
 * @details 创建递归互斥锁时, 使用 osal_mutex_create_typed 创建递归互斥锁
 */
int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 创建静态递归互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @return 结果
 * @details 创建静态递归互斥锁时, 使用 osal_mutex_create_static_typed 创建静态递归互斥锁
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief 创建普通互斥锁
 * @param out 互斥锁指针
 * @return 结果
 * @details 创建普通互斥锁时, 使用 osal_mutex_create_typed 创建普通互斥锁
 */
int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 创建静态普通互斥锁
 * @param out 互斥锁指针
 * @param storage 静态互斥锁存储指针
 * @param storage_size 静态互斥锁存储大小
 * @return 结果
 * @details 创建静态普通互斥锁时, 使用 osal_mutex_create_static_typed 创建静态普通互斥锁
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief 销毁互斥锁
 * @param mutex 互斥锁指针
 * @return void
 * @details 销毁互斥锁时, 使用 vSemaphoreDelete 销毁互斥锁
 * @details 如果互斥锁为空, 则返回
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex || osal_in_isr())
        return;
 
    if (mutex->handle != NULL)
    {
        /**<先销毁底层信号量 */
        vSemaphoreDelete(mutex->handle);
        mutex->handle = NULL;
    }
    /**<然后判断是否属于全局mutex池 */
    uintptr_t pool_start = (uintptr_t)s_mutex_pool;
    uintptr_t pool_end = pool_start + sizeof(s_mutex_pool);
    uintptr_t ptr = (uintptr_t)mutex;
 
    /**<如果属于全局mutex池, 则释放池化资源 */
    if (ptr >= pool_start && ptr < pool_end)
    {
         size_t idx = (uintptr_t)mutex - pool_start;
         COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, (int)idx));
    }
}

/**
 * @brief 锁定互斥锁
 * @param mutex 互斥锁指针
 * @param timeout_ms 超时时间
 * @return 结果
 * @details 锁定互斥锁时, 使用 xSemaphoreTake 锁定互斥锁
 * @details 如果互斥锁为空, 则返回 OSAL_ERR_INVAL
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex || !mutex->handle || osal_in_isr())
        return OSAL_ERR_INVAL;
    if (osal_in_isr()) 
        return OSAL_ERR_ISR;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    if (mutex->type == OSAL_MUTEX_RECURSIVE)
        return xSemaphoreTakeRecursive(mutex->handle, ticks) == pdTRUE ? OSAL_OK : OSAL_ERR_TIMEOUT;
    else
        return xSemaphoreTake(mutex->handle, ticks) == pdTRUE ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief 释放互斥锁
 * @param mutex 互斥锁指针
 * @return 结果
 * @details 释放互斥锁时, 使用 xSemaphoreGive 释放互斥锁
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex || !mutex->handle) return OSAL_ERR_INVAL;
    if (osal_in_isr()) return OSAL_ERR_ISR;  /* 中断中不允许释放 */
    if (mutex->type == OSAL_MUTEX_RECURSIVE)
        return xSemaphoreGiveRecursive(mutex->handle) == pdTRUE ? OSAL_OK : OSAL_ERR_IO;
    return xSemaphoreGive(mutex->handle) == pdTRUE ? OSAL_OK : OSAL_ERR_IO;
}

/* ── 二值信号量 ── */
struct osal_sem
{
    SemaphoreHandle_t handle;
    StaticSemaphore_t sem_buf;
    bool              from_pool;
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE,"OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t       s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t   s_sem_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 * @return void
 * @details 初始化二值信号量池时, 使用 osal_pool_init 初始化二值信号量池
 */
pre_execution(151)
static void osal_sem_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE));
}

/**
 * @brief 初始化二值信号量
 * @param sem 二值信号量指针
 * @return 结果
 * @details 初始化二值信号量时, 使用 xSemaphoreCreateBinaryStatic 初始化二值信号量
 */
static int osal_sem_init_binary(struct osal_sem* sem)
{
    if (!sem)
        return OSAL_ERR_INVAL;

    sem->handle = xSemaphoreCreateBinaryStatic(&sem->sem_buf);
    if (!sem->handle)
        return OSAL_ERR_NOMEM;

    return 0;
}

/**
 * @brief 创建二值信号量
 * @param out 二值信号量指针
 * @return 结果
 * @details 创建二值信号量时, 使用 osal_sem_init_binary 创建二值信号量
 */
int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_sem* sem = &s_sem_pool[idx];
    if (osal_sem_init_binary(sem) != OSAL_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
        return OSAL_ERR_NOMEM;
    }

    sem->from_pool = true;
    *out = sem;
    return 0;
}

/**
 * @brief 创建静态二值信号量
 * @param out 二值信号量指针
 * @param storage 静态二值信号量存储指针
 * @param storage_size 静态二值信号量存储大小
 * @return 结果
 * @details 创建静态二值信号量时, 使用 osal_sem_init_binary 创建静态二值信号量
 */
int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return OSAL_ERR_INVAL;

    struct osal_sem* sem = (struct osal_sem*)storage;
    if (osal_sem_init_binary(sem) != OSAL_OK)
        return OSAL_ERR_NOMEM;

    sem->from_pool = false;
    *out = sem;
    return 0;
}

/**
 * @brief 销毁二值信号量
 * @param sem 二值信号量指针
 * @return void
 * @details 销毁二值信号量时, 使用 vSemaphoreDelete 销毁二值信号量
 * @details 如果二值信号量为空, 则返回
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem || !sem->handle || osal_in_isr())
        return;

    if (sem->from_pool)
    {
        uintptr_t pool_start = (uintptr_t)s_sem_pool;
        uintptr_t pool_end = pool_start + sizeof(s_sem_pool);
        uintptr_t ptr = (uintptr_t)sem;

        if (ptr >= pool_start && ptr < pool_end)
        {
            size_t idx = sem - s_sem_pool;
            if (idx < OSAL_SEM_POOL_SIZE)
                COMPAT_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, (int)idx));
        }
    }
}

/**
 * @brief 等待二值信号量
 * @param sem 二值信号量指针
 * @param timeout_ms 超时时间
 * @return 结果
 * @details 等待二值信号量时, 使用 xSemaphoreTake 等待二值信号量
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem || !sem->handle || osal_in_isr())
        return OSAL_ERR_ISR;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xSemaphoreTake(sem->handle, ticks) == pdTRUE ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief 释放二值信号量
 * @param sem 二值信号量指针
 * @return 结果
 * @details 释放二值信号量时, 使用 xSemaphoreGive 释放二值信号量
 */
bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem || !sem->handle || osal_in_isr())
        return false;

    return xSemaphoreGive(sem->handle) == pdTRUE;
}

/**
 * @brief 通知ISR上下文切换
 * @param px_yield_required 是否需要切换
 * @param higher_prio_woken 更高优先级唤醒
 * @return void
 * @details 通知ISR上下文切换时, 使用 osal_note_isr_yield 通知ISR上下文切换
 */
COMPAT_STATIC_INLINE void osal_note_isr_yield(bool* px_yield_required, BaseType_t higher_prio_woken)
{
    if (px_yield_required != NULL && higher_prio_woken == pdTRUE)
        *px_yield_required = true;
}

/**
 * @brief 从ISR上下文释放二值信号量
 * @param sem 二值信号量指针
 * @param px_yield_required 是否需要切换
 * @return 结果
 * @details 从ISR上下文释放二值信号量时, 使用 xSemaphoreGiveFromISR 从ISR上下文释放二值信号量
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    if (!sem || !sem->handle)
        return false;

    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xSemaphoreGiveFromISR(sem->handle, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/**
 * @brief 从ISR上下文切换
 * @param yield_required 是否需要切换
 * @return void
 * @details 从ISR上下文切换时, 使用 portYIELD_FROM_ISR 从ISR上下文切换
 */
void osal_yield_from_isr(bool yield_required)
{
    if (yield_required)
        portYIELD_FROM_ISR(pdTRUE);
}

/**
 * @brief 调度器冻结
 * @return void
 * @details 调度器冻结时, 使用 vTaskSuspendAll 调度器冻结
 */
void osal_sched_freeze(void)
{
    vTaskSuspendAll();
}

/**
 * @brief 中断冻结 就是禁用中断
 * @return void
 * @details 中断冻结时, 使用 portDISABLE_INTERRUPTS 中断冻结
 */
void osal_int_freeze(void)
{
    portDISABLE_INTERRUPTS();
}

/**
 * @brief 静态分配回调
 * @param ppxIdleTaskTCBBuffer 空闲任务TCB缓冲区
 * @param ppxIdleTaskStackBuffer 空闲任务栈缓冲区
 * @param pulIdleTaskStackSize 空闲任务栈大小
 * @return void
 * @details 静态分配回调时, 使用 vApplicationGetIdleTaskMemory 静态分配回调
 * @details ESP-IDF v5.x 自身已在 port_common.c 提供此回调
 */
#ifndef ESP_PLATFORM
static StackType_t   s_idle_stack[configMINIMAL_STACK_SIZE];/**<空闲任务栈*/
static StaticTask_t  s_idle_tcb;/**<空闲任务TCB*/

/**
 * @brief 静态分配回调
 * @param ppxIdleTaskTCBBuffer 空闲任务TCB缓冲区
 * @param ppxIdleTaskStackBuffer 空闲任务栈缓冲区
 * @param pulIdleTaskStackSize 空闲任务栈大小
 * @return void
 * @details 静态分配回调时, 使用 vApplicationGetIdleTaskMemory 静态分配回调
 */
void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,StackType_t** ppxIdleTaskStackBuffer,uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}
#endif /* !ESP_PLATFORM */

/**
 * @brief 任务 (stack_size 字节 → FreeRTOS words 转换)
 * @param stack_bytes 栈大小
 * @return 栈大小
 * @details 任务时, 使用 osal_stack_words 将栈大小转换为FreeRTOS words
 */
COMPAT_STATIC_INLINE uint32_t osal_stack_words(uint32_t stack_bytes)
{
    return (stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t);/**<向上取整, 确保栈大小足够>*/
}

/**
 * @brief 创建任务 无法拿到任务句柄
 * @param name 任务名称
 * @param stack_size 栈大小
 * @param priority 优先级
 * @param entry 任务入口
 * @param param 任务参数
 * @param core_id 核心ID
 * @return 结果
 * @details 创建任务时, 使用 xTaskCreate 创建任务 默认创建在Core 0上
 */
int osal_task_create(const char* name, uint32_t stack_size,uint32_t priority, osal_task_entry_t entry,void* param, int core_id)
{
#if CONFIG_CPU_CORES > 1
    if (core_id > 0)
    {
        my_printf_output("[osal] WARN: task '%s' requested Core %d, ""but AMP Core 1 has no OS scheduler. ""Falling back to Core 0.\n", name, core_id);
        core_id = 0;
    }
#else
    (void)core_id;
#endif

    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreate(entry, name, osal_stack_words(stack_size),param, priority, &handle);
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_NOMEM;
}

/**
 * @brief 创建任务句柄 可以拿到任务句柄
 * @param name 任务名称
 * @param stack_size 栈大小
 * @param priority 优先级
 * @param entry 任务入口
 * @param param 任务参数
 * @param core_id 核心ID
 * @param out_handle 任务句柄
 * @return 结果
 * @details 创建任务句柄时, 使用 xTaskCreate 创建任务句柄
 */
int osal_task_create_handle(const char* name, uint32_t stack_size,uint32_t priority, osal_task_entry_t entry,void* param, int core_id,osal_task_handle_t* out_handle)
{
    if (!out_handle) return OSAL_ERR_INVAL;
#if CONFIG_CPU_CORES > 1
    if (core_id > 0)
    {
        my_printf_output("[osal] WARN: task '%s' requested Core %d, ""but AMP Core 1 has no OS scheduler. ""Falling back to Core 0.\n", name, core_id);
        core_id = 0;
    }
#else
    (void)core_id;
#endif

    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreate(entry, name, osal_stack_words(stack_size),param, priority, &handle);
    if (ret != pdPASS) 
        return OSAL_ERR_NOMEM;
    *out_handle = (osal_task_handle_t)handle;
    return 0;
}

/**
 * @brief 删除当前任务
 * @return void
 * @details 删除当前任务时, 使用 vTaskDelete 删除当前任务
 */
void osal_task_self_delete(void)
{
#ifdef ESP_PLATFORM
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self != NULL && esp_task_wdt_status(self) == ESP_OK)
        esp_task_wdt_delete(self);
#endif
    vTaskDelete(NULL);
}

/**
 * @brief 删除任务
 * @param task 任务句柄
 * @return void
 * @details 删除任务时, 使用 vTaskDelete 删除任务
 */
void osal_task_delete(osal_task_handle_t task)
{
    vTaskDelete((TaskHandle_t)task);
}

/**
 * @brief 判断任务是否运行
 * @param task 任务句柄
 * @return 是否运行
 * @details 判断任务是否运行时, 使用 eTaskGetState 判断任务是否运行
 */
bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task) return false;
    return eTaskGetState((TaskHandle_t)task) != eDeleted;
}

/**
 * @brief 获取任务名称
 * @param task 任务句柄
 * @return 任务名称
 * @details 获取任务名称时, 使用 pcTaskGetName 获取任务名称
 */
const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task) return "?";
    return pcTaskGetName((TaskHandle_t)task);
}

/**
 * @brief 获取任务栈水位线
 * @param task 任务句柄
 * @return 栈水位线
 * @details 获取任务栈水位线时, 使用 uxTaskGetStackHighWaterMark 获取任务栈水位线
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task) return 0;
    UBaseType_t wm = uxTaskGetStackHighWaterMark((TaskHandle_t)task);
    return (uint32_t)wm * sizeof(StackType_t);
}

/**
 * @brief 创建队列
 * @param queue_len 队列长度
 * @param item_size 队列元素大小
 * @return 队列句柄
 * @details 创建队列时, 使用 xQueueCreate 创建队列
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    return (osal_queue_handle_t)xQueueCreate(queue_len, item_size);
}

/**
 * @brief 删除队列
 * @param queue 队列句柄
 * @return void
 * @details 删除队列时, 使用 vQueueDelete 删除队列
 */
void osal_queue_delete(osal_queue_handle_t queue)
{
    vQueueDelete((QueueHandle_t)queue);
}

/**
 * @brief 发送消息到队列
 * @param queue 队列句柄
 * @param item 消息
 * @param timeout_ms 超时时间
 * @return 是否成功
 * @details 发送消息到队列时, 使用 xQueueSend 发送消息到队列
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

/**
 * @brief 从ISR上下文发送消息到队列
 * @param queue 队列句柄
 * @param item 消息
 * @param px_yield_required 是否需要切换
 * @return 是否成功
 * @details 从ISR上下文发送消息到队列时, 使用 xQueueSendFromISR 从ISR上下文发送消息到队列
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/**
 * @brief 接收消息从队列
 * @param queue 队列句柄
 * @param item 消息
 * @param timeout_ms 超时时间
 * @return 是否成功
 * @details 接收消息从队列时, 使用 xQueueReceive 接收消息从队列
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

/**
 * @brief 从ISR上下文接收消息从队列
 * @param queue 队列句柄
 * @param item 消息
 * @param px_yield_required 是否需要切换
 * @return 是否成功
 * @details 从ISR上下文接收消息从队列时, 使用 xQueueReceiveFromISR 从ISR上下文接收消息从队列
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/**
 * @brief 硬件安全关断 就是触发断言陷入指令
 * @return void
 * @details 硬件安全关断时, 使用 COMPAT_TRAP 硬件安全关断
 */
COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

/**
 * @brief 安全互锁 自己实现
 * @return void
 * @details 安全互锁时, 使用 osal_panic_interlock 安全互锁
 */
COMPAT_WEAK void osal_panic_interlock(void)
{
    /* 板级可覆盖: 喂硬件看门狗, 切断执行器供电, 等待复位 */
}

/**
 * @brief 日志
 * @param level 日志级别
 * @param tag 日志标签
 * @param fmt 日志格式
 * @return void
 * @details 日志时, 使用 my_printf_output 日志
 */
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

/**
 * @brief 致命错误日志
 * @param fmt 日志格式
 * @return void
 * @details 致命错误日志时, 使用 my_printf_output 致命错误日志
 */
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

/**
 * @brief 严重错误日志推荐用 __FILE__ __LINE__ 宏 替代file和line参数
 * @param file 文件名
 * @param line 行号
 * @param fmt 日志格式
 * @return void
 * @details 严重错误日志时, 使用 my_printf_output 严重错误日志
 */
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

#endif /* CONFIG_OSAL_FREERTOS */
