/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/*
 * mini_tree C 版本系统初始化接口.
 *
 * 用户工程在 main() 中按两段式点火调用:
 *
 *   int main(void) {
 *       HAL_Init();
 *       SystemClock_Config();            // 仅保留 Cube 时钟
 *       mini_tree_pre_os_init();         // Phase 1
 *       board_register_all_drivers();
 *       mini_tree_start_tasks();         // Phase 2: DTS probe → hal_if
 *       system_init_complete();
 *   #ifdef CONFIG_OSAL_NULL
 *       while (1) { mini_tree_system_loop(); }
 *   #else
 *       vTaskStartScheduler();
 *   #endif
 *   }
 */

#ifdef __cplusplus
extern "C" 
{
#endif

void mini_tree_pre_os_init(void);
void mini_tree_start_tasks(void);
void mini_tree_system_loop(void);

/* ── 初始化完成 — 释放全局中断 ──
 * 在 vTaskStartScheduler() 之前调用.
 * 如果忘记调用不会造成灾难: FreeRTOS 在首次上下文切换时也会自动使能中断.
 */
void system_init_complete(void);

#ifdef __cplusplus
}
#endif


