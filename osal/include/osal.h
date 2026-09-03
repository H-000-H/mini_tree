/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file osal.h
 *@brief osal 头文件
 *@author H-000-H
 *@details
 *   OSAL — 操作系统抽象层公共头文件
 *   定义平台无关的 RTOS 接口: 互斥锁/信号量/自旋锁/队列/任务/时间/内存
 *   上层仅依赖本头文件, 实现由 osal_freertos/osal_rtthread/osal_null 三后端提供
 *   含 OSAL_PANIC/MINI_CRITICAL_ASSERT 宏, 触发后调用 system_safety_hardware_shutdown
 */

#ifndef BOARD_OSAL_H
#define BOARD_OSAL_H

#include "compiler_compat.h"
#include "osal_tick.h"
#include "printf_output.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*等待超时常量*/
/* -------------------------------------------------------------------------- */
#define OSAL_WAIT_FOREVER UINT32_MAX
#ifndef OSAL_LOCK_TIMEOUT_DEFAULT_MS
#define OSAL_LOCK_TIMEOUT_DEFAULT_MS 100U /* 板级可在 board_config.h 中 #define 覆盖 */
#endif
/* -------------------------------------------------------------------------- */

/*任务入口与日志级别*/
/* -------------------------------------------------------------------------- */
typedef void (*osal_task_entry_t)(void* param);

/* -------------------------------------------------------------------------- */
/* 优先级说明 */
/* OSAL 不定义统一的优先级公约, 每个后端使用所属 RTOS 的原生语义: */
/* FreeRTOS:  0 = 最低, configMAX_PRIORITIES-1 = 最高 */
/* RT-Thread: 0 = 最高, RT_THREAD_PRIORITY_MAX-1 = 最低 */
/* NULL:      不支持多任务, 忽略优先级 */
/* 切换 RTOS 后端时需留意优先级数值含义的差异. */
/* -------------------------------------------------------------------------- */

#ifndef OSAL_LOG_LEVEL_T_DEFINED
#define OSAL_LOG_LEVEL_T_DEFINED
typedef enum
{
    OSAL_LOG_ERROR = 0,
    OSAL_LOG_WARN,
    OSAL_LOG_INFO,
    OSAL_LOG_DEBUG,
} osal_log_level_t;
#endif
/* -------------------------------------------------------------------------- */

/*时间 API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 获取当前时间
 * @return 当前时间 (ms)
 */
uint32_t osal_time_ms(void);
/**
 * @brief 延迟毫秒
 * @param[in] ms 毫秒
 */
void osal_delay_ms(uint32_t ms);
/**
 * @brief 忙等微秒 (1-Wire / bitbang 等短时序; ISR 中可用)
 * @param[in] us 微秒
 */
void osal_delay_us(uint32_t us);
/**
 * @brief 将毫秒转换为滴答数
 * @param[in] ms 毫秒
 * @return 滴答数
 */
osal_tick_t osal_ticks_from_ms(uint32_t ms);
/**
 * @brief 将超时毫秒转换为滴答数
 * @param[in] timeout_ms 超时毫秒数
 * @return 滴答数
 */
osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms);
/**
 * @brief 分配并清零内存 (calloc)
 * @param[in] count 数量
 * @param[in] size 单元素大小
 * @return 内存指针; 失败返回 NULL
 */
void* osal_calloc(size_t count, size_t size);
/**
 * @brief 释放内存
 * @param[in] ptr 内存指针 (可为 NULL)
 * @return 成功返回 MINI_OK
 */
int osal_free(void* ptr);

/**
 * @brief 检查是否在中断上下文
 * @return 1=在中断上下文; 0=不在
 */
int osal_in_isr(void);
/* -------------------------------------------------------------------------- */

/*不透明 OSAL 对象*/
/* -------------------------------------------------------------------------- */
struct osal_spinlock;
struct osal_mutex;
struct osal_sem;
struct osal_event;
/* -------------------------------------------------------------------------- */

/*自旋锁*/
/* -------------------------------------------------------------------------- */
#define OSAL_SPINLOCK_STORAGE_SIZE 32 /* 足够容纳 struct osal_spinlock (含 portMUX_TYPE) + 对齐 */

/**
 * @brief 初始化自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 MINI_OK, lock 为空返回 MINI_ERR_INVAL
 */
int osal_spinlock_init(struct osal_spinlock* lock) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 获取自旋锁 (ISR 安全, 禁止睡眠)
 * @param[in] lock 自旋锁指针
 * @return 成功返回 MINI_OK, lock 为空返回 MINI_ERR_INVAL
 */
int osal_spinlock_lock(struct osal_spinlock* lock) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 释放自旋锁
 * @param[in] lock 自旋锁指针
 * @return 成功返回 MINI_OK, lock 为空返回 MINI_ERR_INVAL
 */
int osal_spinlock_unlock(struct osal_spinlock* lock) MINI_WARN_UNUSED_RESULT;
/* -------------------------------------------------------------------------- */

/*调度器冻结 / 中断冻结 (单向不可恢复)*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 调度器冻结 / 中断冻结 */
/* 用于 safe_state, bootloop 防护等 fail-fast 场景. */
/* 单向操作: 调用后不可恢复, 进入了安全死锁状态. */
/* 调用方无需 #ifdef CONFIG_OSAL_*, 三个后端统一实现. */
/* -------------------------------------------------------------------------- */
/**
 * @brief 冻结调度器 (单向不可恢复, 进入安全死锁状态)
 * @note FreeRTOS 后端: vTaskSuspendAll
 */
void osal_sched_freeze(void); /* 冻结调度器 (FreeRTOS: vTaskSuspendAll) */
/**
 * @brief 冻结全局中断 (单向不可恢复)
 * @note FreeRTOS 后端: portDISABLE_INTERRUPTS
 */
void osal_int_freeze(void); /* 冻结全局中断 (FreeRTOS: portDISABLE_INTERRUPTS) */
/* -------------------------------------------------------------------------- */

/*互斥锁 — 类型与存储*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 互斥锁 */
/* 类型在创建时绑定, 运行期不可变; lock/unlock 按内部 type 自动分发 RTOS API. */
/* 禁止对 plain 锁使用递归语义, 禁止对 recursive 锁当作普通锁混用底层原语. */
/* 选型: */
/* OSAL_MUTEX_PLAIN     — 默认; 非递归, 同一线程二次 lock 将阻塞直至超时. */
/* 用于驱动 io_lock、总线锁、EventBus 等单层持锁场景. */
/* OSAL_MUTEX_RECURSIVE — 同一线程可嵌套 lock/unlock, 须成对释放. */
/* 仅用于 device_lock(pdev->lock) 等可能嵌套 VFS 调用的路径. */
/* 创建 (类型选定后不可更改): */
/* 1) 默认普通互斥锁 (推荐): */
/* osal_mutex_create(&mtx); */
/* osal_mutex_create_static(&mtx, buf, sizeof(buf)); */
/* 2) 显式递归锁 (仅需要重入时): */
/* osal_mutex_create_recursive(&mtx); */
/* osal_mutex_create_static_recursive(&mtx, buf, sizeof(buf)); */
/* 3) 显式别名 / 指定类型: */
/* osal_mutex_create_plain(&mtx);              等价于默认 create */
/* osal_mutex_create_static_plain(&mtx, ...);  等价于默认 create_static */
/* osal_mutex_create_typed(&mtx, OSAL_MUTEX_RECURSIVE); */
/* osal_mutex_create_static_typed(&mtx, buf, sizeof(buf), OSAL_MUTEX_PLAIN); */
/* 使用: */
/* osal_mutex_lock(mtx, OSAL_LOCK_TIMEOUT_DEFAULT_MS);  返回 0 表示成功 */
/* osal_mutex_unlock(mtx); */
/* osal_mutex_destroy(mtx);  仅池分配创建的锁需要 */
/* 注意: 中断上下文禁止 lock/unlock/create/destroy; 临界区请用 osal_spinlock. */
/* -------------------------------------------------------------------------- */
#ifndef OSAL_MUTEX_STORAGE_SIZE
#define OSAL_MUTEX_STORAGE_SIZE 96 /* 足够容纳 struct osal_mutex + 静态信号量缓存 */
#endif

typedef enum
{
    OSAL_MUTEX_RECURSIVE = 0, /* 可重入, 须显式 create_recursive */
    OSAL_MUTEX_PLAIN = 1,     /* 非递归, create 默认 */
} osal_mutex_type_t;
/* -------------------------------------------------------------------------- */

/*互斥锁 — 创建 API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 创建互斥锁 (指定类型, 池分配)
 * @param[out] out 回传互斥锁句柄
 * @param[in] type 互斥锁类型 (OSAL_MUTEX_PLAIN/RECURSIVE)
 * @return 成功返回 MINI_OK, 池耗尽返回 MINI_ERR_NOMEM
 */
int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 创建互斥锁 (指定类型, 静态存储)
 * @param[out] out 回传互斥锁句柄
 * @param[in] storage 存储缓冲
 * @param[in] storage_size 缓冲大小
 * @param[in] type 互斥锁类型
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage, size_t storage_size, osal_mutex_type_t type) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 创建普通非递归互斥锁 (池分配, 推荐)
 * @param[out] out 回传互斥锁句柄
 * @return 成功返回 MINI_OK, 池耗尽返回 MINI_ERR_NOMEM
 */
int osal_mutex_create(struct osal_mutex** out) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 创建普通非递归互斥锁 (静态存储)
 * @param[out] out 回传互斥锁句柄
 * @param[in] storage 存储缓冲
 * @param[in] storage_size 缓冲大小
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 创建递归互斥锁 (可嵌套 lock/unlock, 池分配)
 * @param[out] out 回传互斥锁句柄
 * @return 成功返回 MINI_OK, 池耗尽返回 MINI_ERR_NOMEM
 */
int osal_mutex_create_recursive(struct osal_mutex** out) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 创建递归互斥锁 (静态存储)
 * @param[out] out 回传互斥锁句柄
 * @param[in] storage 存储缓冲
 * @param[in] storage_size 缓冲大小
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 创建普通非递归互斥锁 (与 create 等价, 强调语义)
 * @param[out] out 回传互斥锁句柄
 * @return 成功返回 MINI_OK, 池耗尽返回 MINI_ERR_NOMEM
 */
int osal_mutex_create_plain(struct osal_mutex** out) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 创建普通非递归互斥锁 (静态存储, 强调语义)
 * @param[out] out 回传互斥锁句柄
 * @param[in] storage 存储缓冲
 * @param[in] storage_size 缓冲大小
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;
/* -------------------------------------------------------------------------- */

/*互斥锁 — 使用 API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 销毁互斥锁 (仅池分配创建的锁需要)
 * @param[in] mutex 互斥锁指针
 */
void osal_mutex_destroy(struct osal_mutex* mutex);
/**
 * @brief 锁定互斥锁
 * @param[in] mutex 互斥锁指针
 * @param[in] timeout_ms 超时毫秒数 (OSAL_WAIT_FOREVER 永久等待)
 * @return 成功返回 MINI_OK, 超时返回 MINI_ERR_TIMEOUT
 */
int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 释放互斥锁
 * @param[in] mutex 互斥锁指针
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
int osal_mutex_unlock(struct osal_mutex* mutex);
/* -------------------------------------------------------------------------- */

/*ISR 上下文切换*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* ISR 上下文切换 (FreeRTOS: portYIELD_FROM_ISR) */
/* _from_isr 系列仅设置 *px_yield_required, 绝不内部 yield. */
/* ISR 出口统一调用 osal_yield_from_isr(*px_yield_required). */
/* px_yield_required 可为 NULL (不追踪 yield, 由调用方自行保证). */
/* -------------------------------------------------------------------------- */
/**
 * @brief ISR 出口请求上下文切换
 * @param[in] yield_required 是否需要切换
 * @note 仅 ISR 最外层出口调用; _from_isr 系列只设置标志不内部 yield
 */
void osal_yield_from_isr(bool yield_required);
/* -------------------------------------------------------------------------- */

/*二值信号量*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 二值信号量 (bottom-half: ISR → 任务唤醒) */
/* 初始计数 0; post/post_from_isr 置位, wait 消费. */
/* 多次 post_from_isr 在消费者未 wait 前合并 (计数不超过 1). */
/* -------------------------------------------------------------------------- */

#ifndef OSAL_SEM_STORAGE_SIZE
#define OSAL_SEM_STORAGE_SIZE 96
#endif

#ifndef OSAL_SEM_POOL_SIZE
#define OSAL_SEM_POOL_SIZE 8
#endif

/**
 * @brief 创建二值信号量 (池分配)
 * @param[out] out 回传信号量句柄
 * @return 成功返回 MINI_OK, 池耗尽返回 MINI_ERR_NOMEM
 */
int osal_sem_create_binary(struct osal_sem** out) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 创建二值信号量 (静态存储)
 * @param[out] out 回传信号量句柄
 * @param[in] storage 存储缓冲
 * @param[in] storage_size 缓冲大小
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 销毁信号量 (仅池分配创建的需要)
 * @param[in] sem 信号量句柄
 */
void osal_sem_destroy(struct osal_sem* sem);
/**
 * @brief 等待信号量
 * @param[in] sem 信号量句柄
 * @param[in] timeout_ms 超时毫秒数 (OSAL_WAIT_FOREVER 永久等待)
 * @return 成功返回 MINI_OK, 超时返回 MINI_ERR_TIMEOUT
 */
int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 释放信号量 (task 上下文)
 * @param[in] sem 信号量句柄
 * @return 成功返回 true
 */
bool osal_sem_post(struct osal_sem* sem) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 释放信号量 (ISR 上下文, 不内部 yield)
 * @param[in] sem 信号量句柄
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return 成功返回 true
 */
bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;
/* -------------------------------------------------------------------------- */

/*槽位池*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 槽位池 (线程/中断安全 claim/release) */
/* used_slots[] 由调用方提供; osal_pool_init() 须在首次 claim 前调用一次. */
/* ESP32: 池内嵌 portMUX, 任务与 ISR 均可安全 claim/release. */
/* -------------------------------------------------------------------------- */
#ifndef OSAL_POOL_MUX_STORAGE_SIZE
#define OSAL_POOL_MUX_STORAGE_SIZE 16
#endif

/**
 * @brief 槽位池
 */
typedef struct osal_pool
{
    volatile uint8_t* used_slots;                              /**< 已使用槽位指针 */
    size_t            slot_count;                              /**< 槽位数量 */
    uint8_t           mux_storage[OSAL_POOL_MUX_STORAGE_SIZE]; /**< 临界区锁存储ESP平台使用 */
} osal_pool_t;

/**
 * @brief 初始化槽位池 (线程/中断安全 claim/release)
 * @param[in] pool 槽位池指针
 * @param[in] used_slots 已使用槽位位图指针 (由调用方提供)
 * @param[in] slot_count 槽位数量
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
int osal_pool_init(osal_pool_t* pool, volatile uint8_t* used_slots, size_t slot_count) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 申请一个空闲槽位
 * @param[in] pool 槽位池指针
 * @return 成功返回槽位索引 (>=0), 池满返回负数错误码
 */
int osal_pool_claim(osal_pool_t* pool) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 释放指定槽位
 * @param[in] pool 槽位池指针
 * @param[in] slot_index 槽位索引
 * @return 成功返回 MINI_OK, 索引越界返回 MINI_ERR_INVAL
 */
int osal_pool_release(osal_pool_t* pool, int slot_index) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 检查槽位是否被占用
 * @param[in] pool 槽位池指针
 * @param[in] slot_index 槽位索引
 * @return 占用返回 true
 */
bool osal_pool_is_used(osal_pool_t* pool, int slot_index);
/* -------------------------------------------------------------------------- */

/*任务 API*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 任务 (stack_size 单位: 字节, 所有后端统一) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 创建任务
 * @param[in] name 任务名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 优先级 (后端语义见文件头说明)
 * @param[in] entry 任务入口函数
 * @param[in] param 任务参数
 * @param[in] core_id 核心号 (-1=任意核心)
 * @return 成功返回 MINI_OK, 创建失败返回负数错误码
 */
int osal_task_create(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param,
                     int core_id) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 启动调度器 (OS 后端的统一封装)
 * @return void
 * @details 在 OS 后端 (FreeRTOS / RT-Thread 等) 调用本函数启动内核调度器:
 *          FreeRTOS 转发 vTaskStartScheduler(); RT-Thread 转发
 *          rt_system_scheduler_start()。裸机后端 (CONFIG_OSAL_NULL) 不使用本函数,
 *          其调度器由 xtask 的 xscheduler_start() 启动, 故本函数不在裸机后端声明。
 *          应在所有 osal_task_create() 之后、进入主循环前调用一次。
 */
void osal_scheduler_start(void);
/* -------------------------------------------------------------------------- */

/*任务句柄 API*/
/* -------------------------------------------------------------------------- */
typedef void* osal_task_handle_t;

/**
 * @brief 创建任务并回传句柄
 * @param[in] name 任务名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 优先级
 * @param[in] entry 任务入口函数
 * @param[in] param 任务参数
 * @param[in] core_id 核心号 (-1=任意核心)
 * @param[out] out_handle 回传任务句柄
 * @return 成功返回 MINI_OK, 创建失败返回负数错误码
 */
int osal_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, osal_task_entry_t entry, void* param, int core_id,
                            osal_task_handle_t* out_handle) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 当前任务自我删除
 */
void osal_task_self_delete(void);
/**
 * @brief 删除指定任务
 * @param[in] task 任务句柄
 */
void osal_task_delete(osal_task_handle_t task);
/**
 * @brief 查询任务是否在运行
 * @param[in] task 任务句柄
 * @return 运行中返回 true
 */
bool osal_task_is_running(osal_task_handle_t task);
/**
 * @brief 获取任务名
 * @param[in] task 任务句柄
 * @return 任务名
 */
const char* osal_task_get_name(osal_task_handle_t task);
/**
 * @brief 获取任务栈最低水位 (栈溢出监控)
 * @param[in] task 任务句柄
 * @return 剩余最小栈字节数
 */
uint32_t osal_task_get_stack_watermark(osal_task_handle_t task);
/* -------------------------------------------------------------------------- */

/*队列 API*/
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* 队列 (定长消息队列) */
/* 任务 / ISR 严格分离, 禁止自动推导: */
/* osal_queue_send() / osal_queue_receive() */
/* — 仅任务上下文, ISR 中调用返回 false. */
/* osal_queue_send_from_isr() / osal_queue_receive_from_isr() */
/* — 仅 ISR 上下文, 通过 px_yield_required 上报 yield 需求, */
/* 不在内部调用 osal_yield_from_isr(). */
/* ISR 典型用法: */
/* bool woken = false; */
/* osal_queue_send_from_isr(q, &item, &woken); */
/* osal_yield_from_isr(woken);   // ISR 最外层出口 */
/* -------------------------------------------------------------------------- */
typedef void* osal_queue_handle_t;

/**
 * @brief 创建定长消息队列
 * @param[in] queue_len 队列容量 (条目数)
 * @param[in] item_size 单条目字节数
 * @return 队列句柄; 创建失败返回 NULL
 */
osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 删除消息队列
 * @param[in] queue 队列句柄
 */
void osal_queue_delete(osal_queue_handle_t queue);
/**
 * @brief 入队 (task 上下文)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送条目
 * @param[in] timeout_ms 超时毫秒数
 * @return 成功返回 true, 超时返回 false
 */
bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 入队 (ISR 上下文, 不内部 yield)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送条目
 * @param[out] px_yield_required 是否需要上下文切换
 * @return 成功返回 true
 */
bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 出队 (task 上下文)
 * @param[in] queue 队列句柄
 * @param[out] item 回传接收条目
 * @param[in] timeout_ms 超时毫秒数
 * @return 成功返回 true, 超时返回 false
 */
bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 出队 (ISR 上下文, 不内部 yield)
 * @param[in] queue 队列句柄
 * @param[out] item 回传接收条目
 * @param[out] px_yield_required 是否需要上下文切换
 * @return 成功返回 true
 */
bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;
/* -------------------------------------------------------------------------- */

/*事件组 API*/
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_OSAL_EVENT
/* -------------------------------------------------------------------------- */
/* 事件组 (32 位标志集, 多源事件汇聚)                                  */
/* 与二值信号量的区别: 一个事件组承载 32 个独立位, 等待方可要求     */
/* "任一位" (OR) 或 "全部位" (AND), 满足后可选择自动清除 (消费语义)。 */
/*                                                                        */
/* AND/OR 与 auto_clear 在创建时固定: mini-os 的等待模式是创建期属性   */
/* (mini_os_event_type_t), 而 FreeRTOS (xWaitForAllBits / xClearOnExit) 与  */
/* RT-Thread (RT_EVENT_FLAG_AND/OR/CLEAR) 是等待期参数; 本层取三后端的   */
/* 最小公约数 (创建期固定), FreeRTOS / RT-Thread 后端在等待时把创建期   */
/* 保存的模式传给内核, 因此四后端行为一致。                             */
/*                                                                        */
/* 任务 / ISR 严格分离 (同队列/信号量): osal_event_set() 仅任务上下文, */
/* osal_event_set_from_isr() 仅 ISR 且只置 *px_yield_required, 由 ISR 最外 */
/* 层出口统一调 osal_yield_from_isr()。                                */
/* 本组 API 由 CONFIG_OSAL_EVENT 门控 (默认关闭), 开启时会自动带上所属 */
/* 后端的事件组开关 (MINI_OS_EVENT / FREERTOS_EVENT_GROUPS /            */
/* RTTHREAD_EVENT); 关闭时下列符号不存在, 误用会在编译期报错。        */
/* -------------------------------------------------------------------------- */

#ifndef OSAL_EVENT_STORAGE_SIZE
#define OSAL_EVENT_STORAGE_SIZE 96 /* 足够容纳 struct osal_event (内嵌内核事件组对象) */
#endif

#ifndef OSAL_EVENT_POOL_SIZE
#define OSAL_EVENT_POOL_SIZE 4 /* 池分配方式可创建的事件组上限 */
#endif

/**
 * @brief 事件组可用标志位数 (跨后端最小公约数)
 * @details FreeRTOS 把 32 位标志字的最高 8 位留作内核控制位
 *          (event_groups.h: eventEVENT_BITS_CONTROL_BYTES = 0xFF000000,
 *          分别为 CLEAR_ON_EXIT / UNBLOCKED_DUE_TO_BIT_SET /
 *          WAIT_FOR_ALL_BITS 等), 只有 bit0..bit23 能给应用用;
 *          mini-os / RT-Thread / 裸机后端支持全 32 位。
 *          为让同一份业务代码在四后端上行为一致, OSAL 统一按 24 位
 *          约束: set / clear / wait 的 bits 只要落在高位区就返回
 *          OSAL_ERR_INVAL (FreeRTOS 后端上若放行会直接踩内核
 *          configASSERT 而停机)。
 */
#define OSAL_EVENT_BITS 24U

/** @brief OSAL_EVENT_BITS 对应的可用位掩码 (高位区为保留位, 禁止使用) */
#define OSAL_EVENT_MASK 0x00FFFFFFU

/**
 * @brief 事件组等待模式 (创建时固定, 不可更改)
 */
typedef enum
{
    OSAL_EVENT_OR = 0,  /* 等待位中任一位被置位即满足 */
    OSAL_EVENT_AND = 1, /* 等待位全部被置位才满足 */
} osal_event_mode_t;

/**
 * @brief 创建事件组 (池分配)
 * @param[out] out 回传事件组句柄
 * @param[in] mode 等待模式 (OSAL_EVENT_OR / OSAL_EVENT_AND)
 * @param[in] auto_clear true = 等待成功后自动消费 (清除) 已满足的位;
 *                       false = 保留标志, 需调 osal_event_clear() 手动清
 * @return 成功返回 OSAL_OK; out 为空或 mode 非法返回 OSAL_ERR_INVAL;
 *         池耗尽返回 OSAL_ERR_NOMEM
 * @note 初始标志为 0 (无任何事件)
 */
int osal_event_create(struct osal_event** out, osal_event_mode_t mode, bool auto_clear) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 创建事件组 (静态存储)
 * @param[out] out 回传事件组句柄 (指向 storage)
 * @param[in] storage 存储缓冲 (需 >= OSAL_EVENT_STORAGE_SIZE 字节)
 * @param[in] storage_size 缓冲大小
 * @param[in] mode 等待模式 (OSAL_EVENT_OR / OSAL_EVENT_AND)
 * @param[in] auto_clear true = 等待成功后自动消费已满足的位
 * @return 成功返回 OSAL_OK; 参数非法或缓冲不够返回 OSAL_ERR_INVAL
 * @note 静态创建的事件组无需 (也不可) 调 osal_event_destroy()
 */
int osal_event_create_static(struct osal_event** out, void* storage, size_t storage_size, osal_event_mode_t mode,
                             bool auto_clear) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 销毁事件组 (仅池分配创建的需要)
 * @param[in] ev 事件组句柄 (可为 NULL, 忽略)
 * @note 仍有任务阻塞在该事件组上时销毁属于调用方错误:
 *       mini-os 后端会保留池槽不归还 (避免描述符被复用后
 *       旧等待者阻塞到语义已变的对象上);
 *       FreeRTOS 的 vEventGroupDelete 会自行把阻塞任务挖到
 *       pending ready 队列使其带当前标志返回, 无遗留等待者。
 */
void osal_event_destroy(struct osal_event* ev);
/**
 * @brief 置位事件标志 (任务上下文)
 * @param[in] ev 事件组句柄
 * @param[in] bits 要置位的标志掩码 (按位 OR 入现有标志)
 * @return 成功返回 OSAL_OK; ev 为空或 bits==0 返回 OSAL_ERR_INVAL
 * @note 唤醒所有已满足的等待者, 并在需要时主动让出 CPU;
 *       bits==0 被拒: 空置位无法唤醒任何人, 通常是调用方逻辑错误。
 */
int osal_event_set(struct osal_event* ev, uint32_t bits) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 置位事件标志 (ISR 上下文, 不内部 yield)
 * @param[in] ev 事件组句柄
 * @param[in] bits 要置位的标志掩码
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return 成功返回 OSAL_OK; ev 为空或 bits==0 返回 OSAL_ERR_INVAL
 * @note ISR 典型用法:
 *       bool woken = false;
 *       osal_event_set_from_isr(ev, BIT(0), &woken);
 *       osal_yield_from_isr(woken);   // ISR 最外层出口
 * @warning FreeRTOS 后端的 xEventGroupSetBitsFromISR 经软件定时器守护
 *          任务代理, 因此必须开启 FREERTOS_USE_TIMERS (Kconfig 已 select)。
 */
int osal_event_set_from_isr(struct osal_event* ev, uint32_t bits, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 清除事件标志
 * @param[in] ev 事件组句柄
 * @param[in] bits 要清除的标志掩码
 * @return 成功返回 OSAL_OK; ev 为空或 bits==0 返回 OSAL_ERR_INVAL
 * @note 任务与 ISR 均可调用 (不阻塞、不唤醒、不 yield);
 *       auto_clear=false 时用本接口手动消费已处理的事件。
 */
int osal_event_clear(struct osal_event* ev, uint32_t bits) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 读取当前事件标志 (不阻塞、不消费)
 * @param[in] ev 事件组句柄
 * @param[out] out_bits 回传当前标志 (可为 NULL, 则仅做存在性检查)
 * @return 成功返回 OSAL_OK; ev 为空返回 OSAL_ERR_INVAL
 */
int osal_event_get(struct osal_event* ev, uint32_t* out_bits) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 等待事件标志
 * @param[in] ev 事件组句柄
 * @param[in] bits 等待的标志掩码 (> 0)
 * @param[in] timeout_ms 超时毫秒数 (0 = 不阻塞立即返回,
 *                       OSAL_WAIT_FOREVER = 一直等到满足)
 * @param[out] out_bits 回传实际已置位的相关位 (可为 NULL)
 * @return 满足返回 OSAL_OK; 超时返回 OSAL_ERR_TIMEOUT;
 *         不阻塞且未满足返回 OSAL_ERR_TIMEOUT; 参数非法返回 OSAL_ERR_INVAL
 * @note 是否满足由创建时的 mode 决定 (OR: bits 中任一位; AND: bits 全部);
 *       auto_clear=true 时已满足的位在返回前被消费。
 * @warning 仅任务上下文可调用; ISR 中调用会阻塞导致死锁。
 */
int osal_event_wait(struct osal_event* ev, uint32_t bits, uint32_t timeout_ms, uint32_t* out_bits) MINI_WARN_UNUSED_RESULT;
#endif /* CONFIG_OSAL_EVENT */
/* -------------------------------------------------------------------------- */

/*安全互锁与硬件关断*/
/* -------------------------------------------------------------------------- */
/**
 * @brief panic 互锁 (weak, 默认 trap, 板级可覆盖)
 */
void osal_panic_interlock(void);

/**
 * @brief 硬件安全关断 (weak, 默认 trap, 板级可覆盖)
 */
void safety_hardware_shutdown(void);

/**
 * @brief 板级硬件安全关断 (必须由板级强制实现)
 * @param[in] reason 关断原因 (用于日志/黑匣子)
 */
void system_safety_hardware_shutdown(const char* reason);
/* -------------------------------------------------------------------------- */

/**
 * @brief Panic
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return void
 * @details Panic时, 使用 osal_log_fatal 输出致命原因
 * @details Panic时, 使用 system_safety_hardware_shutdown 板级硬件安全关断
 * @details Panic时, 驻留死循环, 等待外部硬件看门狗复位
 */
#undef OSAL_PANIC
#define OSAL_PANIC(fmt, ...)                                                                                                                         \
    do                                                                                                                                               \
    {                                                                                                                                                \
        osal_log_fatal(fmt, ##__VA_ARGS__);                                                                                                          \
        system_safety_hardware_shutdown("OSAL_PANIC");                                                                                               \
        while (1)                                                                                                                                    \
        {                                                                                                                                            \
            ;                                                                                                                                        \
        }                                                                                                                                            \
    } while (0)

/**
 * @brief 关键断言
 * @param[in] cond 条件
 * @param[in] fmt 格式化字符串
 * @param ... 可变参数
 * @return void
 * @details 关键断言时, 使用 osal_log_critical_assert 输出关键原因
 * @details 关键断言时, 使用 system_safety_hardware_shutdown 板级硬件安全关断
 */
#define MINI_CRITICAL_ASSERT(cond, fmt, ...)                                                                                                         \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(cond))                                                                                                                                 \
        {                                                                                                                                            \
            osal_log_critical_assert(__FILE__, __LINE__, fmt, ##__VA_ARGS__);                                                                        \
            system_safety_hardware_shutdown("MINI_CRITICAL_ASSERT");                                                                                 \
            while (1)                                                                                                                                \
            {                                                                                                                                        \
                ;                                                                                                                                    \
            }                                                                                                                                        \
        }                                                                                                                                            \
    } while (0)
/* -------------------------------------------------------------------------- */

/*日志 API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 分级日志输出 (info/warn/error)
 * @param[in] level 日志级别 (OSAL_LOG_*)
 * @param[in] tag 日志标签
 * @param[in] fmt printf 格式串
 * @param[in] ... 格式化参数
 */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...);
/**
 * @brief 致命日志输出 (panic 前调用)
 * @param[in] fmt printf 格式串
 * @param[in] ... 格式化参数
 */
void osal_log_fatal(const char* fmt, ...) MINI_FMT_PRINTF(1, 2);
/**
 * @brief 关键断言失败日志 (含文件/行号)
 * @param[in] file 源文件名
 * @param[in] line 行号
 * @param[in] fmt printf 格式串
 * @param[in] ... 格式化参数
 */
void osal_log_critical_assert(const char* file, int line, const char* fmt, ...) MINI_FMT_PRINTF(3, 4);
/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_OSAL_H */
