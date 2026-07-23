/* SPDX-License-Identifier: Apache-2.0 */
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

void mini_tree_pre_os_init(void);
void mini_tree_start_tasks(void);

/* 裸机 super-loop 入口; OS 后端由内核调度, 通常不调用 */
void mini_tree_system_loop(void);

/* 初始化完成 — 释放全局中断 (启动调度器 / 进入 while 前调用) */
void system_init_complete(void);

#ifdef __cplusplus
}
#endif
