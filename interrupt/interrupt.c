/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file interrupt.c
 * @brief 中断上下部系统实现 — VIRQ 虚拟中断号 + 下半部工作队列一体化
 * @note  上半部 (ISR): interrupt_virtual_dispatch 内调 top_half, 根据返回值自动 submit 下半部
 * @note  下半部 (主循环): interrupt_bottom_half_poll() → bottom_half_run_pending() 执行回调
 */
#include "interrupt.h"
#include "compiler_compat.h"

/*=======================================================================================================================================================*/
/*                              VIRQ 表 + 调度                                                                                                            */
/*=======================================================================================================================================================*/
interrupt_top_half_t      interrupt_virtual_top_half[VIRTUAL_IRQ_MAX_BASE] = {0};
struct bottom_half_work*  interrupt_virtual_bottom_half_work[VIRTUAL_IRQ_MAX_BASE]  = {0};
void*                     interrupt_virtual_arg[VIRTUAL_IRQ_MAX_BASE]               = {0};

/**< ADC DMA 下半部全局工作项 (fn/arg 由 hal_adc.c 在 hal_adc_dma_it_start 中绑定) */
struct bottom_half_work g_adc_dma_bottom_half_work;

/**
 * @brief 注册虚拟中断的上半部回调与下半部工作
 * @param virq_num  VIRQ(block, idx) 计算的虚拟中断号
 * @param top_half  上半部回调 (NULL 表示无上半部)
 * @param work      下半部工作 (NULL 表示无下半部)
 * @param arg       传递给上半部回调和下半部 work 的参数 (存入表, dispatch 时读取)
 */
void interrupt_virtual_register(uint16_t virq_num, interrupt_top_half_t top_half, struct bottom_half_work* work, void* arg)
{
    if (virq_num >= VIRTUAL_IRQ_MAX_BASE)
        return;
    interrupt_virtual_top_half[virq_num] = top_half;
    interrupt_virtual_bottom_half_work[virq_num] = work;
    interrupt_virtual_arg[virq_num] = arg;
}

/**
 * @brief 虚拟中断调度入口 (ISR 内调用)
 * @param virq_num 虚拟中断号
 * @note  arg 从 VIRQ 表读取 (register 时存入), ISR 不再传 arg
 */
void interrupt_virtual_dispatch(uint16_t virq_num)
{
    if (virq_num >= VIRTUAL_IRQ_MAX_BASE)
        return;

    interrupt_top_half_t     top  = interrupt_virtual_top_half[virq_num];
    struct bottom_half_work* work = interrupt_virtual_bottom_half_work[virq_num];
    void*                    arg  = interrupt_virtual_arg[virq_num];

    if (top)
    {
        int need_bottom_half = top(arg, virq_num);
        if (need_bottom_half && work)
            (void)interrupt_bottom_half_submit(work);
    }
    else if (work)
    {
        (void)interrupt_bottom_half_submit(work);
    }
}

/*=======================================================================================================================================================*/
/*                              硬件中断使能/关闭                                                                                                          */
/*=======================================================================================================================================================*/
void interrupt_hw_enable(int irqn, uint32_t priority)
{
    if (irqn < 0)
        return;
    NVIC_SetPriority((IRQn_Type)irqn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), priority, 0));
    NVIC_EnableIRQ((IRQn_Type)irqn);
}

void interrupt_hw_disable(int irqn)
{
    if (irqn < 0)
        return;
    NVIC_DisableIRQ((IRQn_Type)irqn);
}

/*=======================================================================================================================================================*/
/*                              下半部核心非 inline 实现                                                                                                  */
/*=======================================================================================================================================================*/
/**
 * @brief 消费者执行所有待处理工作 — 禁止在 ISR 内调用
 * @note  fifo_read_data 取出 work 指针后立即推进 r_ptr, 槽位在 fn() 执行期间已释放
 * @note  fn() 执行期间再次 trigger 通过 rerun 补跑, 不静默丢失
 */
void bottom_half_run_pending(struct fifo_spsc* fifo)
{
    if (!fifo || bottom_half_in_isr())
        return;

    Fifo_Data_type elem;
    while (fifo_read_data(fifo, &elem))
    {
        struct bottom_half_work* work = (struct bottom_half_work*)(uintptr_t)elem;

        COMPAT_ATOMIC_STORE(&work->executing, true, COMPAT_MO_RELEASE);
        work->fn(work->arg);
        COMPAT_ATOMIC_STORE(&work->executing, false, COMPAT_MO_RELEASE);

        COMPAT_ATOMIC_STORE(&work->pending, false, COMPAT_MO_RELEASE);

        while (COMPAT_ATOMIC_EXCHANGE(&work->rerun, false, COMPAT_MO_ACQ_REL))
        {
            if (bottom_half_submit_rerun(fifo, work))
                break;
            COMPAT_ATOMIC_STORE(&work->executing, true, COMPAT_MO_RELEASE);
            work->fn(work->arg);
            COMPAT_ATOMIC_STORE(&work->executing, false, COMPAT_MO_RELEASE);
        }
    }
}

/**
 * @brief 主循环轮询 (线程上下文, 禁止在 ISR 内调用)
 * @note  先清 pending_drain 再 run_pending: 期间新 ISR 会重新置位, 防丢唤醒
 */
void bottom_half_poller_run(struct bottom_half_poller* poller)
{
    if (!poller || !poller->pending_drain || bottom_half_in_isr())
        return;

    poller->pending_drain = false;
    bottom_half_run_pending(&poller->fifo);
}

/*=======================================================================================================================================================*/
/*                              全局下半部实例                                                                                                            */
/*=======================================================================================================================================================*/
static struct bottom_half_poller s_global_poller;

/**
 * @brief 下半部池启动初始化 (pre_execution 自动调用)
 */
pre_execution(170)
static void interrupt_bottom_half_pool_init(void)
{
    bottom_half_poller_init(&s_global_poller);
}

/**
 * @brief 初始化全局下半部实例 (兜底, 防 pre_execution 未跑)
 */
void interrupt_bottom_half_init(void)
{
    bottom_half_poller_init(&s_global_poller);
}

/**
 * @brief ISR 入队接口 (一般由 dispatch 自动调用, 也可手动调用)
 * @param work 工作项指针
 * @return true 成功; false 队列满
 */
bool interrupt_bottom_half_submit(struct bottom_half_work* work)
{
    return bottom_half_poller_submit(&s_global_poller, work);
}

/**
 * @brief 主循环执行下半部队列
 * @note  必须在线程上下文调用, 禁止在 ISR 内调用
 */
void interrupt_bottom_half_poll(void)
{
    bottom_half_poller_run(&s_global_poller);
}
