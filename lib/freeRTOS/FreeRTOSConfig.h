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

/* 软件定时器 (由 Kconfig CONFIG_FREERTOS_USE_TIMERS 控制) */
#ifdef CONFIG_FREERTOS_USE_TIMERS
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               29  /* 低于 EventBus (30), 避免同级优先级抢占 */
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE
#else
#define configUSE_TIMERS                        0
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
