#pragma once

namespace MiniTree {

/* ── 两段式点火接口 ──
 *
 * 用户工程在 main() 中按此顺序调用:
 *
 *   int main(void) {
 *       MiniTree::System_Pre_OS_Init();      // [1] 框架预初始化 (EventBus, 安全, RTC WDT, 关全局中断)
 *       platform_register_all_drivers();     // 向 VFS 注册平台驱动
 *       MyApp::init_services();              // 用户业务服务 init()
 *
 *       MiniTree::System_Start_Tasks();      // [2] driver probe, TWDT, scrubber
 *       MyApp::create_tasks();               // 用户业务任务创建
 *
 *       system_init_complete();              // 释放全局中断 (ISR 可开始响应)
 *       vTaskStartScheduler();               // [3] 启动 RTOS 调度器
 *       while(1);
 *   }
 *
 * 框架只提供基础设施 (事件总线、看门狗、设备树、位腐烂巡检),
 * 不包含任何业务 Service 或任务创建.
 */

/* 阶段 1: 预操作系统初始化。
 * 在平台 HAL 初始化之后、vTaskStartScheduler() 之前调用。
 * 完成: 启动循环检查、RTC 看门狗、设备树初始化、EventBus 初始化。
 */
void System_Pre_OS_Init(void);

/* 阶段 2: 启动框架任务。
 * 在用户驱动注册之后、vTaskStartScheduler() 之前调用。
 * 完成: 驱动探测、TWDT 初始化、巡检启动、启动循环清除。
 * 用户在此调用之后创建自身的业务任务。
 */
void System_Start_Tasks(void);

}  // namespace MiniTree

/* ── 初始化完成 — 释放全局中断 ──
 * 在 vTaskStartScheduler() 之前调用.
 * 如果忘记调用不会造成灾难: FreeRTOS 在首次上下文切换时也会自动使能中断.
 */
extern "C" void system_init_complete(void);
