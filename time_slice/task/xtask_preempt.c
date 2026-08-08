/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file xtask_preempt.c
 * @brief N+1 链表多时间片抢占调度器 (preemptive, 实验性, CONFIG_XTASK_PREEMPT)
 * @note  与协调式 xtask_coop.c 共用 xtask.h, 二选一互斥:
 *          - CONFIG_XTASK_PREEMPT 未定义 (默认): 本文件整段关闭, 用 xtask_coop.c
 *          - CONFIG_XTASK_PREEMPT 已定义:       本文件编入 (抢占式), xtask_coop.c 同步关闭
 *        CMake 也会同步门控, 这里是源码层面的第二道保险.
 *        尚未完工: g_scheduler_arr / s_preempt_tim_arr 当前无任何引用.
 */
#ifdef CONFIG_OSAL_NULL
#ifdef CONFIG_XTASK_PREEMPT

#include "xtask.h"

#include "board_devtable.h"
#include "compiler_compat.h"
#include "device.h"
#include "dt_config_gen.h"
#include "interrupt.h"
#include "vfs-tim.h"
/* 抢占式调度器专用配置 — 与协调式 xtask_coop.c 解耦, 不污染 xtask.h */
#ifndef HW_TIM_SCHEDULER
#define HW_TIM_SCHEDULER 1
#endif
#ifndef SOFT_TIME_PRIO_HIGHEST
#define SOFT_TIME_PRIO_HIGHEST 0
#endif

/*=======================================================================================================================================================*/
/*                              以下为N+1链表多时间片抢占调度器实现(可延迟可休眠可抢占帮助使用者在裸机更接近rtos的体验kconfig默认不开启使用者自行开启) */
/*=======================================================================================================================================================*/
/*如果要1个定时器 就设一个多余字段16？/32 不是问题实在要压就kconfig压 就默认32 优先级问题是如果定时器都到了呢怎么办 1.高抢占
2.次优先级?这种东西不太对这东西只能排队不行 3.clz或者ctz一样的这是目前为止最好的方法让我再想想感觉还有好方法 4.虚拟中断加clz？对啊我有虚拟中断我的中断优先级
本来就比一般的单片机项目多我去看看我有几个我自己都记不得了ok默认8个虚拟中断无所谓反正kconfig控制但是如果没有固定值可以吗。感觉可以没问题uc0s3就是这种思想
只不过支持没有我原生那就是最小2*8 最大2^n * 32/极限可以*64  64开玩笑呢过分了单片机哪里来这么多优先级linux都不需要 所以这个路是对的 我看看啊svc和pensv我看看能不能用
svc不行会打断调度顺序但是不对如果这是一个好处呢这方面留意下 那就看一下pensv这是标准rtos的方法标准的答案吗有意思 然后再去o(1)数组&匹配链表结点可以 然后二次侵入式 休息侵入运行
直接接入运行 然后接着睡大觉 如果调度器不止一个呢 上面优先级解决好这个问题了那就优先级控制进入统一运行链表 休息可慢入但是这操作在上半部还是下半部呢 我算一下啊 进下半部可以没问题但是有必要再吃
一个函数调用吗为了几乎0(1)和&匹配 那群人中断malloc都可以跑我为什么不对不对我是中间件不能这样想但是进入休息链表确实不需要进下半部 然后下半部让他慢慢跑任务但是延迟怎么办呢
osal—delay依然走不了啊协程？osal-delay假协程return给一个最垃圾的优先级给他对啊这样不就行了 还好我基础设施多ok就这样开写最后加一个wfi就行了啊这个我要藏入kconfig就让你们高功耗不好好读我的kconfig你们就降不下功耗吧我想一想
svc不可靠还是pensv，其实挺好的那就这样定了硬件tim就管第x*y的x y就由kconfig控制是8还是16还是32位不是这有必要吗感觉没必要啊我想想确实没必要那就是看
到底是几位中断了16位？只有1个tim时不够32倒是够了亦或者64？我看看啊freertos和zephyr和rt-tread其实没多少实在不行我拿响应再去凑翻倍就行了ok大体好了就看实现了
我想一个问题啊如果两个tim时钟不一样触发频率不一样我链表思路有没有问题想想啊其实没有多大问题运行链表就一个休眠链表还是自己管还是侵入式没什么问题
但是移植这块如果这个移植很困难那做到意义不大了移植其实我想想啊虚拟中断加几个函数的事定时器加一个dtsi的事额那问题不大可以但是假协程这边有点麻烦啊之前就是
因为这破假协程出问题没有走这版我真的不想用宏函数包装宏？我看看啊也行用gcc typdef去函数强转就行了但是协程最大问题就是开始那东西假设我自带状态机呢自带的话其实意义不大
对了goto 啊或者不行不行那个东西跳转消耗太大而且我也不跨文件跳转goto够用了*/

/* TODO: N+1 抢占式调度器尚未完工:
 *   - 原实现用运行期全局变量 g_scheduler 做数组初始化器, 非编译期常量 (C 标准禁止), 已改为 {0};
 *   - g_scheduler_arr / s_preempt_tim_arr 当前无任何引用, 待补全实现。
 *   - 与 xtask_coop.c 由 CONFIG_XTASK_PREEMPT 互斥门控, 故早初始化函数可同名 xscheduler_early_init
 *     (两者优先级 160/161 不同, 但互斥编译不会同时注册).
 */
struct scheduler_tim_param
{
    struct scheduler_tim_ctx ctx;
    int                      tim_delay;
};

struct x_preempt_task
{
    x_task* task;
    int priority;
};

x_scheduler g_scheduler_arr[HW_TIM_SCHEDULER] = {0};
static device_id_t s_schedule_chosen_arr[HW_TIM_SCHEDULER] = {CHOSEN_SCHEDULER_TIM};
static struct scheduler_tim_param s_preempt_tim_arr[HW_TIM_SCHEDULER]={0};
static struct x_preempt_task s_preempt_task[HW_TIM_SCHEDULER]={0};

pre_execution(PRE_EXEC_PRIO_SCHEDULER) static void xscheduler_early_init(void)
{
    for (uint8_t i = 0; i < HW_TIM_SCHEDULER; i++)
    {
        x_scheduler_init(&g_scheduler_arr[i]);
    }
}

static bool is_need_preempt(struct x_preempt_task task,uint32_t schedule_id)
{
    return task.priority>s_preempt_task[schedule_id].priority;
}

void xscheduler_start(void)
{
    for (uint8_t i = 0; i < HW_TIM_SCHEDULER; i++)
    {
        struct device* tick_dev = board_dev_get(s_schedule_chosen_arr[i]);
        if (!tick_dev)
            continue;
        if(device_open(tick_dev,NULL)!=VFS_OK)
            return;

        hal_tim_device *ptim = vfs_tim_get_hal_dev(tick_dev);

        s_preempt_tim_arr[i].ctx.tim=ptim;
        s_preempt_tim_arr[i].ctx.scheduler=&g_scheduler_arr[i];
    #ifdef CONFIG_VIRQ
        int tick_delay = -1;
        struct scheduler_tim_param param;
        param.tim_delay=tick_delay;
        param.ctx=s_preempt_tim_arr[i].ctx;
        COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev,"tick_delay",&tick_delay));
        
        interrupt_virtual_register(VIRQ(tim, i),scheduler_tim_isr_top,NULL,&param);
        int irqn =-1;
        int priority = 5;
        COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev,"irqn",&irqn));
        COMPAT_IGNORE_RESULT(device_get_prop_int(tick_dev, "nvic-priority", &priority));
        interrupt_hw_enable(irqn, (uint32_t)priority);
    #endif
    }
}

x_task_handle_t x_scheduler_task_create(x_scheduler* sched,const char* name, uint32_t period_ms, uint32_t priority, void (*cb)(x_task*), void* param,struct x_preempt_task* task)
{
    if(!cb||sched||task)
        return VFS_ERR_INVAL;
    task ->task->name =name;
    task->task->xTask_cb =cb;
    COMPAT_ATOMIC_STORE(&task->task->period,period_ms,COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->task->next_running,COMPAT_ATOMIC_LOAD(&sched->tick_count, COMPAT_MO_RELAXED)+period_ms,COMPAT_MO_RELAXED);
    COMPAT_ATOMIC_STORE(&task->task->is_running, false, COMPAT_MO_RELAXED);
    /*这里要对list动手脚*/
    return (x_task_handle_t)(uintptr_t)task;
}

int scheduler_tim_isr_top(void *arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);

    struct scheduler_tim_param* param = (struct scheduler_tim_param*)arg;
    if(param&&hal_tim_clear_update_flag(param->ctx.tim)==VFS_OK)
        x_scheduler_tick(param->ctx.scheduler,param->tim_delay);
    return VFS_OK;
}

int x_task_run_preempt(x_scheduler* sched[HW_TIM_SCHEDULER])
{
    if((!(*sched))||(!sched))
        return VFS_ERR_INVAL;

    for(uint8_t i;i<HW_TIM_SCHEDULER;i++)
    {
        list_node* head = &sched[i]->task_list_head;
        list_node*curent = head->next;
    }
    return VFS_OK;
}

int x_scheduler_tick(x_scheduler* sched, unsigned int ms)
{
    if (!sched)
        return VFS_ERR_INVAL;
    COMPAT_ATOMIC_ADD_FETCH(&sched->tick_count, ms, COMPAT_MO_RELAXED);
    return VFS_OK;
}
#endif /* CONFIG_XTASK_PREEMPT */
#endif /* CONFIG_OSAL_NULL */
