/**
 * @file mini_backend.h
 * @author H-000-H
 * @brief 统一 OSAL 后端接口: IPC + 任务 + 临界区 + 内存
 * @note  仓库内代码统一走本接口; app 层可直接走原生接口(可选)
 * @note  裸机不提供信号量/队列/任务, 误用即链接报错
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINI_BACKEND_H
#define MINI_BACKEND_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* -------------------------------------------------------------------------- */
/* 公共常量                                                                    */
/* -------------------------------------------------------------------------- */

/** @brief 永久等待 (互斥锁超时 / 信号量等待 / 队列收发) */
#define MINI_WAIT_FOREVER UINT32_MAX

#ifndef MINI_LOCK_TIMEOUT_DEFAULT_MS
#define MINI_LOCK_TIMEOUT_DEFAULT_MS 100U /**< 默认锁超时 (毫秒), 板级可覆盖 */
#endif

#ifndef MINI_MUTEX_STORAGE_SIZE
#define MINI_MUTEX_STORAGE_SIZE 128 /**< 互斥锁静态存储字节数 (覆盖各后端最大内核对象) */
#endif

#ifndef MINI_SEM_STORAGE_SIZE
#define MINI_SEM_STORAGE_SIZE 128 /**< 二值信号量静态存储字节数 */
#endif

/* -------------------------------------------------------------------------- */
/* 互斥锁 (静态存储; 必须支持递归)                                             */
/* -------------------------------------------------------------------------- */
/**
 * @brief 互斥锁不透明句柄
 * @note 类型在创建时绑定, 运行期不可变:
 *       mini_mutex_create_static            普通锁 (非递归)
 *       mini_mutex_create_static_recursive  递归锁 (同一持有者可嵌套加锁)
 * @note 禁止在中断上下文调用 create / lock / unlock / destroy; 临界区请用
 *       mini_critical_enter / mini_critical_exit
 */
typedef struct mini_mutex mini_mutex_t;

/**
 * @brief 创建普通 (非递归) 互斥锁, 存储在调用方给出的缓冲区
 * @param[out] out 回传互斥锁句柄
 * @param[in] storage 存储缓冲区 (长度须 >= MINI_MUTEX_STORAGE_SIZE, 4 字节对齐)
 * @param[in] storage_size 缓冲区字节数
 * @return 成功返回 MINI_OK; 参数非法返回 MINI_ERR_INVAL; 中断中调用返回 MINI_ERR_ISR
 */
mt_err_t mini_mutex_create_static(mini_mutex_t** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 创建递归互斥锁, 存储在调用方给出的缓冲区
 * @param[out] out 回传互斥锁句柄
 * @param[in] storage 存储缓冲区 (长度须 >= MINI_MUTEX_STORAGE_SIZE, 4 字节对齐)
 * @param[in] storage_size 缓冲区字节数
 * @return 成功返回 MINI_OK; 参数非法返回 MINI_ERR_INVAL; 中断中调用返回 MINI_ERR_ISR
 * @note 同一持有者可重复 lock, 须等量 unlock 才真正释放
 */
mt_err_t mini_mutex_create_static_recursive(mini_mutex_t** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 锁定互斥锁
 * @param[in] mtx 互斥锁句柄
 * @param[in] timeout_ms 超时毫秒数 (0 = 不阻塞; MINI_WAIT_FOREVER = 永久等待)
 * @return 成功返回 MINI_OK; 超时返回 MINI_ERR_TIMEOUT; 参数非法返回 MINI_ERR_INVAL
 * @note 仅任务上下文可调用
 */
mt_err_t mini_mutex_lock(mini_mutex_t* mtx, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 释放互斥锁
 * @param[in] mtx 互斥锁句柄
 * @return 成功返回 MINI_OK; 未持有返回 MINI_ERR_IO; 参数非法返回 MINI_ERR_INVAL
 */
mt_err_t mini_mutex_unlock(mini_mutex_t* mtx) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 销毁互斥锁 (仅需在对象生命周期结束时调用; 静态存储的锁可不调用)
 * @param[in] mtx 互斥锁句柄 (可为 NULL, 忽略)
 * @note 仍有等待者时销毁属于调用方错误
 */
void mini_mutex_destroy(mini_mutex_t* mtx);

/* -------------------------------------------------------------------------- */
/* 内存 (与抽象层无关, 仅保持分发语义)                                         */
/* -------------------------------------------------------------------------- */
/**
 * @brief 分配内存 (malloc, 不清零)
 * @param[in] size 字节数
 * @return 内存指针; 失败返回 NULL
 */
void* mini_malloc(size_t size);

/**
 * @brief 分配并清零内存 (calloc)
 * @param[in] count 元素个数
 * @param[in] size 单元素字节数
 * @return 内存指针; 失败返回 NULL
 */
void* mini_calloc(size_t count, size_t size);

/**
 * @brief 释放内存
 * @param[in] ptr 内存指针 (可为 NULL)
 * @return 成功返回 MINI_OK
 */
mt_err_t mini_free(void* ptr);

/* -------------------------------------------------------------------------- */
/* ISR 出口上下文切换                                                          */
/* -------------------------------------------------------------------------- */
/**
 * @brief ISR 出口请求上下文切换
 * @param[in] yield_required _from_isr 系列回传的切换需求 (false 直接返回)
 * @details 契约: *_from_isr 系列只置 *px_yield_required, **绝不内部 yield**;
 *          由 ISR 最外层出口统一调用本函数一次。
 *          mini-os: 转发内核 mini_os_schedule_yield_isr(), 内部自判就绪位图,
 *                   仅更高优先级就绪时才置 PendSV, 重复调用无害;
 *          FreeRTOS: portYIELD_FROM_ISR;
 *          裸机: 空实现 (xtask 不在中断内切换上下文)。
 */
void mini_yield_from_isr(bool yield_required);

/* -------------------------------------------------------------------------- */
/* 任务 (全后端声明, 含裸机)                                                   */
/* -------------------------------------------------------------------------- */
/* 任务族在裸机下**也必须存在符号**: 仓内 board/src/task_utils.c 与 system 的
 * task_manager 是裸机配置也会编入的代码 (根 CMakeLists 的源清单里没有按后端裁剪)。
 * 裸机契约: 符号可链接, 调用返回 MINI_ERR_NOTSUPP ——
 * 而**不是**"裸机下不声明、误用即链接报错"。
 * 注意: 裸机的"任务"是 xtask 的 x_scheduler_task_create() (周期回调模型),
 * 与这里的线程入口语义不通用, 故不作转发, 只报 NOTSUPP。 */

/**
 * @brief 任务句柄
 */
typedef void* mini_task_handle_t;

/**
 * @brief 任务入口
 */
typedef void (*mini_task_entry_t)(void* param);

/**
 * @brief 创建任务并回传句柄
 * @param[in] name 任务名
 * @param[in] stack_size 栈大小 (字节)
 * @param[in] priority 优先级 (后端原生语义: FreeRTOS 越大越高; RT-Thread / mini-os 越小越高)
 * @param[in] entry 任务入口
 * @param[in] param 任务参数
 * @param[in] core_id 核心号 (-1 = 任意核心)
 * @param[out] out_handle 回传任务句柄
 * @return 成功返回 MINI_OK; 创建失败返回负错误码; 裸机返回 MINI_ERR_NOTSUPP
 */
mt_err_t mini_task_create_handle(const char* name, uint32_t stack_size, uint32_t priority, mini_task_entry_t entry, void* param, int core_id,
                            mini_task_handle_t* out_handle) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 当前任务自我删除 (不返回)
 */
void mini_task_self_delete(void);

/**
 * @brief 删除指定任务
 * @param[in] task 任务句柄
 */
void mini_task_delete(mini_task_handle_t task);

/**
 * @brief 查询任务是否在运行
 * @param[in] task 任务句柄
 * @return 运行中返回 true
 */
bool mini_task_is_running(mini_task_handle_t task);

/**
 * @brief 获取任务名
 * @param[in] task 任务句柄
 * @return 任务名 (后端不提供时返回占位串)
 */
const char* mini_task_get_name(mini_task_handle_t task);

/**
 * @brief 获取任务栈最低水位 (栈溢出监控)
 * @param[in] task 任务句柄
 * @return 剩余最小栈字节数 (后端不提供时返回 0)
 */
uint32_t mini_task_get_stack_watermark(mini_task_handle_t task);

/* -------------------------------------------------------------------------- */
/* 调度器冻结 (fail-fast 单向冻结, 全后端)                                     */
/* -------------------------------------------------------------------------- */
/**
 * @brief 冻结调度器 (单向不可恢复, 进入安全死锁状态)
 * @details 与 mini_irq_disable 配对使用于 fail-fast / 安全停机路径:
 *          FreeRTOS 转发 vTaskSuspendAll(); RT-Thread 转发 rt_enter_critical();
 *          mini-os 与裸机无 suspend-all 这一层, 退化为关中断 (关中断后 PendSV /
 *          SysTick 都被屏蔽, 调度器本就不会再切上下文) —— 与原后端语义一致。
 * @note 调用后系统不再推进, 只能靠外部硬件看门狗复位; 不是给业务用的锁。
 */
void mini_sched_freeze(void);

/* -------------------------------------------------------------------------- */
/* 队列 (全后端声明, 含裸机)                                                   */
/* -------------------------------------------------------------------------- */
/* 队列族在裸机下**必须有可用实现**: EventBus 在裸机配置下也编入, 用的就是这条队列
 * (Kconfig OS_BARE_MAX_QUEUES 的帮助写明"开启 EVENT_BUS 时自动 +1, EventBus 需要
 * 一个队列")。故这里的裸机实现是 fifo_spsc 静态池真实现, 而不是 NOTSUPP 桩。 */

/**
 * @brief 定长消息队列句柄 (兼作网络层邮箱)
 */
typedef struct mini_queue mini_queue_t;

/**
 * @brief 创建定长消息队列
 * @param[in] queue_len 队列容量 (条目数)
 * @param[in] item_size 单条目字节数
 * @return 队列句柄; 创建失败 (池满/参数非法/超缓冲) 返回 NULL
 */
mini_queue_t* mini_queue_create(size_t queue_len, size_t item_size) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 删除消息队列
 * @param[in] queue 队列句柄 (可为 NULL, 忽略)
 */
void mini_queue_delete(mini_queue_t* queue);

/**
 * @brief 入队 (任务上下文)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送条目
 * @param[in] timeout_ms 超时毫秒数 (裸机非阻塞, 忽略)
 * @return 成功返回 true, 满/超时返回 false
 */
bool mini_queue_send(mini_queue_t* queue, const void* item, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 入队 (ISR 上下文, 不内部 yield)
 * @param[in] queue 队列句柄
 * @param[in] item 待发送条目
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return 成功返回 true
 */
bool mini_queue_send_from_isr(mini_queue_t* queue, const void* item, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 出队 (任务上下文)
 * @param[in] queue 队列句柄
 * @param[out] item 回传接收条目
 * @param[in] timeout_ms 超时毫秒数 (MINI_WAIT_FOREVER = 永久等待; 裸机为忙等)
 * @return 成功返回 true, 空/超时返回 false
 */
bool mini_queue_receive(mini_queue_t* queue, void* item, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 出队 (ISR 上下文, 不内部 yield)
 * @param[in] queue 队列句柄
 * @param[out] item 回传接收条目
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return 成功返回 true
 */
bool mini_queue_receive_from_isr(mini_queue_t* queue, void* item, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;

/* -------------------------------------------------------------------------- */
/* 信号量 / 池化创建 / 调度器启动: 仅 OS 后端提供 (裸机下不声明)                */
/* -------------------------------------------------------------------------- */
/* 这两类的裸机消费者只有 lwIP 移植层的 #if NO_SYS == 0 分支, 而裸机配置取
 * NO_SYS=1 (见 Kconfig), 故裸机下确实无调用点 —— 误用即链接报错。 */
/* TODO(backend-rename): 条件编译符号随后端符号统一改名阶段收口 */
#if !defined(CONFIG_OS_BARE)

/**
 * @brief 创建普通 (非递归) 互斥锁, 对象取自后端内部静态池
 * @param[out] out 回传互斥锁句柄
 * @return 成功返回 MINI_OK; 池耗尽返回 MINI_ERR_NOMEM; 中断中调用返回 MINI_ERR_ISR
 * @details 与 mini_mutex_create_static 的唯一区别是**不需要调用方提供存储**。
 *          存在这条路径是因为网络移植层需要在运行期按需创建锁而拿不出定长静态
 *          数组 (lwIP 的 sys_mutex_new 由内核在 TCP 连接/定时器建立时调用, 数量
 *          由 lwIP 配置决定)。池尺寸由板级给出, 池耗尽返回 NOMEM 而不是崩溃。
 * @note 池内对象同样由 mini_mutex_destroy 归还槽位; 递归锁请用 create_static_recursive。
 */
mt_err_t mini_mutex_create(mini_mutex_t** out) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 二值信号量不透明句柄 (初始计数 0, 多次 post 合并为 1)
 */
typedef struct mini_sem mini_sem_t;

/**
 * @brief 创建二值信号量, 存储在调用方给出的缓冲区
 * @param[out] out 回传信号量句柄
 * @param[in] storage 存储缓冲区 (长度须 >= MINI_SEM_STORAGE_SIZE)
 * @param[in] storage_size 缓冲区字节数
 * @return 成功返回 MINI_OK; 参数非法返回 MINI_ERR_INVAL
 */
mt_err_t mini_sem_create_binary_static(mini_sem_t** out, void* storage, size_t storage_size) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 创建二值信号量, 对象取自后端内部静态池
 * @param[out] out 回传信号量句柄
 * @return 成功返回 MINI_OK; 池耗尽返回 MINI_ERR_NOMEM
 * @details 同 mini_mutex_create: 为网络移植层 (lwIP 的 sys_sem_new) 提供不需要
 *          调用方存储的创建路径。池内对象由 mini_sem_destroy 归还槽位。
 */
mt_err_t mini_sem_create_binary(mini_sem_t** out) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 等待信号量
 * @param[in] sem 信号量句柄
 * @param[in] timeout_ms 超时毫秒数 (MINI_WAIT_FOREVER = 永久等待)
 * @return 成功返回 MINI_OK; 超时返回 MINI_ERR_TIMEOUT
 */
mt_err_t mini_sem_wait(mini_sem_t* sem, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 释放信号量 (任务上下文)
 * @param[in] sem 信号量句柄
 * @return 成功返回 true
 */
bool mini_sem_post(mini_sem_t* sem) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 释放信号量 (ISR 上下文, 不内部 yield)
 * @param[in] sem 信号量句柄
 * @param[out] px_yield_required 是否需要上下文切换 (可为 NULL)
 * @return 成功返回 true
 */
bool mini_sem_post_from_isr(mini_sem_t* sem, bool* px_yield_required) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 销毁信号量
 * @param[in] sem 信号量句柄 (可为 NULL, 忽略)
 */
void mini_sem_destroy(mini_sem_t* sem);

/**
 * @brief 启动内核调度器 (OS 后端的统一封装)
 * @return 成功启动返回 MINI_OK; 后端不支持时返回负错误码
 * @details 它是 **OS 后端独有**的入口。
 *          裸机后端不使用本函数 —— 其调度器由 xtask 的 xscheduler_start() 启动
 *          (且必须在 mini_tree_start_tasks() 之后调用), 故本函数与信号量 / 队列 /
 *          任务同域声明, 裸机下误用即链接报错。
 *
 *          调用时机, 见 system_c/include/system_init.h 的启动时序:
 *            mini_tree_pre_os_init()  板级外设 / 驱动注册 / 静态分配
 *            mini_tree_start_tasks()  创建框架任务 (内核此时必须已就绪)
 *            mini_scheduler_start()   本函数
 *            system_init_complete()   释放全局中断
 *          正常情况下本函数不返回 (控制权交给内核调度器)。
 *
 *          mini-os: 先配置并启动 tick (mini_os_systick_init), 再
 *                   mini_os_schedule_start() (设 PendSV/SysTick 优先级、priming PSP、
 *                   触发首次上下文切换并开中断)。
 *          FreeRTOS: vTaskStartScheduler(); RT-Thread: rt_system_scheduler_start()。
 * @note tick 源只在这里启动, **不在内核自举钩子里**: 自举只建立内核数据结构
 *       (mini_os_schedule_init) 与 idle 线程, 不产生任何中断副作用。
 */
mt_err_t mini_scheduler_start(void);

#endif /* !CONFIG_OS_BARE */

#ifdef __cplusplus
}
#endif

#endif /* MINI_BACKEND_H */
