/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_init.h
 *@brief system init 头文件
 *@author H-000-H

 */

#pragma once

/*
 * mini_tree 系统初始化接口.
 *
 *   mini_tree_pre_os_init();
 *   board_register_all_drivers();
 *   mini_tree_start_tasks();
 * #ifdef CONFIG_OSAL_NULL
 *   xscheduler_start();
 *   system_init_complete();
 *   while (1) { mini_tree_system_loop(); }   // 裸机
 * #elif defined(CONFIG_OSAL_FREERTOS)
 *   system_init_complete();
 *   vTaskStartScheduler();                   // FreeRTOS
 * #elif defined(CONFIG_OSAL_RTTHREAD)
 *   system_init_complete();
 *   rt_system_scheduler_start();             // RT-Thread
 * #endif
 *
 * 业务任务请走 osal_task_create / osal_task_create_handle.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 系统初始化前置阶段 (调度器启动前: 板级外设/驱动注册/静态分配)
 */
void mini_tree_pre_os_init(void);
/**
 * @brief 启动框架任务 (EventBus 分发、下半部任务等)
 */
void mini_tree_start_tasks(void);

/**
 * @brief 裸机 super-loop 入口 (OS 后端由内核调度, 通常不调用)
 */
void mini_tree_system_loop(void);

/**
 * @brief 系统初始化完成 — 释放全局中断 (启动调度器 / 进入 while 前调用)
 */
void system_init_complete(void);

#ifdef __cplusplus
}
#endif
