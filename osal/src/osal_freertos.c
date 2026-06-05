#ifdef  CONFIG_OSAL_FREERTOS

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdarg.h>
#include <stdlib.h>

/* ── 队列 / 信号量内部存储 ── */
struct osal_mutex
{
    SemaphoreHandle_t handle;
    StaticSemaphore_t sem_buf;
};

_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "OSAL_MUTEX_STORAGE_SIZE too small");

/* ── ISR 上下文检测 (平台适配层) ──
 * ARMv7-M/v8-M: 读 IPSR 寄存器
 * RISC-V:       读 mcause, 检查 bit31
 * 其他:         默认 0 (保守, 不在 ISR 中调用 FreeRTOS FromISR API)
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

/* ── Spinlock: 关中断临界区 (单核 FreeRTOS) ── */
struct osal_spinlock
{
    volatile int locked;
};

void osal_spinlock_init(osal_spinlock_t* lock)
{
    if (!lock) return;
    lock->locked = 0;
}

void osal_spinlock_lock(osal_spinlock_t* lock)
{
    if (!lock) return;
    if (!osal_in_isr())
    {
        taskENTER_CRITICAL();
    }
    lock->locked = 1;
}

void osal_spinlock_unlock(osal_spinlock_t* lock)
{
    if (!lock) return;
    lock->locked = 0;
    if (!osal_in_isr())
    {
        taskEXIT_CRITICAL();
    }
}

/* ── 静态互斥锁池 (OSAL_MUTEX_POOL_SIZE 来自 board_config.h) ── */

static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE];
static uint8_t s_mutex_used[OSAL_MUTEX_POOL_SIZE];

int osal_pool_claim(volatile uint8_t* used_slots, size_t slot_count)
{
    if (!used_slots || slot_count == 0) return -1;

    if (!osal_in_isr()) taskENTER_CRITICAL();
    int claimed_index = -1;
    for (size_t i = 0; i < slot_count; i++)
    {
        if (!used_slots[i])
        {
            used_slots[i] = 1;
            claimed_index = (int)i;
            break;
        }
    }
    if (!osal_in_isr()) taskEXIT_CRITICAL();
    return claimed_index;
}

void osal_pool_release(volatile uint8_t* used_slots, size_t slot_count, int slot_index)
{
    if (!used_slots || slot_index < 0 || (size_t)slot_index >= slot_count) return;
    if (!osal_in_isr()) taskENTER_CRITICAL();
    used_slots[slot_index] = 0;
    if (!osal_in_isr()) taskEXIT_CRITICAL();
}

/* ── 时间 ── */
uint32_t osal_time_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void osal_delay_ms(uint32_t ms)
{
    if (osal_in_isr()) return;  /* 中断中不能阻塞 */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t osal_ticks_from_ms(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

/* ── 内存 ── */
void* osal_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void osal_free(void* ptr)
{
    free(ptr);
}

/* ── 互斥锁 ── */
int osal_mutex_create(osal_mutex_t** out)
{
    if (!out) return -1;
    if (osal_in_isr()) return -1;  /* 中断中不能创建 */
    *out = NULL;

    int index = osal_pool_claim(s_mutex_used, OSAL_MUTEX_POOL_SIZE);
    if (index < 0) return -1;

    struct osal_mutex* m = &s_mutex_pool[index];
    m->handle = xSemaphoreCreateRecursiveMutexStatic(&m->sem_buf);
    if (!m->handle)
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
    if (osal_in_isr()) return -1;  /* 中断中不能创建 */
    *out = NULL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    m->handle = xSemaphoreCreateRecursiveMutexStatic(&m->sem_buf);
    if (!m->handle) return -1;

    *out = (osal_mutex_t*)m;
    return 0;
}

void osal_mutex_destroy(osal_mutex_t* mutex)
{
    if (!mutex) return;
    if (osal_in_isr()) return;  /* 中断中不能销毁 */
    struct osal_mutex* m = (struct osal_mutex*)mutex;
    if (m->handle)
    {
        vSemaphoreDelete(m->handle);
        m->handle = NULL;
    }
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
    if (!mutex || !mutex->handle) return -1;
    if (osal_in_isr()) return -1;  /* 中断中不允许阻塞 */
    TickType_t ticks = (timeout_ms == OSAL_WAIT_FOREVER)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(mutex->handle, ticks) == pdTRUE ? 0 : -1;
}

int osal_mutex_unlock(osal_mutex_t* mutex)
{
    if (!mutex || !mutex->handle) return -1;
    if (osal_in_isr()) return -1;  /* 中断中不允许释放 */
    return xSemaphoreGiveRecursive(mutex->handle) == pdTRUE ? 0 : -1;
}

/* ── FreeRTOS 静态分配回调 (configSUPPORT_STATIC_ALLOCATION) ── */
static StackType_t   s_idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t  s_idle_tcb;

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                    StackType_t** ppxIdleTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/* ── 任务 (stack_size 字节 → FreeRTOS words 转换) ── */
static inline uint32_t osal_stack_words(uint32_t stack_bytes)
{
    return (stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t);
}

int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id)
{
#if CONFIG_CPU_CORES > 1
    if (core_id > 0)
    {
        printf("[osal] WARN: task '%s' requested Core %d, "
               "but AMP Core 1 has no OS scheduler. "
               "Falling back to Core 0.\n", name, core_id);
        core_id = 0;
    }
#else
    (void)core_id;
#endif

    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreate(entry, name, osal_stack_words(stack_size),
                                 param, priority, &handle);
    return (ret == pdPASS) ? 0 : -1;
}

/* ── 任务句柄 ── */
int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle) return -1;
#if CONFIG_CPU_CORES > 1
    if (core_id > 0)
    {
        printf("[osal] WARN: task '%s' requested Core %d, "
               "but AMP Core 1 has no OS scheduler. "
               "Falling back to Core 0.\n", name, core_id);
        core_id = 0;
    }
#else
    (void)core_id;
#endif

    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreate(entry, name, osal_stack_words(stack_size),
                                 param, priority, &handle);
    if (ret != pdPASS) return -1;
    *out_handle = (osal_task_handle_t)handle;
    return 0;
}

void osal_task_self_delete(void)
{
    vTaskDelete(NULL);
}

void osal_task_delete(osal_task_handle_t task)
{
    vTaskDelete((TaskHandle_t)task);
}

bool osal_task_is_running(osal_task_handle_t task)
{
    if (!task) return false;
    return eTaskGetState((TaskHandle_t)task) != eDeleted;
}

const char* osal_task_get_name(osal_task_handle_t task)
{
    if (!task) return "?";
    return pcTaskGetName((TaskHandle_t)task);
}

uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    if (!task) return 0;
    UBaseType_t wm = uxTaskGetStackHighWaterMark((TaskHandle_t)task);
    return (uint32_t)wm * sizeof(StackType_t);
}

/* ── 队列 ── */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    return (osal_queue_handle_t)xQueueCreate(queue_len, item_size);
}

void osal_queue_delete(osal_queue_handle_t queue)
{
    vQueueDelete((QueueHandle_t)queue);
}

bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
    {
        BaseType_t higher_prio_woken = pdFALSE;
        BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
        if (higher_prio_woken)
        {
            portYIELD_FROM_ISR(higher_prio_woken);
        }
        return ret == pdTRUE;
    }
    TickType_t ticks = (timeout_ms == OSAL_WAIT_FOREVER)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);
    return xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item)
{
    BaseType_t high_task_woken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &high_task_woken);
    if (high_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR(high_task_woken);
    }
    return ret == pdTRUE;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
    {
        BaseType_t higher_prio_woken = pdFALSE;
        BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
        if (higher_prio_woken)
        {
            portYIELD_FROM_ISR(higher_prio_woken);
        }
        return ret == pdTRUE;
    }
    TickType_t ticks;
    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }
    return xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

/* ── 硬件安全关断 (weak) ── */
COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

/* ── Panic 安全互锁 (weak) ── */
COMPAT_WEAK void osal_panic_interlock(void)
{
    /* 板级可覆盖: 喂硬件看门狗, 切断执行器供电, 等待复位 */
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

#endif /* CONFIG_OSAL_FREERTOS */