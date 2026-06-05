#ifndef BOARD_OSAL_H
#define BOARD_OSAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSAL_WAIT_FOREVER UINT32_MAX
#ifndef OSAL_LOCK_TIMEOUT_DEFAULT_MS
#define OSAL_LOCK_TIMEOUT_DEFAULT_MS 100U  /* 板级可在 board_config.h 中 #define 覆盖 */
#endif

typedef struct osal_mutex osal_mutex_t;
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

/* ── 时间 ── */
uint32_t osal_time_ms(void);
void osal_delay_ms(uint32_t ms);
uint32_t osal_ticks_from_ms(uint32_t ms);

/* ── 内存 ── */
void* osal_calloc(size_t count, size_t size);
void osal_free(void* ptr);

/* ── 上下文检测 (平台无关, 架构泄露防火墙) ──
 * IEC 61508 §7.4.3.4: 框架层禁止出现 CPU 架构绑定指令.
 * 调用方仅依赖 osal.h, 实现由 osal_freertos.c 按平台适配:
 *   - FreeRTOS 全平台: xPortInIsrContext()
 *   - ARM 裸机: __get_IPSR()
 *   - 不允许在 board_driver.c 等框架层直接调用 CMSIS 汇编.
 */
int osal_in_isr(void);

/* ── 自旋锁 (临界区, 关中断保护) ── */
typedef struct osal_spinlock osal_spinlock_t;

#define OSAL_SPINLOCK_STORAGE_SIZE  8   /* 足够容纳 struct osal_spinlock + 对齐 */

void osal_spinlock_init(osal_spinlock_t* lock);
void osal_spinlock_lock(osal_spinlock_t* lock);
void osal_spinlock_unlock(osal_spinlock_t* lock);

/* ── 互斥锁 ── */
#define OSAL_MUTEX_STORAGE_SIZE 160  /* 足够容纳 struct osal_mutex + 静态信号量缓存 (64-bit POSIX 约 144) */

int osal_mutex_create(osal_mutex_t** out);
int osal_mutex_create_static(osal_mutex_t** out, void* storage, size_t storage_size);
void osal_mutex_destroy(osal_mutex_t* mutex);
int osal_mutex_lock(osal_mutex_t* mutex, uint32_t timeout_ms);
int osal_mutex_unlock(osal_mutex_t* mutex);

/* ── 静态池辅助函数 ── */
int osal_pool_claim(volatile uint8_t* used_slots, size_t slot_count);
void osal_pool_release(volatile uint8_t* used_slots, size_t slot_count, int slot_index);

/* ── 任务 (stack_size 单位: 字节, 所有后端统一) ── */
int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id);

/* ── 任务句柄 (返回句柄, 支持查询和控制) ── */
typedef void* osal_task_handle_t;

int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle);
void osal_task_self_delete(void);
void osal_task_delete(osal_task_handle_t task);
bool osal_task_is_running(osal_task_handle_t task);
const char* osal_task_get_name(osal_task_handle_t task);
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task);

/* ── 队列 (定长消息队列) ──
 *
 * 发送接口提供两条路径:
 *   osal_queue_send()         — 内部通过 osal_in_isr() 自动识别上下文,
 *                                在 ISR 中自动走 FromISR 路径 (含 yield),
 *                                在任务中支持 timeout_ms 阻塞. 省心但隐含
 *                                一次 osal_in_isr() 调用开销和 yield 判断.
 *   osal_queue_send_from_isr() — 显式 ISR 版本, 不猜测, 不 yield.
 *                                调用方自己保证在 ISR 上下文中调用.
 *
 * ★ 推荐: 确定在 ISR 中时优先用 osal_queue_send_from_isr(),
 *   意图直白, 零额外判断, 代码审查时一目了然.
 *   不确定或懒得区分时, 用 osal_queue_send() 自动推导也不会错.
 */
typedef void* osal_queue_handle_t;

osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size);
void osal_queue_delete(osal_queue_handle_t queue);
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms);
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item);
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms);

/* ── 安全互锁 (weak, 板级可重写, 保留用于 OEM 兼容) ── */
void osal_panic_interlock(void);

/* ── 硬件安全关断 (weak, 保留用于外部 OEM 代码兼容) ── */
void safety_hardware_shutdown(void);

/* ── 板级硬件安全关断 (强符号, 板级必须实现) ──
 * IEC 61508 §7.4.3.4 / IEC 62304 Class C:
 * 链接器强制检查 — 若 board_driver.c 未实现此函数, 链接失败.
 * 职责: portDISABLE_INTERRUPTS + hal_gpio_set_level_fast 拉低执行器 + hal_pwm_force_stop_all
 */
void system_safety_hardware_shutdown(const char* reason);

/* ── Panic (不可恢复错误, 工业/医疗 fail-fast → safe state) ──
 * 1. printf 输出致命原因
 * 2. 调用 system_safety_hardware_shutdown() — 强符号, 链接期强制检查
 * 3. 驻留死循环, 等待外部硬件看门狗复位
 * 永不返回.
 */
#undef OSAL_PANIC
#define OSAL_PANIC(fmt, ...) do { \
    printf("\r\n[FATAL ERROR] " fmt "\r\n", ##__VA_ARGS__); \
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
        printf("\r\n[CRITICAL_ASSERT FAILED] %s:%d: " fmt "\r\n", \
               __FILE__, __LINE__, ##__VA_ARGS__); \
        system_safety_hardware_shutdown("CRITICAL_ASSERT"); \
        while (1) \
        { ; } \
    } \
} while (0)

/* ── 日志 ── */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_OSAL_H */