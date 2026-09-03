#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
/*
 * !!! WARNING — 平台模板值 !!!
 * configCPU_CLOCK_HZ 必须按实际目标 MCU 主频修改,
 * 否则 pdMS_TO_TICKS / portTICK_PERIOD_MS 等所有时间换算将完全错误.
 * 可通过 board_config.h 或在 CMake 层面用 -D 覆盖此值.
 */
#define configCPU_CLOCK_HZ                      168000000
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                128
/* 动态堆大小: Kconfig CONFIG_FREERTOS_HEAP_SIZE 优先, 否则 8 KB 默认 */
#ifdef CONFIG_FREERTOS_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                   CONFIG_FREERTOS_HEAP_SIZE
#else
#define configTOTAL_HEAP_SIZE                   8192
#endif
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_TRACE_FACILITY                0
#define INCLUDE_eTaskGetState                   1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_RECURSIVE_MUTEXES             1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_APPLICATION_TASK_TAG          1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         1

/*
 * ISR 优先级约束 — 所有 FreeRTOS FromISR API 仅能在
 * 优先级 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 的中断中调用.
 * 即: NVIC 优先级 0~4 的中断处理程序不得调用 xQueueSendFromISR 等.
 * 违反此约束会导致 FreeRTOS 内部断言失败 (BASEPRI 屏蔽).
 * 移植驱动时, 务必确认 ISR 优先级满足此要求.
 *
 * STM32F407 NVIC 实现 4 位优先级 (16 级), 硬件寄存器值需要左移 4 位:
 *   configMAX_SYSCALL_INTERRUPT_PRIORITY = configLIBRARY_... << 4
 *
 * Cortex-M0/M0+ (ARMv6-M) 无 BASEPRI, NVIC 仅 4 级优先级 (0~3),
 * 通过 __ARM_ARCH_6M__ 编译器宏自动切换.
 */
#if defined(__ARM_ARCH_6M__)
#define configENABLE_MPU                        0   /* M0/M0+ 无 MPU */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    3   /* M0/M0+ NVIC 仅 4 级 (0~3) */
#else
#define configENABLE_MPU                        0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5 << 4)  /* = 80, 屏蔽优先级 ≥ 5 的中断 */
#endif
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          15

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetTickCount               1
#define INCLUDE_xQueueGetMutexHolder            1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

/* ── RISC-V MTIME/CLINT (0 = 无硬件定时器, 由用户工程覆盖) ── */
#define configCLINT_BASE_ADDRESS              0

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0

/*
 * 软件定时器 (由 Kconfig CONFIG_FREERTOS_USE_TIMERS 控制)
 *
 * CONFIG_FREERTOS_USE_TIMERS 来自 kconfig 生成的 config.h, 而内核源文件
 * (tasks.c / timers.c / event_groups.c 等) 并不包含 config.h, 只看
 * #ifdef CONFIG_FREERTOS_USE_TIMERS 会让内核侧的 configUSE_TIMERS 恒为 0,
 * 与 osal 侧看到的值不一致 (timers.c 开头就有
 * "INCLUDE_xTimerPendFunctionCall 需要 configUSE_TIMERS" 的 #error)。
 * 因此 lib/freeRTOS/CMakeLists.txt 额外用 -D 注入
 * MINI_TREE_FREERTOS_USE_TIMERS 作为等效开关, 两个宏任一命中即视为开启。
 */
#if defined(CONFIG_FREERTOS_USE_TIMERS) || defined(MINI_TREE_FREERTOS_USE_TIMERS)
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               29  /* 低于 EventBus (30), 避免同级优先级抢占 */
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE
#else
#define configUSE_TIMERS                        0
#endif

/*
 * 事件组 (由 Kconfig CONFIG_FREERTOS_EVENT_GROUPS 控制, 默认关闭)
 *
 * 必须显式定义 0/1: FreeRTOS.h 在 configUSE_EVENT_GROUPS 未定义时会
 * 兜底成 1, 仅靠 "不写这一行" 无法真正关掉事件组。
 * CONFIG_FREERTOS_EVENT_GROUPS 来自 kconfig 生成的 config.h — 内核源文件
 * (tasks.c / event_groups.c 等) 并不包含 config.h, 因此
 * lib/freeRTOS/CMakeLists.txt 额外用 -D 注入 MINI_TREE_FREERTOS_EVENT_GROUPS
 * 作为等效开关, 两个宏任一命中即视为开启。
 * 关闭时 event_groups.c 也不会进入内核库的编译单元列表。
 * 注意: 事件组的 "等待并自动清位" 语义依赖软件定时器守护任务,
 * 因此 Kconfig 侧开启本项会 select FREERTOS_USE_TIMERS。
 */
#if defined(CONFIG_FREERTOS_EVENT_GROUPS) || defined(MINI_TREE_FREERTOS_EVENT_GROUPS)
#define configUSE_EVENT_GROUPS                  1
/*
 * xEventGroupSetBitsFromISR() 在 event_groups.c 里被
 * (INCLUDE_xTimerPendFunctionCall == 1 && configUSE_TIMERS == 1) 包住:
 * 它不直接改标志, 而是把 vEventGroupSetBitsCallback  pend 给软件
 * 定时器守护任务执行 (ISR 里不能阻塞, 而唤醒等待者需要拿事件组锁)。
 * 不开本宏时 osal_event_set_from_isr() 会因符号缺失编译失败,
 * 因此随事件组一起打开 (configUSE_TIMERS 已由 Kconfig select 保证)。
 */
#define INCLUDE_xTimerPendFunctionCall          1
#else
#define configUSE_EVENT_GROUPS                  0
#endif
#if defined(__ARM_ARCH_6M__)
/* M0/M0+ 无 CLZ 指令, 禁用优化任务选择 (tasks.c 内联汇编会编译失败) */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#else
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#endif

#define configASSERT(x) if (!(x)) { taskDISABLE_INTERRUPTS(); for (;;); }

/* CMSIS 向量名映射 — Cube 空 handler 需在 board irq handlers 中改名让位 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
/* SysTick 仍走 Cube SysTick_Handler, 内调 xPortSysTickHandler + HAL_IncTick */

#endif /* FREERTOS_CONFIG_H */
