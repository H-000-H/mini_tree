#pragma once

/*
 * mini_tree C 版本系统初始化接口.
 *
 * 用户工程在 main() 中按两段式点火调用:
 *
 *   int main(void) {
 *       platform_hardware_init();
 *       mini_tree_pre_os_init();     // Phase 1
 *       platform_register_drivers();
 *       mini_tree_start_tasks();     // Phase 2
 *   #ifdef CONFIG_OSAL_NULL
 *       while (1) { mini_tree_system_loop(); }   // 裸机轮询
 *   #else
 *       vTaskStartScheduler();
 *   #endif
 *   }
 */

void mini_tree_pre_os_init(void);
void mini_tree_start_tasks(void);
void mini_tree_system_loop(void);

/* ── 初始化完成 — 释放全局中断 ──
 * 在 vTaskStartScheduler() 之前调用.
 * 如果忘记调用不会造成灾难: FreeRTOS 在首次上下文切换时也会自动使能中断.
 */
void system_init_complete(void);
