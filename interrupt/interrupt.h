/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file interrupt.h
 *@brief 中断上下部系统 — VIRQ 虚拟中断号 + 下半部工作队列一体化
 *@author H-000-H
 *@details
 *   @note 上半部 (ISR): top_half 回调 + interrupt_virtual_dispatch 内自动 submit
 *   @note 下半部 (主循环): interrupt_bottom_half_poll() → bottom_half_run_pending() 执行回调
 *   @note 裸机路径 (CONFIG_OSAL_NULL): 主循环主动 poll
 *   @note RTOS 路径: bottom_half_task 任务 sem 唤醒 (条件编译保留)
 *   @warning ISR 内禁止: printf / 上锁 / 长时间阻塞; 重活必须放下半部
 */

#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#include "buffer.h"
#include "compiler_compat.h"
#include "osal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* -------------------------------------------------------------------------- */
/*                              VIRQ 虚拟中断号 */
/* -------------------------------------------------------------------------- */
#define VIRTUAL_IRQ_BLOCK_SIZE 8U /**<可以改但是必须 2 的 x 次方 */

/**< 虚拟中断块表 */
#define VIRTUAL_IRQ_BLOCK_TABLE(X)                                                                                                                   \
    X(system)                                                                                                                                        \
    X(tim)                                                                                                                                           \
    X(gpio)                                                                                                                                          \
    X(adc)                                                                                                                                           \
    X(uart)                                                                                                                                          \
    X(spi)                                                                                                                                           \
    X(i2c)                                                                                                                                           \
    X(i2s)                                                                                                                                           \
    X(user)

/**< 阶段一：自动生成索引序号 */
#define BLOCK_INDEX_ENUM(name) VIRTUAL_BLOCK_IDX_##name,
enum
{
    VIRTUAL_IRQ_BLOCK_TABLE(BLOCK_INDEX_ENUM) VIRTUAL_BLOCK_COUNT
};
#undef BLOCK_INDEX_ENUM

/**< 阶段二：生成基地址枚举 */
#define VIRTUAL_IRQ_BLOCK_ENUM(name) VIRTUAL_IRQ_##name = (uint32_t)(VIRTUAL_BLOCK_IDX_##name * VIRTUAL_IRQ_BLOCK_SIZE),

typedef enum
{
    VIRTUAL_IRQ_BLOCK_TABLE(VIRTUAL_IRQ_BLOCK_ENUM) VIRTUAL_IRQ_MAX_BASE = (uint32_t)(VIRTUAL_BLOCK_COUNT * VIRTUAL_IRQ_BLOCK_SIZE)
} virtual_irq_base_t;
#undef VIRTUAL_IRQ_BLOCK_ENUM

/**< 阶段三：辅助宏 */
#define VIRQ(name, idx) ((VIRTUAL_IRQ_##name) + (idx))
#define VIRTUAL_IRQ_BLOCK(x) (VIRTUAL_IRQ_BLOCK_##x)

/**< 上半部回调类型 — 返回非零表示需要 submit 下半部 */
typedef int (*interrupt_top_half_t)(void* arg, uint16_t irq_num);

/**< 下半部工作函数类型 */
typedef void (*bottom_half_fn_t)(void* arg);

extern interrupt_top_half_t     interrupt_virtual_top_half[VIRTUAL_IRQ_MAX_BASE];
extern struct bottom_half_work* interrupt_virtual_bottom_half_work[VIRTUAL_IRQ_MAX_BASE];
extern void*                    interrupt_virtual_arg[VIRTUAL_IRQ_MAX_BASE];

/**< ADC DMA 下半部全局工作项 (fn/arg 由 hal_adc.c 在初始化时绑定) */
extern struct bottom_half_work g_adc_dma_bottom_half_work;
/**< I2S DMA 下半部全局工作项 (fn/arg 由板级 hal_i2s 在初始化时绑定) */
extern struct bottom_half_work g_i2s_bottom_half_work;

/* -------------------------------------------------------------------------- */
/*                              VIRQ 外围弱钩子 (板级强符号覆盖) */
/* -------------------------------------------------------------------------- */
/**
 * @brief VIRQ 外围钩子 — 原散落于 hal 各外设头，统一集中到本模块声明
 * @note  板级以强符号覆盖 (弱实现在 interrupt.c); 未覆盖时上半部返回
 *        MINI_IRQ_ENTRY_NOBOTTOM (不需要下半部)。
 * @note  上半部约定: 返回 MINI_IRQ_ENTRY_BOTTOM(1) = 需要 submit 下半部;
 *        MINI_IRQ_ENTRY_NOBOTTOM(0) = 不需要。
 */
/**
 * @brief ADC VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_adc_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief I2S VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_i2s_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief SPI VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_spi_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief CAN VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_can_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief DAC VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_dac_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief TIM VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_tim_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief UART VIRQ 上半部钩子 (板级以强符号覆盖)
 * @param[in] arg 中断上下文参数
 * @param[in] irq_num 虚拟中断号
 * @return MINI_IRQ_ENTRY_BOTTOM(1)=需 submit 下半部; MINI_IRQ_ENTRY_NOBOTTOM(0)=不需要
 */
int hal_virtual_uart_irq_callback(void* arg, uint16_t irq_num);
/**
 * @brief ADC DMA 下半部处理函数 (hal_adc.c 初始化时绑定到 g_adc_dma_bottom_half_work)
 * @param[in] arg 下半部参数
 */
void hal_adc_dma_bottom_half_handler(void* arg);
/**
 * @brief I2S DMA 下半部处理函数 (板级 hal_i2s 初始化时绑定到 g_i2s_bottom_half_work)
 * @param[in] arg 下半部参数
 */
void hal_i2s_dma_bottom_half_handler(void* arg);

/* -------------------------------------------------------------------------- */
/*                              下半部配置参数 */
/* -------------------------------------------------------------------------- */
/**< 队列深度, 必须是 2 的幂 (Kconfig CONFIG_BOTTOM_HALF_QUEUE_DEPTH 或工程侧覆盖) */
#ifndef BOTTOM_HALF_QUEUE_DEPTH
#ifdef CONFIG_BOTTOM_HALF_QUEUE_DEPTH
#define BOTTOM_HALF_QUEUE_DEPTH CONFIG_BOTTOM_HALF_QUEUE_DEPTH
#else
#define BOTTOM_HALF_QUEUE_DEPTH 32U
#endif
#endif

/**< bottom_half_task 专用任务默认参数 (可在工程侧覆盖) */
#ifndef BOTTOM_HALF_TASK_STACK_SIZE
#define BOTTOM_HALF_TASK_STACK_SIZE 2048U
#endif

#ifndef BOTTOM_HALF_TASK_PRIORITY
#define BOTTOM_HALF_TASK_PRIORITY 5U
#endif

#ifndef BOTTOM_HALF_TASK_NAME
#define BOTTOM_HALF_TASK_NAME "bottom_half_task"
#endif

/* -------------------------------------------------------------------------- */
/*                              下半部核心 (无锁队列 + run_pending) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 下半部工作项
 * @note  pending/executing/rerun 三原子位实现合并与补跑, 执行期间再触发不丢失
 */
struct bottom_half_work
{
    bottom_half_fn_t fn;        /**< 下半部处理函数 */
    void*            arg;       /**< 处理函数参数 */
    MINI_ATOMIC_BOOL pending;   /**< 已在队列或正在执行 */
    MINI_ATOMIC_BOOL executing; /**< run_pending 内 fn() 执行中 (仅消费者写) */
    MINI_ATOMIC_BOOL rerun;     /**< fn() 执行期间再次 trigger, run_pending 结束后补跑 */
};

#define BOTTOM_HALF_WORK_INIT(work_fn, work_arg)                                                                                                     \
    {.fn = (work_fn), .arg = (work_arg), .pending = MINI_ATOMIC_INIT(false), .executing = MINI_ATOMIC_INIT(false), .rerun = MINI_ATOMIC_INIT(false)}

MINI_STATIC_ASSERT((BOTTOM_HALF_QUEUE_DEPTH >= 2U) && ((BOTTOM_HALF_QUEUE_DEPTH & (BOTTOM_HALF_QUEUE_DEPTH - 1U)) == 0U),
                   "BOTTOM_HALF_QUEUE_DEPTH must be a power of two >= 2");

/**
 * @brief 判断当前是否在中断上下文
 */
MINI_STATIC_INLINE bool bottom_half_in_isr(void) { return osal_in_isr() != 0; }

/**
 * @brief 补跑入队 (内部使用)
 * @param[in] fifo  fifo_spsc 指针
 * @param[in] work  工作项指针
 * @return MINI_OK 成功; MINI_ERR_NOSPC 队列满
 */
MINI_STATIC_INLINE int bottom_half_submit_rerun(struct fifo_spsc* fifo, struct bottom_half_work* work)
{
    bool expected = false;

    if (!MINI_ATOMIC_CAS(&work->pending, &expected, true, MINI_ACQ_REL, MINI_RELAXED))
        return MINI_OK;

    if (fifo_write_data(fifo, (fifo_data_type)(uintptr_t)work) == BUFF_OK)
        return MINI_OK;

    MINI_ATOMIC_STORE(&work->pending, false, MINI_RELEASE);
    return MINI_ERR_NOSPC;
}

/**
 * @brief ISR 侧入队 (裸机 / OS 共用)
 * @param[in] fifo  fifo_spsc 指针
 * @param[in] work  工作项指针
 * @return MINI_OK 已入队 / 已在队列 / 已记 rerun; MINI_ERR_INVAL 入参非法; MINI_ERR_NOSPC
 * 队列满 (work 被丢弃)
 * @note   本函数不执行 work->fn(); 调用方 return-from-ISR 后由消费者 run_pending
 */
MINI_STATIC_INLINE int bottom_half_submit_from_isr(struct fifo_spsc* fifo, struct bottom_half_work* work)
{
    bool expected = false;

    if (!fifo || !work || !work->fn)
        return MINI_ERR_INVAL;

    if (!MINI_ATOMIC_CAS(&work->pending, &expected, true, MINI_ACQ_REL, MINI_RELAXED))
    {
        if (MINI_ATOMIC_LOAD(&work->executing, MINI_ACQUIRE))
            MINI_ATOMIC_STORE(&work->rerun, true, MINI_RELEASE);
        return MINI_OK;
    }

    if (fifo_write_data(fifo, (fifo_data_type)(uintptr_t)work) != BUFF_OK)
    {
        MINI_ATOMIC_STORE(&work->pending, false, MINI_RELEASE);
        return MINI_ERR_NOSPC;
    }
    return MINI_OK;
}

/**< 非 inline 实现, 在 interrupt.c 中定义 */
void bottom_half_run_pending(struct fifo_spsc* fifo);

/* -------------------------------------------------------------------------- */
/*                              下半部裸机轮询适配 */
/* -------------------------------------------------------------------------- */
/**
 * @brief 下半部轮询器结构 (裸机主循环用)
 * @note  在 fifo_spsc 上叠加 pending_drain 标志, ISR 置位、主循环轮询
 *        bottom_half_poller_run() 先清标志再 run_pending, 期间新 ISR 重新置位, 防丢唤醒
 */
struct bottom_half_poller
{
    struct fifo_spsc fifo;                          /**< 工作项 FIFO */
    fifo_data_type   ring[BOTTOM_HALF_QUEUE_DEPTH]; /**< FIFO 环形缓冲 */
    volatile bool    pending_drain;                 /**< ISR 写, 主循环读 */
};

/**
 * @brief 初始化裸机下半部轮询器
 * @param[in] poller 轮询器指针
 */
MINI_STATIC_INLINE void bottom_half_poller_init(struct bottom_half_poller* poller)
{
    if (!poller)
        return;
    MINI_IGNORE_RESULT(fifo_init(&poller->fifo, poller->ring, BOTTOM_HALF_QUEUE_DEPTH));
    poller->pending_drain = false;
}

/**
 * @brief ISR 调: 仅入队 + 置 pending_drain, 不执行 fn
 * @param[in] poller 轮询器指针
 * @param[in] work 工作项指针
 * @note   return-from-ISR 后, 主循环 bottom_half_poller_run() 才 run_pending
 * @return MINI_OK 成功入队; MINI_ERR_INVAL 入参非法; MINI_ERR_NOSPC 队列满
 */
MINI_STATIC_INLINE int bottom_half_poller_submit(struct bottom_half_poller* poller, struct bottom_half_work* work)
{
    if (!poller)
        return MINI_ERR_INVAL;

    int ret = bottom_half_submit_from_isr(&poller->fifo, work);
    if (ret == MINI_OK)
        poller->pending_drain = true;
    return ret;
}

/**< 非 inline 实现, 在 interrupt.c 中定义 */
void bottom_half_poller_run(struct bottom_half_poller* poller);

/* -------------------------------------------------------------------------- */
/*                              下半部 RTOS 任务适配 (可选) */
/* -------------------------------------------------------------------------- */
#ifndef CONFIG_OSAL_NULL
/**
 * @brief 下半部任务适配结构 (RTOS 用)
 * @note  在 fifo_spsc 上叠加二值信号量, ISR 入队后 post 唤醒专用下半部任务
 */
struct bottom_half_task
{
    struct fifo_spsc fifo;                          /**< 工作项 FIFO */
    fifo_data_type   ring[BOTTOM_HALF_QUEUE_DEPTH]; /**< FIFO 环形缓冲 */
    struct osal_sem* sem;                           /**< 二值信号量 (唤醒下半部任务) */
};

/**
 * @brief 初始化下半部任务适配器 (绑定二值信号量)
 * @param[in] task 任务适配器指针
 * @param[in] sem 二值信号量 (ISR 入队后用于唤醒)
 * @return MINI_OK 成功; MINI_ERR_INVAL 参数非法或 FIFO 初始化失败
 */
MINI_STATIC_INLINE int bottom_half_task_init(struct bottom_half_task* task, struct osal_sem* sem)
{
    if (!task || !sem)
        return MINI_ERR_INVAL;
    if (fifo_init(&task->fifo, task->ring, BOTTOM_HALF_QUEUE_DEPTH) != BUFF_OK)
        return MINI_ERR_INVAL;
    task->sem = sem;
    return MINI_OK;
}

/**
 * @brief ISR 调: 入队 + post 二值信号量
 * @param[in] task 任务适配器指针
 * @param[in] work 工作项指针
 * @param[out] px_yield_required ISR 出口是否需要 yield
 * @return MINI_OK 成功入队; MINI_ERR_INVAL 入参非法; MINI_ERR_NOSPC 队列满
 */
MINI_STATIC_INLINE int bottom_half_task_submit_from_isr(struct bottom_half_task* task, struct bottom_half_work* work, bool* px_yield_required)
{
    if (!task || !task->sem)
        return MINI_ERR_INVAL;

    int ret = bottom_half_submit_from_isr(&task->fifo, work);
    if (ret != MINI_OK)
        return ret;

    (void)osal_sem_post_from_isr(task->sem, px_yield_required);
    return MINI_OK;
}

/**
 * @brief 任务上下文: 入队 + post 二值信号量
 * @param[in] task 任务适配器指针
 * @param[in] work 工作项指针
 * @return MINI_OK 成功入队; MINI_ERR_INVAL 入参非法; MINI_ERR_ISR 在 ISR 中调用; MINI_ERR_NOSPC
 * 队列满
 */
MINI_STATIC_INLINE int bottom_half_task_submit(struct bottom_half_task* task, struct bottom_half_work* work)
{
    if (!task || !task->sem)
        return MINI_ERR_INVAL;
    if (bottom_half_in_isr())
        return MINI_ERR_ISR;

    int ret = bottom_half_submit_from_isr(&task->fifo, work);
    if (ret != MINI_OK)
        return ret;

    (void)osal_sem_post(task->sem);
    return MINI_OK;
}

/**
 * @brief 下半部专用任务入口
 * @param[in] arg 指向 struct bottom_half_task 的指针
 * @note  sem wait 返回时已脱离中断; 随后 run_pending 执行下半部
 */
MINI_STATIC_INLINE void bottom_half_task_entry(void* arg)
{
    struct bottom_half_task* task = (struct bottom_half_task*)arg;
    if (!task || !task->sem)
        return;

    for (;;)
    {
        if (osal_sem_wait(task->sem, OSAL_WAIT_FOREVER) != OSAL_OK)
            continue;
        bottom_half_run_pending(&task->fifo);
    }
}

/**
 * @brief 创建并启动下半部专用任务
 * @param[in] task 任务适配器指针
 * @param[in] name 任务名 (NULL 用默认)
 * @param[in] stack_size 栈大小 (0 用默认)
 * @param[in] priority 优先级
 * @return 成功返回 MINI_OK, 创建失败返回负数错误码
 */
MINI_STATIC_INLINE int bottom_half_task_start(struct bottom_half_task* task, const char* name, uint32_t stack_size, uint32_t priority)
{
    if (!task)
        return MINI_ERR_INVAL;
    return osal_task_create(name ? name : BOTTOM_HALF_TASK_NAME, stack_size ? stack_size : BOTTOM_HALF_TASK_STACK_SIZE, priority,
                            bottom_half_task_entry, task, 0);
}
#endif /* CONFIG_OSAL_NULL */

/* -------------------------------------------------------------------------- */
/*                              VIRQ + 下半部一体化 API */
/* -------------------------------------------------------------------------- */
/**
 * @brief 注册虚拟中断的上半部回调与下半部工作
 * @param[in] virq_num  VIRQ(block, idx) 计算的虚拟中断号
 * @param[in] top_half  上半部回调 (在 ISR 内执行, 必须轻量; NULL 表示无上半部)
 * @param[in] work      下半部工作 (NULL 表示无下半部; 非 NULL 时由 dispatch 自动 submit)
 * @param[in] arg       传递给上半部回调和下半部 work 的参数 (ISR 调 dispatch 时从表读取)
 * @note  top_half 返回非零表示需要 submit 下半部; 返回零表示不需要
 */
void interrupt_virtual_register(uint16_t virq_num, interrupt_top_half_t top_half, struct bottom_half_work* work, void* arg);

/**
 * @brief 虚拟中断调度入口 (ISR 内调用)
 * @param[in] virq_num 虚拟中断号
 * @note  内部自动衔接: 调完上半部后根据返回值决定是否 submit 下半部
 * @note  arg 由 register 时存入表, dispatch 从表读取 — ISR 不再传 arg
 */
void interrupt_virtual_dispatch(uint16_t virq_num);

/**
 * @brief 初始化全局下半部实例 (mini_pre_execution 自动调用)
 */
void interrupt_bottom_half_init(void);

/* -------------------------------------------------------------------------- */
/*                              硬件中断使能/关闭 (平台无关接口) */
/* -------------------------------------------------------------------------- */
/**
 * @brief 使能硬件中断 (NVIC / intr_alloc)
 * @param[in] irqn      硬件中断号 (来自 DTS)
 * @param[in] priority  抢占优先级 (0=最高, 数值越大优先级越低)
 * @note  STM32/CH32: NVIC_SetPriority + NVIC_EnableIRQ
 * @note  ESP32: esp_intr_alloc (priority 语义不同, 由实现适配)
 */
void interrupt_hw_enable(int irqn, uint32_t priority);

/**
 * @brief 关闭硬件中断
 * @param[in] irqn 硬件中断号
 */
void interrupt_hw_disable(int irqn);

/**
 * @brief ISR 入队接口 (一般由 dispatch 自动调用, 也可手动调用)
 * @param[in] work 工作项指针
 * @return MINI_OK 成功; MINI_ERR_INVAL 入参非法; MINI_ERR_NOSPC 队列满
 */
int interrupt_bottom_half_submit(struct bottom_half_work* work);

/**
 * @brief 主循环执行下半部队列
 * @note  必须在线程上下文调用, 禁止在 ISR 内调用
 */
void interrupt_bottom_half_poll();

/* -------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif
#endif /* __INTERRUPT_H__ */
