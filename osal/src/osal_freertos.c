/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file osal_freertos.c
 *@brief osal freertos 实现
 *@author H-000-H
 *@details
 *   osal_freertos.c — OSAL FreeRTOS 后端实现
 *   将 OSAL API 映射到 xSemaphore/xQueue/xTaskCreate 等 FreeRTOS 原语
 *   静态互斥锁/信号量池 + 槽位池 (osal_pool), ISR 临界区用
 *   taskENTER_CRITICAL_FROM_ISR / taskEXIT_CRITICAL_FROM_ISR
 *   ESP32 (ESP-IDF) 平台额外嵌入 portMUX 适配, 由 ESP-IDF 自带
 *   FreeRTOS 提供, 项目侧勿重复 vendor
 *   关键差异 (参考基准, 其他后端差异以此对齐):
 *   1. ISR 临界区用 taskENTER/EXIT_CRITICAL_FROM_ISR; ESP-IDF 下等价于
 *   portMUX 自旋锁 (portTICK_RATE_MS 1 ms tick), 退出自动让出;
 *   2. 互斥锁 xSemaphoreCreateMutex 自带优先级继承 (避免优先级反转),
 *   OSAL_MUTEX_PLAIN 的"二次获取阻塞"语义由本层自实现 (递归计数包装)
 *   确保 OSAL_ERR_TIMEOUT 一致返回;
 *   3. 信号量是计数的 — 本层用 posted 标志保证"多次 post 合并计数 ≤ 1"
 *   实现严格二值语义;
 *   4. 任务删除自身 vTaskDelete(NULL) 真返回 (与 ThreadX 不同),
 *   任务控制块/栈可被 idle 回收;
 *   5. ISR 出口 osal_yield_from_isr 调用 portYIELD_FROM_ISR 触发 PendSV;
 *   6. 栈水位依赖 configCHECK_FOR_STACK_OVERFLOW > 0; 无运行时栈高水位查询
 *   (FreeRTOS 不暴露), 仅靠 overflow hook 检测.
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
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* 队列 / 信号量内部存储 */
/* -------------------------------------------------------------------------- */
struct osal_mutex
{
    SemaphoreHandle_t handle;  /**< FreeRTOS 句柄 */
    StaticSemaphore_t sem_buf; /**< 静态存储 */
    osal_mutex_type_t type;    /**< 互斥锁类型 */
};

/**
 * @brief 初始化互斥锁
 * @param[in] mutex 互斥锁指针
 * @param[in] type 互斥锁类型
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

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE, "OSAL_MUTEX_STORAGE_SIZE too small");

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

/* -------------------------------------------------------------------------- */
/* Spinlock (内核临界区 API 即 FreeRTOS 的自旋锁等价物) */
/* -------------------------------------------------------------------------- */
/**
 * @details FreeRTOS-Kernel 上游不对外提供独立的自旋锁对象
 *          (lib/freeRTOS 下无 spinlock.h / portMUX_TYPE), 其等价物就是
 *          嵌套临界区 API:
 *          - CONFIG_OSAL_SPINLOCK_IRQ_DISABLE (默认), 非 ESP:
 *            taskENTER_CRITICAL()/taskEXIT_CRITICAL() — port 内部用
 *            uxCriticalNesting 计数, 天然可重入, 故本层结构体为空;
 *          - ESP_PLATFORM (ESP-IDF): portMUX_TYPE 就是官方真自旋锁
 *            (自带嵌套计数 + owner 校验), ISR 与线程上下文分别走
 *            portENTER_CRITICAL_ISR / taskENTER_CRITICAL;
 *          - CONFIG_OSAL_SPINLOCK_ATOMIC, 非 ESP: 上游无对应物,
 *            用原子 test-and-set 忙等 (仅适合 SMP; M0/M0+ 无
 *            LDREX/STREX, 会退化到 libatomic 软件实现)。
 * @note ESP 下无论选哪种模式都复用 portMUX_TYPE: 它已是
 *       "TAS + 关中断" 的完整自旋锁, 另搞一套原子标志反而丢掉
 *       owner 校验与 ISR 安全。旧写法在 ATOMIC 分支里引用了只在
 *       IRQ_DISABLE 分支定义的 mux 成员, ESP + ATOMIC 组合无法编译。
 */
struct osal_spinlock
{
#ifdef ESP_PLATFORM
    portMUX_TYPE mux; /**< ESP-IDF 原生自旋锁 (两种模式共用) */
#elif !defined(CONFIG_OSAL_SPINLOCK_IRQ_DISABLE)
    volatile int locked; /**< 非 ESP 原子模式: TAS 标志 (0=空闲, 1=持有) */
#endif
};

_Static_assert(sizeof(struct osal_spinlock) <= OSAL_SPINLOCK_STORAGE_SIZE, "osal_freertos: OSAL_SPINLOCK_STORAGE_SIZE too small");

/**
 * @brief 初始化自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 */
int osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(&lock->mux);
#elif !defined(CONFIG_OSAL_SPINLOCK_IRQ_DISABLE)
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
#endif
    /* 非 ESP 的临界区模式无状态可初始化 (嵌套计数在 port 全局) */
    return OSAL_OK;
}

/**
 * @brief 进入自旋锁 (ISR 安全, 禁止睡眠)
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 * @warning 持锁区间内中断/抢占已被屏蔽, 禁止调用任何可能阻塞的
 *          API (互斥锁/信号量/队列/延时)。
 */
int osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef ESP_PLATFORM
    if (osal_in_isr())
        portENTER_CRITICAL_ISR(&lock->mux);
    else
        taskENTER_CRITICAL(&lock->mux);
#elif defined(CONFIG_OSAL_SPINLOCK_IRQ_DISABLE)
    taskENTER_CRITICAL(); /* 全局临界区, port 自带嵌套计数 */
#else
    /* TAS 成功时已将 locked 置 1, 无需额外 store */
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
        ;
#endif
    return OSAL_OK;
}

/**
 * @brief 退出自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 OSAL_OK; lock 为空返回 OSAL_ERR_INVAL
 * @note 临界区/portMUX 模式下嵌套计数归零才会重新开中断, 因此
 *       lock/unlock 必须严格成对。
 */
int osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock)
        return OSAL_ERR_INVAL;
#ifdef ESP_PLATFORM
    if (osal_in_isr())
        portEXIT_CRITICAL_ISR(&lock->mux);
    else
        taskEXIT_CRITICAL(&lock->mux);
#elif defined(CONFIG_OSAL_SPINLOCK_IRQ_DISABLE)
    taskEXIT_CRITICAL();
#else
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
#endif
    return OSAL_OK;
}

/**
 * @brief 检查自旋锁是否被持有
 * @param[in] lock 自旋锁指针
 * @return true 已持有; false 未持有、lock 为空或后端不暴露此状态
 * @note 仅作诊断用: 临界区/portMUX 模式不对外暴露嵌套计数,
 *       统一返回 false; 只有非 ESP 的原子模式能给出真实值。
 */
MINI_UNUSED MINI_STATIC_INLINE bool osal_spinlock_is_locked(struct osal_spinlock* lock)
{
#if !defined(ESP_PLATFORM) && !defined(CONFIG_OSAL_SPINLOCK_IRQ_DISABLE)
    if (!lock)
        return false;
    return __atomic_load_n(&lock->locked, __ATOMIC_ACQUIRE) != 0;
#else
    MINI_UNUSED_PARAM(lock);
    return false;
#endif
}

/* -------------------------------------------------------------------------- */
/* 槽位池 (每池独立临界区锁) */
/* -------------------------------------------------------------------------- */

#ifdef ESP_PLATFORM
_Static_assert(sizeof(portMUX_TYPE) <= OSAL_POOL_MUX_STORAGE_SIZE, "OSAL_POOL_MUX_STORAGE_SIZE too small for portMUX_TYPE");
/**
 * @param[in] pool 池指针
 * @return 临界区锁指针
 * @brief ESP平台使用portMUX_TYPE作为临界区锁 把字节缓冲区指针强制转换成 portMUX_TYPE 结构体指针,
 * 然后传给 portENTER_CRITICAL_ISR 或 taskENTER_CRITICAL
 */
MINI_STATIC_INLINE portMUX_TYPE* osal_pool_mux(osal_pool_t* pool) { return (portMUX_TYPE*)pool->mux_storage; }
#else
typedef int portMUX_TYPE;
#endif

/**
 * @brief 进入槽位池临界区 (ESP 用 portMUX, 其它平台用 taskENTER_CRITICAL)
 * @param[in] pool 槽位池指针
 */
MINI_STATIC_INLINE void osal_pool_lock(osal_pool_t* pool)
{
#ifdef ESP_PLATFORM
    portMUX_TYPE* mux = osal_pool_mux(pool);
    if (osal_in_isr())
        portENTER_CRITICAL_ISR(mux);
    else
        taskENTER_CRITICAL(mux);
#else
    taskENTER_CRITICAL();
    MINI_UNUSED_PARAM(pool);
#endif
}

/**
 * @brief 退出槽位池临界区
 * @param[in] pool 槽位池指针
 */
MINI_STATIC_INLINE void osal_pool_unlock(osal_pool_t* pool)
{
#ifdef ESP_PLATFORM
    portMUX_TYPE* mux = osal_pool_mux(pool);
    if (osal_in_isr())
        portEXIT_CRITICAL_ISR(mux);
    else
        taskEXIT_CRITICAL(mux);
#else
    taskEXIT_CRITICAL();
    MINI_UNUSED_PARAM(pool);
#endif
}

/**
 * @brief 初始化槽位池
 * @param[in] pool 池
 * @param[in] used_slots 数组
 * @param[in] slot_count 数量
 * @return 0 或 OSAL_ERR_INVAL
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* used_slots, size_t slot_count)
{
    if (!pool || !used_slots || slot_count == 0)
        return OSAL_ERR_INVAL;

    pool->used_slots = used_slots;
    pool->slot_count = slot_count;

    for (size_t iter_index = 0; iter_index < slot_count; iter_index++)
        used_slots[iter_index] = 0;

#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(osal_pool_mux(pool));
#endif

    return 0;
}

/**
 * @brief 申请槽位
 * @param[in] pool 池
 * @return 索引或负值
 */
int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return OSAL_ERR_INVAL;
    osal_pool_lock(pool);

    int ret_idx = -1;
    for (size_t iter_index = 0; iter_index < pool->slot_count; iter_index++)
    {
        if (!pool->used_slots[iter_index])
        {
            pool->used_slots[iter_index] = 1;
            ret_idx = (int)iter_index;
            break;
        }
    }

    osal_pool_unlock(pool);
    return ret_idx;
}

/**
 * @brief 释放槽位
 * @param[in] pool 池
 * @param[in] slot_index 索引
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
 * @param[in] pool 池
 * @param[in] slot_index 索引
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
 * @param[in] s_mutex_pool 互斥锁池结构体指针
 * @param[in] s_mutex_used 互斥锁使用情况指针
 * @param[in] s_mutex_pool_ctrl 互斥锁池控制结构体指针
 */
static struct osal_mutex             s_mutex_pool[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_mutex_used[OSAL_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_mutex_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化静态互斥锁池
 * @details 上电时通过 mini_pre_execution 调用 osal_pool_init 初始化互斥锁池控制结构体
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void osal_mutex_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE));
}

/**
 * @brief xTaskGetTickCount 转 ms
 * @return 毫秒
 */
uint32_t osal_time_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

/**
 * @brief vTaskDelay
 * @param[in] ms 毫秒
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
        uint32_t          cycles = us * (configCPU_CLOCK_HZ / 1000000U);
        volatile uint32_t iter_index;
        for (iter_index = 0; iter_index < cycles; iter_index++)
            MINI_UNUSED_PARAM(iter_index);
    }
#endif
}

/**
 * @brief pdMS_TO_TICKS
 * @param[in] ms 毫秒
 * @return tick
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms) { return pdMS_TO_TICKS(ms); }

/**
 * @brief 超时转 tick
 * @param[in] timeout_ms 毫秒
 * @return tick
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return portMAX_DELAY;
    return pdMS_TO_TICKS(timeout_ms);
}

/* -------------------------------------------------------------------------- */
/* 内存 */
/* -------------------------------------------------------------------------- */
/**
 * @brief calloc
 * @param[in] count 数量
 * @param[in] size 大小
 * @return 指针
 */
void* osal_calloc(size_t count, size_t size) { return calloc(count, size); }

/**
 * @brief free
 * @param[in] ptr 指针
 * @return OSAL_OK
 */
int osal_free(void* ptr)
{
    free(ptr);
    return OSAL_OK;
}

/* -------------------------------------------------------------------------- */
/* 互斥锁 */
/* -------------------------------------------------------------------------- */
/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
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

    struct osal_mutex* mutex_obj = &s_mutex_pool[index];
    if (osal_mutex_init(mutex_obj, type) != OSAL_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, index));
        return OSAL_ERR_NOMEM;
    }
    *out = (struct osal_mutex*)mutex_obj;
    return OSAL_OK;
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage, size_t storage_size, osal_mutex_type_t type)
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
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_recursive(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE); }

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_plain(struct osal_mutex** out) { return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN); }

/**
 * @brief FreeRTOS 静态/池化互斥锁
 * @param[out] out 等见签名
 * @return OSAL_OK 或错误码
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

/**
 * @brief vSemaphoreDelete + 释放池槽
 * @param[in] mutex 锁
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
        MINI_IGNORE_RESULT(osal_pool_release(&s_mutex_pool_ctrl, (int)idx));
    }
}

/**
 * @brief xSemaphoreTake
 * @param[in] mutex 锁
 * @param[in] timeout_ms 超时
 * @return OSAL_OK 或 TIMEOUT
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex || !mutex->handle)
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
 * @param[in] mutex 锁
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

/* -------------------------------------------------------------------------- */
/* 二值信号量 */
/* -------------------------------------------------------------------------- */
struct osal_sem
{
    SemaphoreHandle_t handle;    /**< FreeRTOS 句柄 */
    StaticSemaphore_t sem_buf;   /**< 静态存储 */
    bool              from_pool; /**< 是否来自静态池 */
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE, "OSAL_SEM_STORAGE_SIZE too small");

/**
 * @brief 静态二值信号量池
 * @param[in] s_sem_pool 信号量池结构体指针
 * @param[in] s_sem_used 信号量池占用数组
 * @param[in] s_sem_pool_ctrl 信号量池控制结构体指针
 */
static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
/**
 * @brief 静态二值信号量池占用数组
 * @param[in] s_sem_pool 信号量池结构体指针
 * @param[in] s_sem_used 信号量池占用数组
 * @param[in] s_sem_pool_ctrl 信号量池控制结构体指针
 */
static uint8_t s_sem_used[OSAL_SEM_POOL_SIZE] MINI_ALIGNED(4);
/**
 * @brief 静态二值信号量池控制
 * @param[in] s_sem_pool 信号量池结构体指针
 * @param[in] s_sem_used 信号量池占用数组
 * @param[in] s_sem_pool_ctrl 信号量池控制结构体指针
 */
static osal_pool_t s_sem_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化二值信号量池
 * @details 上电时通过 mini_pre_execution 调用 osal_pool_init 初始化二值信号量池
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_SEM_POOL) static void osal_sem_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_sem_pool_ctrl, s_sem_used, OSAL_SEM_POOL_SIZE));
}

/**
 * @brief 初始化二值信号量
 * @param[in] sem 二值信号量指针
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
 * @param[out] out 输出
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
        MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, idx));
        return OSAL_ERR_NOMEM;
    }

    sem->from_pool = true;
    *out = sem;
    return 0;
}

/**
 * @brief 静态二值信号量
 * @param[out] out 输出
 * @param[in] storage 存储
 * @param[in] storage_size 大小
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
 * @param[in] sem 信号量
 */
void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem || osal_in_isr())
        return;

    /* 只有 handle 非空 (合法创建的信号量) 才允许销毁底层 FreeRTOS 信号量;
     * handle 为空的池外指针视为非法输入, 不触碰其字段. */
    if (sem->handle == NULL)
        return;

    /**< 先销毁底层信号量 */
    vSemaphoreDelete(sem->handle);
    sem->handle = NULL;

    /**< 再判断是否属于全局信号量池: 仅池内对象归还池槽, 静态信号量不归还 */
    if (sem >= s_sem_pool && sem < &s_sem_pool[OSAL_SEM_POOL_SIZE])
    {
        size_t idx = (size_t)(sem - s_sem_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_sem_pool_ctrl, (int)idx));
    }
}

/**
 * @brief xSemaphoreTake
 * @param[in] sem 信号量
 * @param[in] timeout_ms 超时
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
 * @param[in] sem 信号量
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
 * @param[in] px_yield_required 是否需要切换
 * @param[in] higher_prio_woken 更高优先级唤醒
 * @return void
 * @details 通知ISR上下文切换时, 使用 osal_note_isr_yield 通知ISR上下文切换
 */
MINI_STATIC_INLINE void osal_note_isr_yield(bool* px_yield_required, BaseType_t higher_prio_woken)
{
    if (px_yield_required != NULL && higher_prio_woken == pdTRUE)
        *px_yield_required = true;
}

/**
 * @brief xSemaphoreGiveFromISR
 * @param[in] sem 信号量
 * @param[in] px_yield_required yield
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
 * @param[in] yield_required 是否 yield
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
static StackType_t  s_idle_stack[configMINIMAL_STACK_SIZE]; /**<空闲任务栈*/
static StaticTask_t s_idle_tcb;                             /**<空闲任务TCB*/

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer, StackType_t** ppxIdleTaskStackBuffer, uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/*
 * 定时器服务任务静态内存回调 — configSUPPORT_STATIC_ALLOCATION && configUSE_TIMERS
 * 时 FreeRTOS 强制要求应用提供, 否则链接期缺 vApplicationGetTimerTaskMemory。
 * 事件组抽象 (CONFIG_OSAL_EVENT) 会随 xTimerPendFunctionCall 打开 configUSE_TIMERS,
 */
#if (configUSE_TIMERS == 1)
static StackType_t  s_timer_stack[configTIMER_TASK_STACK_DEPTH]; /**<定时器服务任务栈*/
static StaticTask_t s_timer_tcb;                                 /**<定时器服务任务TCB*/

void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer, StackType_t** ppxTimerTaskStackBuffer, uint32_t* pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &s_timer_tcb;
    *ppxTimerTaskStackBuffer = s_timer_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS */

/**
 * @brief FreeRTOS 栈溢出钩子 (configCHECK_FOR_STACK_OVERFLOW=2 时必需)
 */
void vApplicationStackOverflowHook(TaskHandle_t x_task, char* pcTaskName)
{
    MINI_UNUSED_PARAM(x_task);
    MINI_UNUSED_PARAM(pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
#endif /* !ESP_PLATFORM */

/**
 * @brief 将栈字节数换算为 FreeRTOS StackType_t 个数 (向上取整)
 * @param[in] stack_bytes 栈大小 (字节)
 * @return StackType_t 元素个数
 */
MINI_STATIC_INLINE uint32_t osal_stack_words(uint32_t stack_bytes)
{
    return (stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t); /**< 向上取整, 确保栈大小足够 */
}

/**
 * @brief 钳位到 FreeRTOS 合法优先级 [0, configMAX_PRIORITIES)
 */
MINI_STATIC_INLINE UBaseType_t osal_clamp_task_priority(uint32_t priority)
{
    if (priority >= (uint32_t)configMAX_PRIORITIES)
        return (UBaseType_t)(configMAX_PRIORITIES - 1U);
    return (UBaseType_t)priority;
}

/**
 * @brief xTaskCreate
 * @param[in] name 名
 * @param[in] stack_size 栈字节
 * @param[in] priority 优先级
 * @param[in] entry 入口
 * @param[in] param 参数
 * @param[in] core_id 核
 * @return OSAL_OK 或 NOMEM
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id)
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
    MINI_UNUSED_PARAM(core_id);
#endif

    TaskHandle_t handle = NULL;
    BaseType_t   ret = xTaskCreate(entry, name, osal_stack_words(stack_size), param, osal_clamp_task_priority(priority), &handle);
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_NOMEM;
}

/**
 * @brief xTaskCreate 返回句柄
 * @param[in] name 名
 * @param[in] stack_size 栈
 * @param[in] priority 优先级
 * @param[in] entry 入口
 * @param[in] param 参数
 * @param[in] core_id 核
 * @param[out] out_handle 输出
 * @return 0 或 NOMEM
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id,
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
    MINI_UNUSED_PARAM(core_id);
#endif

    TaskHandle_t handle = NULL;
    BaseType_t   ret = xTaskCreate(entry, name, osal_stack_words(stack_size), param, osal_clamp_task_priority(priority), &handle);
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
 * @param[in] task 句柄
 */
void osal_task_delete(osal_task_handle_t task) { vTaskDelete((TaskHandle_t)task); }

/**
 * @brief 启动 FreeRTOS 调度器
 * @details 转发 vTaskStartScheduler(); 应在所有 osal_task_create() 之后调用,
 *          正常情况下永不返回 (控制权交给内核调度器).
 */
void osal_scheduler_start(void) { vTaskStartScheduler(); }

/**
 * @brief eTaskGetState != eDeleted
 * @param[in] task 句柄
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
 * @param[in] task 句柄
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
 * @param[out] task 句柄
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
 * @param[in] queue_len 长度
 * @param[in] item_size 大小
 * @return 句柄
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size) { return (osal_queue_handle_t)xQueueCreate(queue_len, item_size); }

/**
 * @brief vQueueDelete
 * @param[in] queue 句柄
 */
void osal_queue_delete(osal_queue_handle_t queue) { vQueueDelete((QueueHandle_t)queue); }

/**
 * @brief xQueueSend 任务态
 * @param[in] queue 句柄
 * @param[in] item 数据
 * @param[in] timeout_ms 超时
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
 * @param[in] queue 队列句柄
 * @param[in] item 待发送数据
 * @param[in] px_yield_required 输出是否需要 ISR 末尾 yield
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
 * @param[out] queue 句柄
 * @param[out] item 缓冲
 * @param[in] timeout_ms 超时
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
 * @param[out] queue 队列句柄
 * @param[out] item 接收缓冲区
 * @param[out] px_yield_required 输出是否需要 ISR 末尾 yield
 * @return true 成功
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/* -------------------------------------------------------------------------- */
/* 事件组 (CONFIG_OSAL_EVENT 门控, 映射 xEventGroup*) */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_OSAL_EVENT
/* Kconfig 的 CONFIG_OSAL_EVENT 会 select FREERTOS_EVENT_GROUPS
 * (并连带 FREERTOS_USE_TIMERS); 两者不一致时 event_groups.c 未编入内核库,
 * 下面全部符号会在链接期缺失, 不如在这里直接 fail-fast。 */
#if (configUSE_EVENT_GROUPS != 1)
#error "osal_freertos: CONFIG_OSAL_EVENT requires CONFIG_FREERTOS_EVENT_GROUPS"
#endif
/* osal_event_set_from_isr() 走 xEventGroupSetBitsFromISR(), 它在 event_groups.c
 * 里被 (INCLUDE_xTimerPendFunctionCall && configUSE_TIMERS) 包住:
 * ISR 里不能阻塞而唤醒等待者要拿事件组锁, 因此内核把置位动作
 * pend 给软件定时器守护任务执行。FreeRTOSConfig.h 已随事件组一起打开。 */
#if (INCLUDE_xTimerPendFunctionCall != 1) || (configUSE_TIMERS != 1)
#error "osal_freertos: osal_event_set_from_isr requires INCLUDE_xTimerPendFunctionCall and configUSE_TIMERS"
#endif

/**
 * @brief OSAL 事件组对象
 * @details 句柄 + 静态存储内嵌 (xEventGroupCreateStatic), 与互斥锁/二值
 *          信号量的做法一致, 池分配与调用方静态存储共用同一条创建路径,
 *          全程不碰 FreeRTOS 堆。
 *          mode / auto_clear 必须在创建期存下来: FreeRTOS 把 AND/OR
 *          (xWaitForAllBits) 与自动清位 (xClearOnExit) 做成了等待期参数,
 *          而抽象层按 mini-os 的创建期语义固定, 所以等待时再回填。
 */
struct osal_event
{
    EventGroupHandle_t handle;     /**< FreeRTOS 事件组句柄 */
    StaticEventGroup_t buf;        /**< 静态存储 (xEventGroupCreateStatic 用) */
    osal_event_mode_t  mode;       /**< 创建期固定的 AND/OR 模式 */
    bool               auto_clear; /**< 创建期固定的自动消费标志 */
    bool               from_pool;  /**< 是否来自静态池 */
};

_Static_assert(sizeof(struct osal_event) <= OSAL_EVENT_STORAGE_SIZE, "OSAL_EVENT_STORAGE_SIZE too small");

/**
 * @brief 静态事件组池
 */
static struct osal_event             s_event_pool[OSAL_EVENT_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t                       s_event_used[OSAL_EVENT_POOL_SIZE] MINI_ALIGNED(4);
static osal_pool_t s_event_pool_ctrl MINI_ALIGNED(4);

/**
 * @brief 初始化事件组池
 * @details 上电时通过 mini_pre_execution 调用 osal_pool_init 初始化事件组池
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_EVENT_POOL) static void osal_event_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_event_pool_ctrl, s_event_used, OSAL_EVENT_POOL_SIZE));
}

/**
 * @brief 校验标志掩码 (非 0 且不越出 OSAL 可用位区)
 * @param[in] bits 待校验掩码
 * @return true 合法
 * @details 本后端是 24 位上限的来源: bit24..31 是内核控制位
 *          (eventEVENT_BITS_CONTROL_BYTES), xEventGroupWaitBits 入口的
 *          configASSERT 会直接拦下, 本层提前报 OSAL_ERR_INVAL。
 */
MINI_STATIC_INLINE bool osal_event_bits_valid(uint32_t bits) { return (bits != 0U) && ((bits & ~OSAL_EVENT_MASK) == 0U); }

/**
 * @brief 在调用方存储上初始化事件组
 * @param[in] ev 事件组对象
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后是否自动消费已满足的位
 * @return OSAL_OK; ev 为空或 mode 非法返回 OSAL_ERR_INVAL;
 *         内核创建失败返回 OSAL_ERR_NOMEM
 */
static int osal_event_init(struct osal_event* ev, osal_event_mode_t mode, bool auto_clear)
{
    if (!ev)
        return OSAL_ERR_INVAL;
    if (mode != OSAL_EVENT_OR && mode != OSAL_EVENT_AND)
        return OSAL_ERR_INVAL;

    ev->handle = xEventGroupCreateStatic(&ev->buf);
    if (ev->handle == NULL)
        return OSAL_ERR_NOMEM;

    /**< 内核初始标志就是 0; mode / auto_clear 存下来给 wait 用 */
    ev->mode = mode;
    ev->auto_clear = auto_clear;
    return OSAL_OK;
}

/**
 * @brief 池化事件组
 * @param[out] out 输出
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后自动消费已满足的位
 * @return OSAL_OK 或错误码
 */
int osal_event_create(struct osal_event** out, osal_event_mode_t mode, bool auto_clear)
{
    if (!out)
        return OSAL_ERR_INVAL;

    int idx = osal_pool_claim(&s_event_pool_ctrl);
    if (idx < 0)
        return OSAL_ERR_NOMEM;

    struct osal_event* ev = &s_event_pool[idx];
    int                rc = osal_event_init(ev, mode, auto_clear);
    if (rc != OSAL_OK)
    {
        MINI_IGNORE_RESULT(osal_pool_release(&s_event_pool_ctrl, idx));
        return rc;
    }

    ev->from_pool = true;
    *out = ev;
    return OSAL_OK;
}

/**
 * @brief 静态存储事件组
 * @param[out] out 输出
 * @param[in] storage 存储
 * @param[in] storage_size 大小
 * @param[in] mode AND/OR 等待模式
 * @param[in] auto_clear 等待成功后自动消费已满足的位
 * @return OSAL_OK 或错误码
 */
int osal_event_create_static(struct osal_event** out, void* storage, size_t storage_size, osal_event_mode_t mode, bool auto_clear)
{
    if (!out || !storage || storage_size < sizeof(struct osal_event))
        return OSAL_ERR_INVAL;

    struct osal_event* ev = (struct osal_event*)storage;
    int                rc = osal_event_init(ev, mode, auto_clear);
    if (rc != OSAL_OK)
        return rc;

    ev->from_pool = false;
    *out = ev;
    return OSAL_OK;
}

/**
 * @brief 销毁事件组并归还池槽
 * @param[in] ev 事件组
 * @details vEventGroupDelete 会自行把阻塞在该组上的任务挖到 pending ready
 *          队列, 使其带当前标志从 xEventGroupWaitBits 返回, 因此不存在
 *          遗留等待者 (与 mini-os 后端需要自己判忙不同)。
 */
void osal_event_destroy(struct osal_event* ev)
{
    if (!ev || osal_in_isr())
        return;

    if (ev->handle != NULL)
    {
        vEventGroupDelete(ev->handle);
        ev->handle = NULL;
    }

    if (ev->from_pool && ev >= s_event_pool && ev < &s_event_pool[OSAL_EVENT_POOL_SIZE])
    {
        int idx = (int)(ev - s_event_pool);
        MINI_IGNORE_RESULT(osal_pool_release(&s_event_pool_ctrl, idx));
    }
}

/**
 * @brief 置位事件标志 (task 上下文)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0, 仅 bit0..23)
 * @return OSAL_OK 或错误码
 * @details xEventGroupSetBits 本身就是 OR 并入, 与抽象层契约一致;
 *          它可能阻塞 (内部要为唤醒任务拿事件组锁), 故仅 task 上下文。
 * @note 本 API 无失败回传 (返回的是置位后的标志值), 只能以句柄有效性
 *       判定成败; 内核异常已由 configASSERT 兼顾。
 */
int osal_event_set(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !ev->handle || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    MINI_IGNORE_RESULT(xEventGroupSetBits(ev->handle, (EventBits_t)bits));
    return OSAL_OK;
}

/**
 * @brief 置位事件标志 (ISR 上下文, 不内部 yield)
 * @param[in] ev 事件组
 * @param[in] bits 要置位的掩码 (> 0, 仅 bit0..23)
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return OSAL_OK; 参数非法 OSAL_ERR_INVAL; 定时器 pend 队列满 OSAL_ERR_NOMEM
 * @details xEventGroupSetBitsFromISR 不直接改标志, 而是把置位动作 pend 给
 *          软件定时器守护任务, 因此实际生效时机在守护任务被调度到时,
 *          不是 ISR 返回前; 守护任务优先级 configTIMER_TASK_PRIORITY 低于
 *          EventBus (30), 对延迟敏感的场景请改用队列/信号量的 FromISR。
 */
int osal_event_set_from_isr(struct osal_event* ev, uint32_t bits, bool* px_yield_required)
{
    if (!ev || !ev->handle || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xEventGroupSetBitsFromISR(ev->handle, (EventBits_t)bits, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    /**< pdFAIL 只有一种成因: pend 到守护任务的命令队列已满 */
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_NOMEM;
}

/**
 * @brief 清除事件标志
 * @param[in] ev 事件组
 * @param[in] bits 要清除的掩码 (> 0, 仅 bit0..23)
 * @return OSAL_OK 或错误码
 * @details xEventGroupClearBits 在临界区内改标志, 不阻塞不唤醒,
 *          因此 task 上下文可直接调用; ISR 下必须走 FromISR 变体
 *          (它同样是 pend 给定时器守护任务执行, 故清除也是延迟生效)。
 */
int osal_event_clear(struct osal_event* ev, uint32_t bits)
{
    if (!ev || !ev->handle || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;

    if (osal_in_isr())
    {
        MINI_IGNORE_RESULT(xEventGroupClearBitsFromISR(ev->handle, (EventBits_t)bits));
        return OSAL_OK;
    }
    MINI_IGNORE_RESULT(xEventGroupClearBits(ev->handle, (EventBits_t)bits));
    return OSAL_OK;
}

/**
 * @brief 读取当前事件标志 (不阻塞、不消费)
 * @param[in] ev 事件组
 * @param[out] out_bits 回传当前标志 (可为 NULL, 则仅做存在性检查)
 * @return OSAL_OK 或错误码
 * @details xEventGroupGetBits 就是 xEventGroupClearBits(h, 0), 会进临界区
 *          因此仅 task 可用; ISR 下改走 xEventGroupGetBitsFromISR。
 */
int osal_event_get(struct osal_event* ev, uint32_t* out_bits)
{
    if (!ev || !ev->handle)
        return OSAL_ERR_INVAL;
    if (out_bits == NULL)
        return OSAL_OK;

    EventBits_t cur = osal_in_isr() ? xEventGroupGetBitsFromISR(ev->handle) : xEventGroupGetBits(ev->handle);
    /**< 内核返回值的最高 8 位是控制位, 对上层一律屏蔽 */
    *out_bits = (uint32_t)cur & OSAL_EVENT_MASK;
    return OSAL_OK;
}

/**
 * @brief 等待事件标志
 * @param[in] ev 事件组
 * @param[in] bits 等待的掩码 (> 0, 仅 bit0..23)
 * @param[in] timeout_ms 超时毫秒 (0 = 不阻塞, OSAL_WAIT_FOREVER = 永久)
 * @param[out] out_bits 回传实际已置位的相关位 (可为 NULL)
 * @return 满足 OSAL_OK; 未满足/超时 OSAL_ERR_TIMEOUT; 参数非法 OSAL_ERR_INVAL
 * @details 创建期存的 mode / auto_clear 在这里转成内核参数
 *          xWaitForAllBits / xClearOnExit, 因此四后端行为一致。
 *          xEventGroupWaitBits 无论成败都返回"判定那一刻的标志值"
 *          (自动清位发生在取值之后), 所以用返回值的位测试定成败:
 *          AND 要求 bits 全部命中, OR 只要命中任一位。
 * @warning timeout_ms 非 0 时内核 configASSERT 要求调度器未被挂起;
 *          且 xTicksToWait 为 0 才是非阻塞语义。
 */
int osal_event_wait(struct osal_event* ev, uint32_t bits, uint32_t timeout_ms, uint32_t* out_bits)
{
    if (!ev || !ev->handle || !osal_event_bits_valid(bits))
        return OSAL_ERR_INVAL;
    if (osal_in_isr())
        return OSAL_ERR_ISR;

    bool        wait_all = (ev->mode == OSAL_EVENT_AND);
    EventBits_t got = xEventGroupWaitBits(ev->handle, (EventBits_t)bits, ev->auto_clear ? pdTRUE : pdFALSE, wait_all ? pdTRUE : pdFALSE,
                                          osal_timeout_to_ticks(timeout_ms));

    uint32_t relevant = (uint32_t)got & bits;
    if (out_bits != NULL)
        *out_bits = relevant;

    if (wait_all)
        return (relevant == bits) ? OSAL_OK : OSAL_ERR_TIMEOUT;
    return (relevant != 0U) ? OSAL_OK : OSAL_ERR_TIMEOUT;
}
#endif /* CONFIG_OSAL_EVENT */

/**
 * @brief 弱符号硬件安全关断 (板级未覆盖时触发 trap)
 */
MINI_WEAK void safety_hardware_shutdown(void) { MINI_TRAP(); }

/**
 * @brief 弱符号 Panic 安全互锁 (板级可覆盖: 喂狗、切断执行器等)
 */
MINI_WEAK void osal_panic_interlock(void) { /* 板级可覆盖: 喂硬件看门狗, 切断执行器供电, 等待复位 */ }

/**
 * @brief 日志
 * @param[in] level 级别
 * @param[in] tag 标签
 * @param[in] fmt 格式
 * @param ... 参数
 */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    MINI_UNUSED_PARAM(level);
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
 * @param[in] fmt 格式
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
 * @param[in] file 文件
 * @param[in] line 行
 * @param[in] fmt 格式
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
