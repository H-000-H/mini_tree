/* SPDX-License-Identifier: Apache-2.0 */
/*
 * osal_freertos.c — OSAL FreeRTOS 后端实现
 *
 * 将 OSAL API 映射到 xSemaphore/xQueue/xTaskCreate 等 FreeRTOS 原语
 * 静态互斥锁/信号量池 + 槽位池 (osal_pool), ISR 临界区用
 * taskENTER_CRITICAL_FROM_ISR / taskEXIT_CRITICAL_FROM_ISR
 * ESP32 (ESP-IDF) 平台额外嵌入 portMUX 适配, 由 ESP-IDF 自带
 * FreeRTOS 提供, 项目侧勿重复 vendor
 *
 * 关键差异 (参考基准, 其他后端差异以此对齐):
 * 1. ISR 临界区用 taskENTER/EXIT_CRITICAL_FROM_ISR; ESP-IDF 下等价于
 *    portMUX 自旋锁 (portTICK_RATE_MS 1 ms tick), 退出自动让出;
 * 2. 互斥锁 xSemaphoreCreateMutex 自带优先级继承 (避免优先级反转),
 *    OSAL_MUTEX_PLAIN 的"二次获取阻塞"语义由本层自实现 (递归计数包装)
 *    确保 OSAL_ERR_TIMEOUT 一致返回;
 * 3. 信号量是计数的 — 本层用 posted 标志保证"多次 post 合并计数 ≤ 1"
 *    实现严格二值语义;
 * 4. 任务删除自身 vTaskDelete(NULL) 真返回 (与 ThreadX 不同),
 *    任务控制块/栈可被 idle 回收;
 * 5. ISR 出口 osal_yield_from_isr 调用 portYIELD_FROM_ISR 触发 PendSV;
 * 6. 栈水位依赖 configCHECK_FOR_STACK_OVERFLOW > 0; 无运行时栈高水位查询
 *    (FreeRTOS 不暴露), 仅靠 overflow hook 检测.
 */
#ifdef CONFIG_OSAL_FREERTOS

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "board_config.h"
#include "compiler_compat.h"
#include "config.h"
#include "osal.h"
#include "status.h"
#ifdef ESP_PLATFORM
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "compiler_compat_poison.h"

/* ── 队列 / 信号量内部存储 ── */
struct osal_mutex
{
    SemaphoreHandle_t handle; /**< FreeRTOS 句柄 */
    StaticSemaphore_t sem_buf; /**< 静态存储 */
    osal_mutex_type_t type; /**< 互斥锁类型 */
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

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "OSAL_MUTEX_STORAGE_SIZE too small");

/**
 * @brief 判定是否在 FreeRTOS ISR 上下文
 * @return 1 在 ISR 中, 0 不在
 * @details ARMv7-M/v8-M 读 IPSR; RISC-V 读 mcause bit31; 其他架构保守返回 0
 */
int osal_in_isr(void)
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
 * @details 或 FreeRTOS taskENTER_CRITICAL). CONFIG_OSAL_SPINLOCK_ATOMIC:非 ESP 平台使用原子
 * test-and-set 忙等自旋锁 (仅适合 SMP).
 */
struct osal_spinlock
{
#ifdef CONFIG_OSAL_SPINLOCK_IRQ_DISABLE
    /**< ESP-IDF portMUX 已包含嵌套计数; 非 ESP FreeRTOS 用全局临界区 */
#ifdef ESP_PLATFORM
    portMUX_TYPE mux; /**< ESP-IDF portMUX 自旋锁 */
#endif
#else
    volatile int locked; /**< 原子锁标志 (0=空闲, 1=持有) */
#endif
};

/**
 * @brief 初始化自旋锁
 * @param lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
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
 * @brief 进入自旋锁
 * @param lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
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
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
        ;
    __atomic_store_n(&lock->locked, 1, __ATOMIC_RELEASE);
#endif
#endif
    return OSAL_OK;
}

/**
 * @brief 退出自旋锁
 * @param lock 锁
 * @return OSAL_OK
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
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
COMPAT_UNUSED COMPAT_STATIC_INLINE bool osal_spinlock_is_locked(struct osal_spinlock* lock)
{
    COMPAT_UNUSED_PARAM(lock);
    /**< 临界区模式下不暴露内部计数, 统一返回 false; 调用方不应依赖此状态 */
    return false;
}

/* ── 槽位池 (每池独立临界区锁) ── */

#ifdef ESP_PLATFORM
_Static_assert(sizeof(portMUX_TYPE) <= OSAL_POOL_MUX_STORAGE_SIZE,
               "OSAL_POOL_MUX_STORAGE_SIZE too small for portMUX_TYPE");
/**
 * @param pool 池指针
 * @return 临界区锁指针
 * @brief ESP平台使用portMUX_TYPE作为临界区锁 把字节缓冲区指针强制转换成 portMUX_TYPE 结构体指针,
 * 然后传给 portENTER_CRITICAL_ISR 或 taskENTER_CRITICAL
 */
COMPAT_STATIC_INLINE portMUX_TYPE* osal_pool_mux(osal_pool_t* pool)
{
    return (portMUX_TYPE*)pool->mux_storage;
}
#else
typedef int portMUX_TYPE;
#endif

/**
 * @brief 进入槽位池临界区 (ESP 用 portMUX, 其它平台用 taskENTER_CRITICAL)
 * @param pool 槽位池指针
 */
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
    COMPAT_UNUSED_PARAM(pool);
#endif
}

/**
 * @brief 退出槽位池临界区
 * @param pool 槽位池指针
 */
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
    COMPAT_UNUSED_PARAM(pool);
#endif
}

/**
 * @brief 初始化槽位池
 * @param pool 池
 * @param used_slots 数组
 * @param slot_count 数量
 * @return 0 或 OSAL_ERR_INVAL
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
 * @param pool 池
 * @return 索引或负值
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;
    osal_pool_lock(pool);

    int ret_idx = -1;
    for (size_t i = 0; i < pool->slot_count; i++)
    {
        if (!pool->used_slots[i])
        {
            pool->used_slots[i] = 1;
            ret_idx = (int)i;
            break;
        }
    }

    osal_pool_unlock(pool);
    return ret_idx;
}

/**
 * @brief 释放槽位
 * @param pool 池
 * @param slot_index 索引
 * @return OSAL_OK
 */
int osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return OSAL_ERR_INVAL;

    osal_pool_lock(pool);
    pool->used_slots[slot_index] = 0;
    osal_pool_unlock(pool);
    return OSAL_OK;
}

/**
 * @brief 查询槽占用
 * @param pool 池
 * @param slot_index 索引
 * @return true 已占用
 */
bool osal_pool_is_used(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return false;
    return pool->used_slots[slot_index] != 0U;
}

/**
 * @brief 静态互斥锁池
 * @param s_mutex_pool 互斥锁池结构体指针
 * @param s_mutex_used 互斥锁使用情况指针
 * @param s_mutex_pool_ctrl 互斥锁池控制结构体指针
 */
static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t s_mutex_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 * @details 上电时通过 pre_execution 调用 osal_pool_init 初始化互斥锁池控制结构体
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void osal_mutex_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE));
}

/**
 * @brief xTaskGetTickCount 转 ms
 * @return 毫秒
 */
uint32_t osal_time_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

/**
 * @brief vTaskDelay
 * @param ms 毫秒
 */
void osal_delay_ms(uint32_t ms)
{
    if (osal_in_isr())
        return; /* 中断中不能阻塞 */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief 忙等微秒（不让出调度；1-Wire 等短时序）
 */
void osal_delay_us(uint32_t us)
{
    if (us == 0U)
        return;
#ifdef ESP_PLATFORM
    /* 厂商 ROM 忙等锁在 OSAL，不进 product driver */
    extern void esp_rom_delay_us(uint32_t us);
    esp_rom_delay_us(us);
#else
    /* 粗略忙等：按 configTICK_RATE_HZ 不可靠，用 CPU 时钟周期近似 */
    {
        uint32_t cycles = us * (configCPU_CLOCK_HZ / 1000000U);
        volatile uint32_t i;
        for (i = 0; i < cycles; i++)
            COMPAT_UNUSED_PARAM(i);
    }
#endif
}

/**
 * @brief pdMS_TO_TICKS
 * @param ms 毫秒
 * @return tick
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms) { return pdMS_TO_TICKS(ms); }

/**
 * @brief 超时转 tick
 * @param timeout_ms 毫秒
 * @return tick
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return portMAX_DELAY;
    return pdMS_TO_TICKS(timeout_ms);
}

/* ── 内存 ── */
/**
 * @brief calloc
 * @param count 数量
 * @param size 大小
 * @return 指针
 */
void* osal_calloc(size_t count, size_t size) { return calloc(count, size); }

/**
 * @brief free
 * @param ptr 指针
 * @return OSAL_OK
 */
int osal_free(void* ptr)
{
    free(ptr);
    return OSAL_OK;
}

/* ── 互斥锁 ── */
/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN)
        return OSAL_ERR_INVAL;
    *out = NULL;

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0)
        return OSAL_ERR_NOMEM;

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
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage, size_t storage_size,
                                   osal_mutex_type_t type)
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
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief vSemaphoreDelete + 释放池槽
 * @param mutex 锁
 */
void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex || osal_in_isr())
        return;

    /* 只有 handle 非空 (合法创建的锁) 才允许销毁底层信号量;
     * handle 为空的池外指针视为非法输入, 不触碰其字段. */
    if (mutex->handle == NULL)
        return;

    /**< 先销毁底层信号量 */
    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;

    /**< 再判断是否属于全局mutex池: 仅池内对象归还池槽, 静态锁不归还 */
    if (mutex >= s_mutex_pool && mutex < &s_mutex_pool[OSAL_MUTEX_POOL_SIZE])
    {
        size_t idx = (size_t)(mutex - s_mutex_pool);
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, (int)idx));
    }
}

/**
 * @brief xSemaphoreTake
 * @param mutex 锁
 * @param timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
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
 * @brief xSemaphoreGive
 * @param mutex 锁
 * @return OSAL_OK 或 OSAL_ERR_IO
 */
int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex || !mutex->handle)
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR; /* 中断中不允许释放 */
    if (mutex->type == OSAL_MUTEX_RECURSIVE)
        return xSemaphoreGiveRecursive(mutex->handle) == pdTRUE ? OSAL_OK : OSAL_ERR_IO;
    return xSemaphoreGive(mutex->handle) == pdTRUE ? OSAL_OK : OSAL_ERR_IO;
}

/* ── 二值信号量 ── */
struct osal_sem
{
    SemaphoreHandle_t handle; /**< FreeRTOS 句柄 */
    StaticSemaphore_t sem_buf; /**< 静态存储 */
    bool from_pool; /**< 是否来自静态池 */
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE, "OSAL_SEM_STORAGE_SIZE too small");

/**
 * @brief 静态二值信号量池
 * @param s_sem_pool 信号量池结构体指针
 * @param s_sem_used 信号量池占用数组
 * @param s_sem_pool_ctrl 信号量池控制结构体指针
 */
static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
/**
 * @brief 静态二值信号量池占用数组
 * @param s_sem_pool 信号量池结构体指针
 * @param s_sem_used 信号量池占用数组
 * @param s_sem_pool_ctrl 信号量池控制结构体指针
 */
static uint8_t s_sem_used[OSAL_SEM_POOL_SIZE] COMPAT_ALIGNED(4);
/**
 * @brief 静态二值信号量池控制
 * @param s_sem_pool 信号量池结构体指针
 * @param s_sem_used 信号量池占用数组
 * @param s_sem_pool_ctrl 信号量池控制结构体指针
 */
static osal_pool_t s_sem_pool_ctrl COMPAT_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 * @details 上电时通过 pre_execution 调用 osal_pool_init 初始化二值信号量池
 */
pre_execution(PRE_EXEC_PRIO_SEM_POOL) static void osal_sem_pool_boot_init(void)
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
 * @brief 池化二值信号量
 * @param out 输出
 * @return 0 或错误码
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
 * @brief 静态二值信号量
 * @param out 输出
 * @param storage 存储
 * @param storage_size 大小
 * @return 0 或错误码
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
 * @brief 销毁信号量
 * @param sem 信号量
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem || !sem->handle || osal_in_isr())
        return;

    if (sem->from_pool)
    {
        if (sem >= s_sem_pool && sem < &s_sem_pool[OSAL_SEM_POOL_SIZE])
        {
            size_t idx = (size_t)(sem - s_sem_pool);
            COMPAT_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, (int)idx));
        }
    }
}

/**
 * @brief xSemaphoreTake
 * @param sem 信号量
 * @param timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem || !sem->handle || osal_in_isr())
        return OSAL_ERR_ISR;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xSemaphoreTake(sem->handle, ticks) == pdTRUE ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

/**
 * @brief xSemaphoreGive
 * @param sem 信号量
 * @return true
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
 * @brief xSemaphoreGiveFromISR
 * @param sem 信号量
 * @param px_yield_required yield
 * @return true
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
 * @brief portYIELD_FROM_ISR
 * @param yield_required 是否 yield
 */
void osal_yield_from_isr(bool yield_required)
{
    if (yield_required)
        portYIELD_FROM_ISR(pdTRUE);
}

/**
 * @brief vTaskSuspendAll
 */
void osal_sched_freeze(void) { vTaskSuspendAll(); }

/**
 * @brief portDISABLE_INTERRUPTS
 */
void osal_int_freeze(void) { portDISABLE_INTERRUPTS(); }

/**
 * @brief FreeRTOS 空闲任务静态内存分配回调
 * @param ppxIdleTaskTCBBuffer 输出空闲任务 TCB 缓冲区指针
 * @param ppxIdleTaskStackBuffer 输出空闲任务栈缓冲区指针
 * @param pulIdleTaskStackSize 输出空闲任务栈深度 (StackType_t 个数)
 */
#ifndef ESP_PLATFORM
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE]; /**<空闲任务栈*/
static StaticTask_t s_idle_tcb; /**<空闲任务TCB*/

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                   StackType_t** ppxIdleTaskStackBuffer,
                                   uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/**
 * @brief FreeRTOS 栈溢出钩子 (configCHECK_FOR_STACK_OVERFLOW=2 时必需)
 */
void vApplicationStackOverflowHook(TaskHandle_t x_task, char* pcTaskName)
{
    COMPAT_UNUSED_PARAM(x_task);
    COMPAT_UNUSED_PARAM(pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
#endif /* !ESP_PLATFORM */

/**
 * @brief 将栈字节数换算为 FreeRTOS StackType_t 个数 (向上取整)
 * @param stack_bytes 栈大小 (字节)
 * @return StackType_t 元素个数
 */
COMPAT_STATIC_INLINE uint32_t osal_stack_words(uint32_t stack_bytes)
{
    return (stack_bytes + sizeof(StackType_t) - 1) /
           sizeof(StackType_t); /**< 向上取整, 确保栈大小足够 */
}

/**
 * @brief 钳位到 FreeRTOS 合法优先级 [0, configMAX_PRIORITIES)
 */
COMPAT_STATIC_INLINE UBaseType_t osal_clamp_task_priority(uint32_t priority)
{
    if (priority >= (uint32_t)configMAX_PRIORITIES)
        return (UBaseType_t)(configMAX_PRIORITIES - 1U);
    return (UBaseType_t)priority;
}

/**
 * @brief xTaskCreate
 * @param name 名
 * @param stack_size 栈字节
 * @param priority 优先级
 * @param entry 入口
 * @param param 参数
 * @param core_id 核
 * @return OSAL_OK 或 NOMEM
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority,
                     osal_task_entry_t entry, void* param, int core_id)
{
#if CONFIG_CPU_CORES > 1
    if (core_id > 0)
    {
        my_printf_output("[osal] WARN: task '%s' requested Core %d, "
                         "but AMP Core 1 has no OS scheduler. "
                         "Falling back to Core 0.\n",
                         name, core_id);
        core_id = 0;
    }
#else
    COMPAT_UNUSED_PARAM(core_id);
#endif

    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreate(entry, name, osal_stack_words(stack_size), param,
                                 osal_clamp_task_priority(priority), &handle);
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_NOMEM;
}

/**
 * @brief xTaskCreate 返回句柄
 * @param name 名
 * @param stack_size 栈
 * @param priority 优先级
 * @param entry 入口
 * @param param 参数
 * @param core_id 核
 * @param out_handle 输出
 * @return 0 或 NOMEM
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority,
                            osal_task_entry_t entry, void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle)
        return OSAL_ERR_INVAL;
#if CONFIG_CPU_CORES > 1
    if (core_id > 0)
    {
        my_printf_output("[osal] WARN: task '%s' requested Core %d, "
                         "but AMP Core 1 has no OS scheduler. "
                         "Falling back to Core 0.\n",
                         name, core_id);
        core_id = 0;
    }
#else
    COMPAT_UNUSED_PARAM(core_id);
#endif

    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreate(entry, name, osal_stack_words(stack_size), param,
                                 osal_clamp_task_priority(priority), &handle);
    if (ret != pdPASS)
        return OSAL_ERR_NOMEM;
    *out_handle = (osal_task_handle_t)handle;
    return 0;
}

/**
 * @brief vTaskDelete(NULL)
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
 * @brief vTaskDelete
 * @param task 句柄
 */
void osal_task_delete(osal_task_handle_t task) { vTaskDelete((TaskHandle_t)task); }

/**
 * @brief eTaskGetState != eDeleted
 * @param task 句柄
 * @return true 运行
 */
bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task)
        return false;
    return eTaskGetState((TaskHandle_t)task) != eDeleted;
}

/**
 * @brief pcTaskGetName
 * @param task 句柄
 * @return 名称
 */
const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task)
        return "?";
    return pcTaskGetName((TaskHandle_t)task);
}

/**
 * @brief uxTaskGetStackHighWaterMark×字节
 * @param task 句柄
 * @return 剩余栈字节
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task)
        return 0;
    UBaseType_t wm = uxTaskGetStackHighWaterMark((TaskHandle_t)task);
    return (uint32_t)wm * sizeof(StackType_t);
}

/**
 * @brief xQueueCreate
 * @param queue_len 长度
 * @param item_size 大小
 * @return 句柄
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    return (osal_queue_handle_t)xQueueCreate(queue_len, item_size);
}

/**
 * @brief vQueueDelete
 * @param queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue) { vQueueDelete((QueueHandle_t)queue); }

/**
 * @brief xQueueSend 任务态
 * @param queue 句柄
 * @param item 数据
 * @param timeout_ms 超时
 * @return true
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

/**
 * @brief xQueueSendFromISR 发送
 * @param queue 队列句柄
 * @param item 待发送数据
 * @param px_yield_required 输出是否需要 ISR 末尾 yield
 * @return true 成功
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/**
 * @brief xQueueReceive
 * @param queue 句柄
 * @param item 缓冲
 * @param timeout_ms 超时
 * @return true
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

/**
 * @brief xQueueReceiveFromISR 接收
 * @param queue 队列句柄
 * @param item 接收缓冲区
 * @param px_yield_required 输出是否需要 ISR 末尾 yield
 * @return true 成功
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/**
 * @brief 弱符号硬件安全关断 (板级未覆盖时触发 trap)
 */
COMPAT_WEAK void safety_hardware_shutdown(void) { COMPAT_TRAP(); }

/**
 * @brief 弱符号 Panic 安全互锁 (板级可覆盖: 喂狗、切断执行器等)
 */
COMPAT_WEAK void osal_panic_interlock(void)
{
    /* 板级可覆盖: 喂硬件看门狗, 切断执行器供电, 等待复位 */
}

/**
 * @brief 日志
 * @param level 级别
 * @param tag 标签
 * @param fmt 格式
 * @param ... 参数
 */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    COMPAT_UNUSED_PARAM(level);
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("[%s] ", tag ? tag : "drv");
    vprintf(fmt, args);
    my_printf_output("\n");
    va_end(args);
}

/**
 * @brief 致命日志
 * @param fmt 格式
 * @param ... 参数
 */
void osal_log_fatal(const char* fmt, ...)
{
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[FATAL ERROR] ");
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

/**
 * @brief 断言日志
 * @param file 文件
 * @param line 行
 * @param fmt 格式
 * @param ... 参数
 */
void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
{
    if (!fmt)
        fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[CRITICAL_ASSERT FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_FREERTOS */
