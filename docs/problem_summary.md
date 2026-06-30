## 13. 问题总结：RT-Thread 路径"烧录后常量，复位后闪几下灭"

> 本问题在 ARM Cortex-M4F 平台（使用 `-mfloat-abi=hard -mfpu=fpv4-sp-d16` 工具链，如 STM32F407 等 Cortex-M4F 系列 MCU）上发现并修复。属 Cortex-M4F 通用问题，非特定厂商。本章为平台特定经验记录，与中间件层架构无关。

### 根因

**FPU 未在 Reset_Handler 中使能。** 工具链使用 `-mfloat-abi=hard -mfpu=fpv4-sp-d16`，编译产物中到处是 VFP 指令，但 `SCB->CPACR` 保持复位默认值 0x00000000（FPU 禁止访问）。

### 现象解释

| 场景 | 行为 | 原因 |
|---|---|---|
| 刚烧录 | LED 常量 | 调试器连接时替你配了 CPACR，FPU 可用。但复位行为本身可能还有其他边际问题未暴露 |
| 复位 | 闪几下 → 灭 | 调试器不再干预，CPACR=0。blink 任务跑了几个周期后，某次上下文切换或 C 运行时库（nano.specs 的硬件浮点优化版 `memcpy`/`memset`）执行 VFP 指令 → NOCP UsageFault → 升级为 HardFault → 死灯 |

### 修复（两处）

1. **startup.c** — `Reset_Handler` 在 SP 设置后、data/bss 初始化前，写入 `CPACR |= 0x00F00000` 开放 CP10/CP11 全权限
2. **main.c** — `system_clock_init` 规范化：先关 PLL → 再改参数 → 再开 PLL → 切 SYSCLK → 最后设总线分频（符合芯片参考手册 PLL 切换时序要求）

### 为什么 FreeRTOS / 裸机能过

- **FreeRTOS**: `xPortStartScheduler()` 内部调用 `vPortEnableVFP()`，写入 `CPACR |= 0x00F00000` 使能 FPU，因此 FreeRTOS 路径启动后 FPU 可用
- **裸机**: 当前测试代码中 `blink_task` 的纯 GPIO 翻转路径未触发编译器生成 VFP 指令，碰巧没暴露。但不代表安全——同一个工程里只要存在任何 VFP 指令，CPACR 不配就是定时炸弹，哪次代码改动触发了就会随机 HardFault。
