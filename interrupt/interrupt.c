/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file interrupt.c
 *@brief 中断上下部系统实现 — VIRQ 虚拟中断号 + 下半部工作队列一体化
 *@author H-000-H
 *@details
 *   @note  interrupt_hw_* 为 weak 空实现，板级覆盖；厂商 ISR 不进中间件
 */

#include "interrupt.h"

#include "compiler_compat.h"
#include "status.h"

/*=======================================================================================================================================================*/
/*                              VIRQ 表 + 调度 */
/*=======================================================================================================================================================*/
interrupt_top_half_t interrupt_virtual_top_half[VIRTUAL_IRQ_MAX_BASE] = {0};
struct bottom_half_work* interrupt_virtual_bottom_half_work[VIRTUAL_IRQ_MAX_BASE] = {0};
void* interrupt_virtual_arg[VIRTUAL_IRQ_MAX_BASE] = {0};

/**< ADC DMA 下半部全局工作项 (fn/arg 由板级 HAL 绑定) */
struct bottom_half_work g_adc_dma_bottom_half_work;

/**< I2S DMA 下半部全局工作项 (fn/arg 由板级 HAL 绑定) */
struct bottom_half_work g_i2s_bottom_half_work;

void interrupt_virtual_register(uint16_t virq_num, interrupt_top_half_t top_half,
                                struct bottom_half_work* work, void* arg)
{
    if (virq_num >= VIRTUAL_IRQ_MAX_BASE)
        return;
    interrupt_virtual_top_half[virq_num] = top_half;
    interrupt_virtual_bottom_half_work[virq_num] = work;
    interrupt_virtual_arg[virq_num] = arg;
}

void interrupt_virtual_dispatch(uint16_t virq_num)
{
    if (virq_num >= VIRTUAL_IRQ_MAX_BASE)
        return;

    interrupt_top_half_t top = interrupt_virtual_top_half[virq_num];
    struct bottom_half_work* work = interrupt_virtual_bottom_half_work[virq_num];
    void* arg = interrupt_virtual_arg[virq_num];

    if (top)
    {
        int need_bottom_half = top(arg, virq_num);
        if (need_bottom_half && work)
            COMPAT_IGNORE_RESULT(interrupt_bottom_half_submit(work));
    }
    else if (work)
    {
        COMPAT_IGNORE_RESULT(interrupt_bottom_half_submit(work));
    }
}

/*=======================================================================================================================================================*/
/*                              硬件中断使能/关闭 (weak) */
/*=======================================================================================================================================================*/
COMPAT_WEAK void interrupt_hw_enable(int irqn, uint32_t priority)
{
    COMPAT_UNUSED_PARAM(irqn);
    COMPAT_UNUSED_PARAM(priority);
}

COMPAT_WEAK void interrupt_hw_disable(int irqn) { COMPAT_UNUSED_PARAM(irqn); }

/*=======================================================================================================================================================*/
/*                              VIRQ 外围弱钩子 (板级强符号覆盖) */
/*=======================================================================================================================================================*/
COMPAT_WEAK int hal_virtual_adc_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK int hal_virtual_i2s_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK int hal_virtual_spi_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK int hal_virtual_can_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK int hal_virtual_dac_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK int hal_virtual_tim_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK int hal_virtual_uart_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_UNUSED_PARAM(arg);
    COMPAT_UNUSED_PARAM(irq_num);
    return MINI_IRQ_ENTRY_NOBOTTOM;
}

COMPAT_WEAK void hal_adc_dma_bottom_half_handler(void* arg) { COMPAT_UNUSED_PARAM(arg); }

COMPAT_WEAK void hal_i2s_dma_bottom_half_handler(void* arg) { COMPAT_UNUSED_PARAM(arg); }

/*=======================================================================================================================================================*/
/*                              下半部核心非 inline 实现 */
/*=======================================================================================================================================================*/
void bottom_half_run_pending(struct fifo_spsc* fifo)
{
    if (!fifo || bottom_half_in_isr())
        return;

    fifo_data_type elem;
    while (fifo_read_data(fifo, &elem) == BUFF_OK)
    {
        struct bottom_half_work* work = (struct bottom_half_work*)(uintptr_t)elem;

        COMPAT_ATOMIC_STORE(&work->executing, true, COMPAT_MO_RELEASE);
        work->fn(work->arg);
        COMPAT_ATOMIC_STORE(&work->executing, false, COMPAT_MO_RELEASE);

        COMPAT_ATOMIC_STORE(&work->pending, false, COMPAT_MO_RELEASE);

        while (COMPAT_ATOMIC_EXCHANGE(&work->rerun, false, COMPAT_MO_ACQ_REL))
        {
            if (bottom_half_submit_rerun(fifo, work) == MINI_OK)
                break;
            COMPAT_ATOMIC_STORE(&work->executing, true, COMPAT_MO_RELEASE);
            work->fn(work->arg);
            COMPAT_ATOMIC_STORE(&work->executing, false, COMPAT_MO_RELEASE);
        }
    }
}

void bottom_half_poller_run(struct bottom_half_poller* poller)
{
    if (!poller || !poller->pending_drain || bottom_half_in_isr())
        return;

    poller->pending_drain = false;
    bottom_half_run_pending(&poller->fifo);
}

/*=======================================================================================================================================================*/
/*                              全局下半部实例 */
/*=======================================================================================================================================================*/
static struct bottom_half_poller s_global_poller;

/** 全局下半部轮询器初始化 (pre_execution 阶段) */
pre_execution(PRE_EXEC_PRIO_IRQ_BOTTOM) static void interrupt_bottom_half_pool_init(void)
{
    bottom_half_poller_init(&s_global_poller);
}

void interrupt_bottom_half_init(void) { bottom_half_poller_init(&s_global_poller); }

int interrupt_bottom_half_submit(struct bottom_half_work* work)
{
    return bottom_half_poller_submit(&s_global_poller, work);
}

void interrupt_bottom_half_poll(void) { bottom_half_poller_run(&s_global_poller); }
