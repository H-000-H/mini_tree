#ifdef  CONFIG_OSAL_FREERTOS

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"

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

static int osal_mutex_init(struct osal_mutex* m, osal_mutex_type_t type)
{
    if (!m) return -1;

    m->type = type;
    if (type == OSAL_MUTEX_RECURSIVE)
        m->handle = xSemaphoreCreateRecursiveMutexStatic(&m->sem_buf);
    else if (type == OSAL_MUTEX_PLAIN)
        m->handle = xSemaphoreCreateMutexStatic(&m->sem_buf);
    else
        return -1;

    return m->handle ? 0 : -1;
}

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

/* ── Spinlock: 关中断临界区 ── */
struct osal_spinlock
{
    volatile int locked;
#ifdef ESP_PLATFORM
    portMUX_TYPE mux;
#endif
};

void osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return;
    lock->locked = 0;
#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(&lock->mux);
#endif
}

void osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return;
    if (!osal_in_isr())
    {
#ifdef ESP_PLATFORM
        taskENTER_CRITICAL(&lock->mux);
#else
        taskENTER_CRITICAL();
#endif
    }
    lock->locked = 1;
}

void osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return;
    lock->locked = 0;
    if (!osal_in_isr())
    {
#ifdef ESP_PLATFORM
        taskEXIT_CRITICAL(&lock->mux);
#else
        taskEXIT_CRITICAL();
#endif
    }
}

/* ── 槽位池 (每池独立临界区锁) ── */

#ifdef ESP_PLATFORM
_Static_assert(sizeof(portMUX_TYPE) <= OSAL_POOL_MUX_STORAGE_SIZE,
               "OSAL_POOL_MUX_STORAGE_SIZE too small for portMUX_TYPE");

static inline portMUX_TYPE* osal_pool_mux(osal_pool_t* pool)
{
    return (portMUX_TYPE*)pool->mux_storage;
}
#else
typedef int portMUX_TYPE;
#endif

static inline void osal_pool_lock(osal_pool_t* pool)
{
#ifdef ESP_PLATFORM
    portMUX_TYPE* mux = osal_pool_mux(pool);
    if (osal_in_isr())
        portENTER_CRITICAL_ISR(mux);
    else
        taskENTER_CRITICAL(mux);
#else
    if (!osal_in_isr())
        taskENTER_CRITICAL();
    (void)pool;
#endif
}

static inline void osal_pool_unlock(osal_pool_t* pool)
{
#ifdef ESP_PLATFORM
    portMUX_TYPE* mux = osal_pool_mux(pool);
    if (osal_in_isr())
        portEXIT_CRITICAL_ISR(mux);
    else
        taskEXIT_CRITICAL(mux);
#else
    if (!osal_in_isr())
        taskEXIT_CRITICAL();
    (void)pool;
#endif
}

int osal_pool_init(osal_pool_t* pool, volatile uint8_t* buffer, size_t count)
{
    if (!pool || !buffer || count == 0)
        return -1;

    pool->used_slots = buffer;
    pool->slot_count = count;

    for (size_t i = 0; i < count; i++)
        buffer[i] = 0;

#ifdef ESP_PLATFORM
    portMUX_INITIALIZE(osal_pool_mux(pool));
#endif

    return 0;
}

int osal_pool_claim(osal_pool_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return -1;

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

void osal_pool_release(osal_pool_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 ||
        (size_t)slot_index >= pool->slot_count)
        return;

    osal_pool_lock(pool);
    pool->used_slots[slot_index] = 0;
    osal_pool_unlock(pool);
}

/* ── 静态互斥锁池 (OSAL_MUTEX_POOL_SIZE 来自 board_config.h) ── */

static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static uint8_t           s_mutex_used[OSAL_MUTEX_POOL_SIZE] COMPAT_ALIGNED(4);
static osal_pool_t       s_mutex_pool_ctrl COMPAT_ALIGNED(4);

pre_execution(150)
static void osal_mutex_pool_boot_init(void)
{
    osal_pool_init(&s_mutex_pool_ctrl, s_mutex_used, OSAL_MUTEX_POOL_SIZE);
}

/* ── 获取现在时间 ── */
uint32_t osal_time_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void osal_delay_ms(uint32_t ms)
{
    if (osal_in_isr()) return;  /* 中断中不能阻塞 */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return portMAX_DELAY;
    return pdMS_TO_TICKS(timeout_ms);
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
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out) return -1;
    if (osal_in_isr()) return -1;  /* 中断中不能创建 */
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;
    *out = NULL;

    int index = osal_pool_claim(&s_mutex_pool_ctrl);
    if (index < 0) return -1;

    struct osal_mutex* m = &s_mutex_pool[index];
    if (osal_mutex_init(m, type) != 0)
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
    if (osal_in_isr()) return -1;  /* 中断中不能创建 */
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;
    *out = NULL;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type) != 0) return -1;

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
            osal_pool_release(&s_mutex_pool_ctrl, i);
            break;
        }
    }
}

int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex || !mutex->handle) return -1;
    if (osal_in_isr()) return -1;  /* 中断中不允许阻塞 */
    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    if (mutex->type == OSAL_MUTEX_RECURSIVE)
        return xSemaphoreTakeRecursive(mutex->handle, ticks) == pdTRUE ? 0 : -1;
    return xSemaphoreTake(mutex->handle, ticks) == pdTRUE ? 0 : -1;
}

int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex || !mutex->handle) return -1;
    if (osal_in_isr()) return -1;  /* 中断中不允许释放 */
    if (mutex->type == OSAL_MUTEX_RECURSIVE)
        return xSemaphoreGiveRecursive(mutex->handle) == pdTRUE ? 0 : -1;
    return xSemaphoreGive(mutex->handle) == pdTRUE ? 0 : -1;
}

/* ── 二值信号量 ── */
struct osal_sem
{
    SemaphoreHandle_t handle;
    StaticSemaphore_t sem_buf;
    bool              from_pool;
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

    sem->handle = xSemaphoreCreateBinaryStatic(&sem->sem_buf);
    if (!sem->handle)
        return -1;

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
    if (!sem || !sem->handle)
        return;

    vSemaphoreDelete(sem->handle);
    sem->handle = NULL;

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
    if (!sem || !sem->handle || osal_in_isr())
        return -1;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xSemaphoreTake(sem->handle, ticks) == pdTRUE ? 0 : -1;
}

bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem || !sem->handle || osal_in_isr())
        return false;

    return xSemaphoreGive(sem->handle) == pdTRUE;
}

static inline void osal_note_isr_yield(bool* px_yield_required, BaseType_t higher_prio_woken)
{
    if (px_yield_required != NULL && higher_prio_woken == pdTRUE)
        *px_yield_required = true;
}

bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    if (!sem || !sem->handle)
        return false;

    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xSemaphoreGiveFromISR(sem->handle, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

void osal_yield_from_isr(bool yield_required)
{
    if (yield_required)
        portYIELD_FROM_ISR(pdTRUE);
}

/* ── 调度器挂起 / 中断禁用 (safe_state, bootloop 防护) ── */
void osal_sched_suspend(void)
{
    vTaskSuspendAll();
}

void osal_int_disable(void)
{
    portDISABLE_INTERRUPTS();
}

/* ── FreeRTOS 静态分配回调 (configSUPPORT_STATIC_ALLOCATION) ──
 * ESP-IDF v5.x 自身已在 port_common.c 提供此回调, 避免重复定义.
 */
#ifndef ESP_PLATFORM
static StackType_t   s_idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t  s_idle_tcb;

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                    StackType_t** ppxIdleTaskStackBuffer,
                                    uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}
#endif /* !ESP_PLATFORM */

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
        my_printf_output("[osal] WARN: task '%s' requested Core %d, "
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
        my_printf_output("[osal] WARN: task '%s' requested Core %d, "
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
#ifdef ESP_PLATFORM
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self != NULL && esp_task_wdt_status(self) == ESP_OK)
    {
        esp_task_wdt_delete(self);
    }
#endif
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
        return false;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    osal_tick_t ticks = osal_timeout_to_ticks(timeout_ms);
    return xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE;
}

bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    BaseType_t higher_prio_woken = pdFALSE;
    BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t)queue, item, &higher_prio_woken);
    osal_note_isr_yield(px_yield_required, higher_prio_woken);
    return ret == pdTRUE;
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

#endif /* CONFIG_OSAL_FREERTOS */
