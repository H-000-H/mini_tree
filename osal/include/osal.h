#ifndef BOARD_OSAL_H
#define BOARD_OSAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler_compat.h"
#include "printf_output.h"
#include "osal_tick.h"

#ifdef __cplusplus
extern "C" 
{
#endif

                                                            /*等待超时常量*/
/*===========================================================================================================================================================*/
#define OSAL_WAIT_FOREVER UINT32_MAX
#ifndef OSAL_LOCK_TIMEOUT_DEFAULT_MS
#define OSAL_LOCK_TIMEOUT_DEFAULT_MS 100U  /* 板级可在 board_config.h 中 #define 覆盖 */
#endif
/*===========================================================================================================================================================*/

                                                            /*任务入口与日志级别*/
/*===========================================================================================================================================================*/
typedef void (*osal_task_entry_t)(void* param);

/* ── 优先级说明 ──
 * OSAL 不定义统一的优先级公约, 每个后端使用所属 RTOS 的原生语义:
 *   FreeRTOS:  0 = 最低, configMAX_PRIORITIES-1 = 最高
 *   RT-Thread: 0 = 最高, RT_THREAD_PRIORITY_MAX-1 = 最低
 *   NULL:      不支持多任务, 忽略优先级
 * 切换 RTOS 后端时需留意优先级数值含义的差异.
 */

typedef enum
{
    OSAL_LOG_ERROR = 0,
    OSAL_LOG_WARN,
    OSAL_LOG_INFO,
    OSAL_LOG_DEBUG,
} osal_log_level_t;
/*===========================================================================================================================================================*/

                                                            /*时间 API*/
/*===========================================================================================================================================================*/
uint32_t osal_time_ms(void);
void osal_delay_ms(uint32_t ms);
osal_tick_t osal_ticks_from_ms(uint32_t ms);
/* timeout_ms → RTOS tick; OSAL_WAIT_FOREVER → 永久等待, 0 → 不等待 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms);
/*===========================================================================================================================================================*/

                                                            /*内存 API*/
/*===========================================================================================================================================================*/
void* osal_calloc(size_t count, size_t size);
void osal_free(void* ptr);
/*===========================================================================================================================================================*/

                                                            /*上下文检测*/
/*===========================================================================================================================================================*/
/* ── 上下文检测 (平台无关, 架构泄露防火墙) ──
 * IEC 61508 §7.4.3.4: 框架层禁止出现 CPU 架构绑定指令.
 * 调用方仅依赖 osal.h, 实现由 osal_freertos.c 按平台适配:
 *   - FreeRTOS 全平台: xPortInIsrContext()
 *   - ARM 裸机: __get_IPSR()
 *   - 不允许在 board_driver.c 等框架层直接调用 CMSIS 汇编.
 */
int osal_in_isr(void);
/*===========================================================================================================================================================*/

                                                            /*不透明 OSAL 对象*/
/*===========================================================================================================================================================*/
struct osal_spinlock;
struct osal_mutex;
struct osal_sem;
/*===========================================================================================================================================================*/

                                                            /*自旋锁*/
/*===========================================================================================================================================================*/
#define OSAL_SPINLOCK_STORAGE_SIZE  32  /* 足够容纳 struct osal_spinlock (含 portMUX_TYPE) + 对齐 */

void osal_spinlock_init(struct osal_spinlock* lock);
void osal_spinlock_lock(struct osal_spinlock* lock);
void osal_spinlock_unlock(struct osal_spinlock* lock);
/*===========================================================================================================================================================*/

                                                            /*调度器挂起 / 中断禁用*/
/*===========================================================================================================================================================*/
/* ── 调度器挂起 / 中断禁用 ──
 * 用于 safe_state, bootloop 防护等 fail-fast 场景.
 * 调用方无需 #ifdef CONFIG_OSAL_*, 三个后端统一实现.
 */
void osal_sched_suspend(void);   /* 挂起调度器 (FreeRTOS: vTaskSuspendAll) */
void osal_int_disable(void);     /* 禁用全局中断 (FreeRTOS: portDISABLE_INTERRUPTS) */
/*===========================================================================================================================================================*/

                                                            /*互斥锁 — 类型与存储*/
/*===========================================================================================================================================================*/
/* ── 互斥锁 ──
 *
 * 类型在创建时绑定, 运行期不可变; lock/unlock 按内部 type 自动分发 RTOS API.
 * 禁止对 plain 锁使用递归语义, 禁止对 recursive 锁当作普通锁混用底层原语.
 *
 * 选型:
 *   OSAL_MUTEX_PLAIN     — 默认; 非递归, 同一线程二次 lock 将阻塞直至超时.
 *                          用于驱动 io_lock、总线锁、EventBus 等单层持锁场景.
 *   OSAL_MUTEX_RECURSIVE — 同一线程可嵌套 lock/unlock, 须成对释放.
 *                          仅用于 device_lock(dev->lock) 等可能嵌套 VFS 调用的路径.
 *
 * 创建 (类型选定后不可更改):
 *
 *   1) 默认普通互斥锁 (推荐):
 *        osal_mutex_create(&mtx);
 *        osal_mutex_create_static(&mtx, buf, sizeof(buf));
 *
 *   2) 显式递归锁 (仅需要重入时):
 *        osal_mutex_create_recursive(&mtx);
 *        osal_mutex_create_static_recursive(&mtx, buf, sizeof(buf));
 *
 *   3) 显式别名 / 指定类型:
 *        osal_mutex_create_plain(&mtx);              等价于默认 create
 *        osal_mutex_create_static_plain(&mtx, ...);  等价于默认 create_static
 *        osal_mutex_create_typed(&mtx, OSAL_MUTEX_RECURSIVE);
 *        osal_mutex_create_static_typed(&mtx, buf, sizeof(buf), OSAL_MUTEX_PLAIN);
 *
 * 使用:
 *        osal_mutex_lock(mtx, OSAL_LOCK_TIMEOUT_DEFAULT_MS);  返回 0 表示成功
 *        osal_mutex_unlock(mtx);
 *        osal_mutex_destroy(mtx);  仅池分配创建的锁需要
 *
 * 注意: 中断上下文禁止 lock/unlock/create/destroy; 临界区请用 osal_spinlock.
 */
#ifndef OSAL_MUTEX_STORAGE_SIZE
#define OSAL_MUTEX_STORAGE_SIZE 96   /* 足够容纳 struct osal_mutex + 静态信号量缓存 */
#endif

typedef enum
{
    OSAL_MUTEX_RECURSIVE = 0,  /* 可重入, 须显式 create_recursive */
    OSAL_MUTEX_PLAIN     = 1,  /* 非递归, create 默认 */
} osal_mutex_type_t;
/*===========================================================================================================================================================*/

                                                            /*互斥锁 — 创建 API*/
/*===========================================================================================================================================================*/
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
    COMPAT_WARN_UNUSED_RESULT;
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,
                                   size_t storage_size, osal_mutex_type_t type)
    COMPAT_WARN_UNUSED_RESULT;

/* 默认普通互斥锁 */
int osal_mutex_create(struct osal_mutex** out) COMPAT_WARN_UNUSED_RESULT;
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
    COMPAT_WARN_UNUSED_RESULT;

/* 显式递归锁 */
int osal_mutex_create_recursive(struct osal_mutex** out) COMPAT_WARN_UNUSED_RESULT;
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
    COMPAT_WARN_UNUSED_RESULT;

/* 显式普通锁 (与默认 create 等价, 保留用于强调语义) */
int osal_mutex_create_plain(struct osal_mutex** out) COMPAT_WARN_UNUSED_RESULT;
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
    COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*互斥锁 — 使用 API*/
/*===========================================================================================================================================================*/
void osal_mutex_destroy(struct osal_mutex* mutex);
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;  /* OSAL_WAIT_FOREVER 永久等待 */
int osal_mutex_unlock(struct osal_mutex* mutex);
/*===========================================================================================================================================================*/

                                                            /*ISR 上下文切换*/
/*===========================================================================================================================================================*/
/* ── ISR 上下文切换 (FreeRTOS: portYIELD_FROM_ISR) ──
 * _from_isr 系列仅设置 *px_yield_required, 绝不内部 yield.
 * ISR 出口统一调用 osal_yield_from_isr(*px_yield_required).
 * px_yield_required 可为 NULL (不追踪 yield, 由调用方自行保证).
 */
void osal_yield_from_isr(bool yield_required);
/*===========================================================================================================================================================*/

                                                            /*二值信号量*/
/*===========================================================================================================================================================*/
/* ── 二值信号量 (bottom-half: ISR → 任务唤醒) ──
 * 初始计数 0; post/post_from_isr 置位, wait 消费.
 * 多次 post_from_isr 在消费者未 wait 前合并 (计数不超过 1).
 */

#ifndef OSAL_SEM_STORAGE_SIZE
#define OSAL_SEM_STORAGE_SIZE 96
#endif

#ifndef OSAL_SEM_POOL_SIZE
#define OSAL_SEM_POOL_SIZE 8
#endif

int  osal_sem_create_binary(struct osal_sem** out) COMPAT_WARN_UNUSED_RESULT;
int  osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
    COMPAT_WARN_UNUSED_RESULT;
void osal_sem_destroy(struct osal_sem* sem);
int  osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
bool osal_sem_post(struct osal_sem* sem) COMPAT_WARN_UNUSED_RESULT;
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*槽位池*/
/*===========================================================================================================================================================*/
/* ── 槽位池 (线程/中断安全 claim/release) ──
 * used_slots[] 由调用方提供; osal_pool_init() 须在首次 claim 前调用一次.
 * ESP32: 池内嵌 portMUX, 任务与 ISR 均可安全 claim/release.
 */
#ifndef OSAL_POOL_MUX_STORAGE_SIZE
#define OSAL_POOL_MUX_STORAGE_SIZE 16
#endif

typedef struct osal_pool
{
    volatile uint8_t* used_slots;
    size_t            slot_count;
    uint8_t           mux_storage[OSAL_POOL_MUX_STORAGE_SIZE];
} osal_pool_t;

int  osal_pool_init(osal_pool_t* pool, volatile uint8_t* buffer, size_t count)
    COMPAT_WARN_UNUSED_RESULT;
int  osal_pool_claim(osal_pool_t* pool) COMPAT_WARN_UNUSED_RESULT;
void osal_pool_release(osal_pool_t* pool, int slot_index);
/*===========================================================================================================================================================*/

                                                            /*任务 API*/
/*===========================================================================================================================================================*/
/* ── 任务 (stack_size 单位: 字节, 所有后端统一) ── */
int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*任务句柄 API*/
/*===========================================================================================================================================================*/
typedef void* osal_task_handle_t;

int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle) COMPAT_WARN_UNUSED_RESULT;
void osal_task_self_delete(void);
void osal_task_delete(osal_task_handle_t task);
bool osal_task_is_running(osal_task_handle_t task);
const char* osal_task_get_name(osal_task_handle_t task);
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task);
/*===========================================================================================================================================================*/

                                                            /*队列 API*/
/*===========================================================================================================================================================*/
/* ── 队列 (定长消息队列) ──
 *
 * 任务 / ISR 严格分离, 禁止自动推导:
 *   osal_queue_send() / osal_queue_receive()
 *       — 仅任务上下文, ISR 中调用返回 false.
 *   osal_queue_send_from_isr() / osal_queue_receive_from_isr()
 *       — 仅 ISR 上下文, 通过 px_yield_required 上报 yield 需求,
 *         不在内部调用 osal_yield_from_isr().
 *
 * ISR 典型用法:
 *   bool woken = false;
 *   osal_queue_send_from_isr(q, &item, &woken);
 *   osal_yield_from_isr(woken);   // ISR 最外层出口
 */
typedef void* osal_queue_handle_t;

osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size) COMPAT_WARN_UNUSED_RESULT;
void osal_queue_delete(osal_queue_handle_t queue);
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required) COMPAT_WARN_UNUSED_RESULT;
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*安全互锁与硬件关断*/
/*===========================================================================================================================================================*/
/* ── 安全互锁 (weak, 板级可重写, 保留用于 OEM 兼容) ── */
void osal_panic_interlock(void);

/* ── 硬件安全关断 (weak, 保留用于外部 OEM 代码兼容) ── */
void safety_hardware_shutdown(void);

/* ── 板级硬件安全关断 (强符号, 板级必须实现) ──
 * IEC 61508 §7.4.3.4 / IEC 62304 Class C:
 * 链接器强制检查 — 若 board_driver.c 未实现此函数, 链接失败.
 * 职责: portDISABLE_INTERRUPTS + hal_gpio_set_level 拉低执行器 + hal_pwm_force_stop_all
 */
void system_safety_hardware_shutdown(const char* reason);
/*===========================================================================================================================================================*/

                                                            /*Panic / Critical Assert 宏*/
/*===========================================================================================================================================================*/
/* ── Panic (不可恢复错误, 工业/医疗 fail-fast → safe state) ──
 * 1. printf 输出致命原因
 * 2. 调用 system_safety_hardware_shutdown() — 强符号, 链接期强制检查
 * 3. 驻留死循环, 等待外部硬件看门狗复位
 * 永不返回.
 */
#undef OSAL_PANIC
#define OSAL_PANIC(fmt, ...) do { \
    osal_log_fatal(fmt, ##__VA_ARGS__); \
    system_safety_hardware_shutdown("OSAL_PANIC"); \
    while (1) \
    { ; } \
} while (0)

/* ── 关键断言 (IEC 61508 §7.4.3.4: Fail-Fast) ──
 * 用于 probe/config 阶段的强制契约检查.
 * 如果 DTS 缺少强制属性或硬件配置不匹配, 必须立即停机, 严禁静默降级.
 */
#define CRITICAL_ASSERT(cond, fmt, ...) do { \
    if (!(cond)) \
    { \
        osal_log_critical_assert(__FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        system_safety_hardware_shutdown("CRITICAL_ASSERT"); \
        while (1) \
        { ; } \
    } \
} while (0)
/*===========================================================================================================================================================*/

                                                            /*日志 API*/
/*===========================================================================================================================================================*/
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...);
void osal_log_fatal(const char* fmt, ...) COMPAT_FMT_PRINTF(1, 2);
void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
    COMPAT_FMT_PRINTF(3, 4);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* BOARD_OSAL_H */
