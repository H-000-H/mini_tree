/**
 * @file mini_backend_freertos.c
 * @author H-000-H
 * @brief FreeRTOS 后端实现 (互斥锁 / 信号量 / 队列 / 任务 / 内存 / 调度器启动)
 * @note  优先级数字越大越优先 (与 mini-os/RT-Thread 相反), 故只钳位不翻转
 * @note  *_from_isr 系列通过 px_yield_required 上报, 绝不内部 yield; 互斥锁只有静态存储一种形态
 * @note  本文件另承载 FreeRTOS 静态分配/栈溢出检查所需的 3 个应用回调, 必须与后端放一起否则链接缺符号
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#if defined(CONFIG_OS_FREERTOS)

#define ALLOW_HEAP_ALLOC

#include "mini_backend.h"

#include "board_config.h"
#include "compiler_compat.h"
#include "config.h"
#include "hal_amp.h"
#include "mini_slot.h"
#include "status.h"
#include "system_log.h"
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
#include <stdlib.h>

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* 公共换算                                                                    */
/* -------------------------------------------------------------------------- */
MINI_STATIC_INLINE TickType_t mini_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == MINI_WAIT_FOREVER)
        return portMAX_DELAY;
    return pdMS_TO_TICKS(timeout_ms);
}

/* 只有内核回报"唤醒了更高优先级任务"时才置位 */
MINI_STATIC_INLINE void mini_note_isr_yield(bool* px_yield_required, BaseType_t higher_prio_woken)
{
    if (px_yield_required != NULL && higher_prio_woken == pdTRUE)
        *px_yield_required = true;
}

/* -------------------------------------------------------------------------- */
/* 互斥锁 (内核互斥量, 带优先级继承)                                            */
/* -------------------------------------------------------------------------- */
typedef enum
{
    MINI_MUTEX_RECURSIVE = 0,
    MINI_MUTEX_PLAIN = 1,
} mini_mutex_type_t;

struct mini_mutex
{
    SemaphoreHandle_t handle;
    StaticSemaphore_t sem_buf;
    mini_mutex_type_t type;
};

_Static_assert(sizeof(struct mini_mutex) <= MINI_MUTEX_STORAGE_SIZE, "mini_backend_freertos: MINI_MUTEX_STORAGE_SIZE too small");

static mt_err_t mini_mutex_init(struct mini_mutex* mutex, mini_mutex_type_t type)
{
    if (!mutex)
        return MINI_ERR_INVAL;

    mutex->type = type;
    if (type == MINI_MUTEX_RECURSIVE)
        mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&mutex->sem_buf);
    else if (type == MINI_MUTEX_PLAIN)
        mutex->handle = xSemaphoreCreateMutexStatic(&mutex->sem_buf);
    else
        return MINI_ERR_INVAL;

    return mutex->handle ? MINI_OK : MINI_ERR_NOMEM;
}

static mt_err_t mini_mutex_create_static_typed(mini_mutex_t** out, void* storage, size_t storage_size, mini_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct mini_mutex))
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    *out = NULL;
    struct mini_mutex* mutex = (struct mini_mutex*)storage;
    if (mini_mutex_init(mutex, type) != MINI_OK)
        return MINI_ERR_NOMEM;

    *out = (mini_mutex_t*)mutex;
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

/* 池化互斥锁: 给拿不出静态存储的调用方 (lwIP sys_mutex_new) 用 */
static struct mini_mutex s_mutex_pool[MINI_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t           s_mutex_used[MINI_MUTEX_POOL_SIZE] MINI_ALIGNED(4);
static mini_slot_t       s_mutex_pool_ctrl MINI_ALIGNED(4);

mini_pre_execution(MINI_PRE_EXEC_PRIO_RES_POOL) static void mini_mutex_pool_boot(void)
{
    MINI_IGNORE_RESULT(mini_slot_init(&s_mutex_pool_ctrl, s_mutex_used, MINI_MUTEX_POOL_SIZE));
}

mt_err_t mini_mutex_create(mini_mutex_t** out)
{
    if (!out)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;
    *out = NULL;

    int idx = mini_slot_claim(&s_mutex_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    if (mini_mutex_init(&s_mutex_pool[idx], MINI_MUTEX_PLAIN) != MINI_OK)
    {
        MINI_IGNORE_RESULT(mini_slot_release(&s_mutex_pool_ctrl, idx));
        return MINI_ERR_NOMEM;
    }

    *out = (mini_mutex_t*)&s_mutex_pool[idx];
    return MINI_OK;
}

mt_err_t mini_mutex_lock(mini_mutex_t* mtx, uint32_t timeout_ms)
{
    if (!mtx)
        return MINI_ERR_INVAL;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    if (!mutex->handle)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    TickType_t ticks = mini_timeout_to_ticks(timeout_ms);
    if (mutex->type == MINI_MUTEX_RECURSIVE)
        return xSemaphoreTakeRecursive(mutex->handle, ticks) == pdTRUE ? MINI_OK : MINI_ERR_TIMEOUT;
    return xSemaphoreTake(mutex->handle, ticks) == pdTRUE ? MINI_OK : MINI_ERR_TIMEOUT;
}

mt_err_t mini_mutex_unlock(mini_mutex_t* mtx)
{
    if (!mtx)
        return MINI_ERR_INVAL;

    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    if (!mutex->handle)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR; /* 中断中不允许释放 */

    if (mutex->type == MINI_MUTEX_RECURSIVE)
        return xSemaphoreGiveRecursive(mutex->handle) == pdTRUE ? MINI_OK : MINI_ERR_IO;
    return xSemaphoreGive(mutex->handle) == pdTRUE ? MINI_OK : MINI_ERR_IO;
}

void mini_mutex_destroy(mini_mutex_t* mtx)
{
    if (!mtx || hal_is_in_isr())
        return;

    /* handle 为空视为非法输入, 不触碰其字段 */
    struct mini_mutex* mutex = (struct mini_mutex*)mtx;
    if (mutex->handle == NULL)
        return;

    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;

    /* 仅池内对象归还槽位; 静态存储的对象不在池内 */
    if (mutex >= s_mutex_pool && mutex < &s_mutex_pool[MINI_MUTEX_POOL_SIZE])
        MINI_IGNORE_RESULT(mini_slot_release(&s_mutex_pool_ctrl, (int)(mutex - s_mutex_pool)));
}

/* -------------------------------------------------------------------------- */
/* 二值信号量                                                                  */
/* -------------------------------------------------------------------------- */
struct mini_sem
{
    SemaphoreHandle_t handle;
    StaticSemaphore_t sem_buf;
};

_Static_assert(sizeof(struct mini_sem) <= MINI_SEM_STORAGE_SIZE, "mini_backend_freertos: MINI_SEM_STORAGE_SIZE too small");

static mt_err_t mini_sem_init(struct mini_sem* sem)
{
    sem->handle = xSemaphoreCreateBinaryStatic(&sem->sem_buf);
    return sem->handle ? MINI_OK : MINI_ERR_NOMEM;
}

mt_err_t mini_sem_create_binary_static(mini_sem_t** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct mini_sem))
        return MINI_ERR_INVAL;

    struct mini_sem* sem = (struct mini_sem*)storage;
    if (mini_sem_init(sem) != MINI_OK)
        return MINI_ERR_NOMEM;

    *out = (mini_sem_t*)sem;
    return MINI_OK;
}

/* 池化信号量: 给拿不出静态存储的调用方 (lwIP sys_sem_new) 用 */
static struct mini_sem s_sem_pool[MINI_SEM_POOL_SIZE] MINI_ALIGNED(4);
static uint8_t         s_sem_used[MINI_SEM_POOL_SIZE] MINI_ALIGNED(4);
static mini_slot_t     s_sem_pool_ctrl MINI_ALIGNED(4);

mini_pre_execution(MINI_PRE_EXEC_PRIO_SEM_POOL) static void mini_sem_pool_boot(void)
{
    MINI_IGNORE_RESULT(mini_slot_init(&s_sem_pool_ctrl, s_sem_used, MINI_SEM_POOL_SIZE));
}

mt_err_t mini_sem_create_binary(mini_sem_t** out)
{
    if (!out)
        return MINI_ERR_INVAL;
    *out = NULL;

    int idx = mini_slot_claim(&s_sem_pool_ctrl);
    if (idx < 0)
        return MINI_ERR_NOMEM;

    struct mini_sem* sem = &s_sem_pool[idx];
    if (mini_sem_init(sem) != MINI_OK)
    {
        MINI_IGNORE_RESULT(mini_slot_release(&s_sem_pool_ctrl, idx));
        return MINI_ERR_NOMEM;
    }

    *out = (mini_sem_t*)sem;
    return MINI_OK;
}

mt_err_t mini_sem_wait(mini_sem_t* sem, uint32_t timeout_ms)
{
    if (!sem)
        return MINI_ERR_INVAL;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->handle || hal_is_in_isr())
        return MINI_ERR_ISR;

    return xSemaphoreTake(obj->handle, mini_timeout_to_ticks(timeout_ms)) == pdTRUE ? MINI_OK : MINI_ERR_TIMEOUT;
}

bool mini_sem_post(mini_sem_t* sem)
{
    if (!sem)
        return false;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->handle || hal_is_in_isr())
        return false;

    return xSemaphoreGive(obj->handle) == pdTRUE;
}

bool mini_sem_post_from_isr(mini_sem_t* sem, bool* px_yield_required)
{
    if (!sem)
        return false;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (!obj->handle)
        return false;

    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xSemaphoreGiveFromISR(obj->handle, &higher_prio_woken);
    mini_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

void mini_sem_destroy(mini_sem_t* sem)
{
    if (!sem || hal_is_in_isr())
        return;

    struct mini_sem* obj = (struct mini_sem*)sem;
    if (obj->handle == NULL)
        return;

    vSemaphoreDelete(obj->handle);
    obj->handle = NULL;

    /* 仅池内对象归还槽位 */
    if (obj >= s_sem_pool && obj < &s_sem_pool[MINI_SEM_POOL_SIZE])
        MINI_IGNORE_RESULT(mini_slot_release(&s_sem_pool_ctrl, (int)(obj - s_sem_pool)));
}

/* -------------------------------------------------------------------------- */
/* 定长消息队列 (描述符与缓冲区由内核堆分配)                                    */
/* -------------------------------------------------------------------------- */
mini_queue_t* mini_queue_create(size_t queue_len, size_t item_size)
{
    if (queue_len == 0 || item_size == 0)
        return NULL;
    return (mini_queue_t*)xQueueCreate(queue_len, item_size);
}

void mini_queue_delete(mini_queue_t* queue)
{
    if (!queue)
        return;
    vQueueDelete((QueueHandle_t)queue);
}

bool mini_queue_send(mini_queue_t* queue, const void* item, uint32_t timeout_ms)
{
    if (!queue || hal_is_in_isr())
        return false;

    return xQueueSend((QueueHandle_t)queue, item, mini_timeout_to_ticks(timeout_ms)) == pdTRUE;
}

bool mini_queue_send_from_isr(mini_queue_t* queue, const void* item, bool* px_yield_required)
{
    if (!queue)
        return false;

    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    mini_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

bool mini_queue_receive(mini_queue_t* queue, void* item, uint32_t timeout_ms)
{
    if (!queue || hal_is_in_isr())
        return false;

    return xQueueReceive((QueueHandle_t)queue, item, mini_timeout_to_ticks(timeout_ms)) == pdTRUE;
}

bool mini_queue_receive_from_isr(mini_queue_t* queue, void* item, bool* px_yield_required)
{
    if (!queue)
        return false;

    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    mini_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

/* -------------------------------------------------------------------------- */
/* 任务 (动态栈)                                                               */
/* -------------------------------------------------------------------------- */
/* 栈字节换算为 StackType_t 个数, 向上取整 */
MINI_STATIC_INLINE uint32_t mini_stack_words(uint32_t stack_bytes) { return (stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t); }

/* 只钳位, 不翻转方向: FreeRTOS 数字越大越优先 */
MINI_STATIC_INLINE UBaseType_t mini_clamp_task_priority(uint32_t priority)
{
    if (priority >= (uint32_t)configMAX_PRIORITIES)
        return (UBaseType_t)(configMAX_PRIORITIES - 1U);
    return (UBaseType_t)priority;
}

/* AMP 模式下 Core 1 没有 OS 调度器, 回退到 Core 0 并告警 */
MINI_STATIC_INLINE void mini_note_core_fallback(const char* name, int* core_id)
{
#if CONFIG_CPU_CORES > 1
    if (*core_id > 0)
    {
        MT_LOG_WARN("mini_backend", "task '%s' requested Core %d, but AMP Core 1 has no OS scheduler. Falling back to Core 0.", name, *core_id);
        *core_id = 0;
    }
#else
    MINI_UNUSED_PARAM(name);
    MINI_UNUSED_PARAM(core_id);
#endif
}

mt_err_t mini_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, mini_task_entry_t entry, void* param, int core_id,
                            mini_task_handle_t* out_handle)
{
    if (!out_handle || !entry)
        return MINI_ERR_INVAL;
    if (hal_is_in_isr())
        return MINI_ERR_ISR;

    mini_note_core_fallback(name, &core_id);

    TaskHandle_t handle = NULL;
    BaseType_t   ret    = xTaskCreate(entry, name, mini_stack_words(stack_size), param, mini_clamp_task_priority(priority), &handle);
    if (ret != pdPASS)
        return MINI_ERR_NOMEM;

    *out_handle = (mini_task_handle_t)handle;
    return MINI_OK;
}

void mini_task_self_delete(void)
{
#ifdef ESP_PLATFORM
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self != NULL && esp_task_wdt_status(self) == ESP_OK)
        esp_task_wdt_delete(self);
#endif
    vTaskDelete(NULL);
}

void mini_task_delete(mini_task_handle_t task)
{
    if (!task)
        return;
    vTaskDelete((TaskHandle_t)task);
}

bool mini_task_is_running(mini_task_handle_t task)
{
    if (!task)
        return false;
    return eTaskGetState((TaskHandle_t)task) != eDeleted;
}

const char* mini_task_get_name(mini_task_handle_t task)
{
    if (!task)
        return "?";
    return pcTaskGetName((TaskHandle_t)task);
}

uint32_t mini_task_get_stack_watermark(mini_task_handle_t task)
{
    if (!task)
        return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)task) * sizeof(StackType_t);
}

/* -------------------------------------------------------------------------- */
/* 调度器启动 / ISR 出口                                                       */
/* -------------------------------------------------------------------------- */
mt_err_t mini_scheduler_start(void)
{
    vTaskStartScheduler();
    return MINI_OK; /* 正常情况下不返回 */
}

/* fail-fast 单向冻结: 挂起调度器, 之后不再有任何上下文切换 */
void mini_sched_freeze(void) { vTaskSuspendAll(); }

void mini_yield_from_isr(bool yield_required)
{
    if (yield_required)
        portYIELD_FROM_ISR(pdTRUE);
}

/* -------------------------------------------------------------------------- */
/* 内存 (libc 堆)                                                              */
/* -------------------------------------------------------------------------- */
void* mini_malloc(size_t size) { return malloc(size); }

void* mini_calloc(size_t count, size_t size) { return calloc(count, size); }

mt_err_t mini_free(void* ptr)
{
    free(ptr);
    return MINI_OK;
}

/* -------------------------------------------------------------------------- */
/* FreeRTOS 内核强制要求应用提供的回调 (ESP 由 IDF 自带, 故排除)                */
/* -------------------------------------------------------------------------- */
/* configSUPPORT_STATIC_ALLOCATION 打开时必需, 否则链接期缺符号 */
#ifndef ESP_PLATFORM
static StackType_t  s_idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t s_idle_tcb;

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer, StackType_t** ppxIdleTaskStackBuffer, uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

#if (configUSE_TIMERS == 1)
static StackType_t  s_timer_stack[configTIMER_TASK_STACK_DEPTH];
static StaticTask_t s_timer_tcb;

void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer, StackType_t** ppxTimerTaskStackBuffer, uint32_t* pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &s_timer_tcb;
    *ppxTimerTaskStackBuffer = s_timer_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS */

/* configCHECK_FOR_STACK_OVERFLOW > 0 时必需 */
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

#endif /* CONFIG_OS_FREERTOS */
