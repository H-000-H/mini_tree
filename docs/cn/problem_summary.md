# 已知问题汇总

> 当前中间件未解决 / 已规避的设计与实现问题。修复进度见 [todolist.md](todolist.md)、规划见 [roadmap.md](roadmap.md)。严重度：高 / 中 / 低。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 评估风险 / 接手修复 |
| **相关** | [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md) |

---

## 1. 高严重度

| 编号 | 问题 | 影响 | 规避 / 状态 |
| :--- | :--- | :--- | :--- |
| P1 | `err_section` 在部分链接脚本未单独放置 | 错误符号表与代码同段，诊断困难 | `CONFIG_ERR_SECTION=1` 仅在有独立 ROM 区时有效；见 [memory_footprint.md](memory_footprint.md) §1 |
| P2 | 裸机 C++ 任务在 `CONFIG_OSAL_NULL_TASK_CPP` 下的栈归属 | 栈由谁分配不清，可能溢出 | 暂由平台在 `osal_task.cpp` 手动指定；长期归 `system` 任务管理 |
| P3 | dtc-lite 对嵌套 dtsi 的 `include` 解析顺序敏感 | 板级覆盖易错 | 平台固定 `BOARD_DTSI_DIR` 单一来源 |

---

## 2. 中严重度

| 编号 | 问题 | 影响 | 规避 / 状态 |
| :--- | :--- | :--- | :--- |
| P4 | VFS 层驱动池重置后需显式再绑定 | 漏绑导致 `probe` 不触发 | 见 [driver_guide.md](driver_guide.md) §7 |
| P5 | `hal/amp` 多核消息在单核板空转 | 浪费少量周期 | 单核默认不编 `hal/amp` |
| P6 | `LOG_*` 宏在关日志后仍占符号 | 体积略增 | `CONFIG_SYS_LOG_LEVEL=0` 时由编译器去死代码，多数可消 |

---

## 3. 低严重度

| 编号 | 问题 | 影响 | 规避 / 状态 |
| :--- | :--- | :--- | :--- |
| P7 | `.clang-tidy` 分层在 app 层仅建议 | 命名规范可能被忽略 | 靠 review 兜底 |
| P8 | `tools/build_size.py` 的 `--format=baseline` 无历史基线时报警 | 首次运行无对照 | 先跑一次生成基线 |
| P9 | `ide/stubs/` 与 real 头不同步 | clangd 误报 | 重跑 CMake configure 再生 |

---

## 4. 已修复

| 编号 | 问题 | 根因 | 影响 | 修复 |
| :--- | :--- | :--- | :--- | :--- |
| P12 | 抢占式调度器 (xtask_preempt) TIM 路径不工作 | 见下方详细分析 | TIM7 中断不触发 → fall through 到 SysTick 兜底 → tick_count 恒为 0 → 任务不到期 → LED 不闪 | `xtask_preempt.c` 补 `interrupt_hw_enable()`；`interrupt_stm32.c` SysTick_Handler 桥接 `hal_systick_irq_handler()` |

### P12 详细分析

**因果链**：原因 1 → TIM7 中断不触发 → fall through 到 SysTick 兜底 → 原因 2 → tick_count 恒为 0 → 任务不到期 → LED 不闪

| # | 根因 | 位置 | 协调式为什么不受影响 |
| :---: | :--- | :--- | :--- |
| 1 | TIM 分支缺 `interrupt_hw_enable()` — VIRQ 注册了但 NVIC 未使能，硬件中断永远不触发 | `xtask_preempt.c` `xscheduler_start()` | 协调式 `xtask_coop.c` 已有此 3 行，NVIC 正常使能 |
| 2 | 兜底 SysTick 路径的 `SysTick_Handler`（板级强符号）只调 `HAL_IncTick()`，没链 `hal_systick_irq_handler()` 调度器钩子 | `interrupt_stm32.c` | 协调式走 TIM7 → `TIM7_IRQHandler` → VIRQ 分发路径，不依赖 SysTick 钩子 |

**修复内容**：

1. `mini_tree/time_slice/task/xtask_preempt.c` — `xscheduler_start()` TIM 分支补 NVIC 使能：
   ```c
   int irqn = -1;
   int priority = 5;
   device_get_prop_int(tick_dev, "irqn", &irqn);
   device_get_prop_int(tick_dev, "nvic-priority", &priority);
   interrupt_hw_enable(irqn, (uint32_t)priority);
   ```
2. `hal/system/interrupt_stm32.c` — `SysTick_Handler` 桥接调度器钩子：
   ```c
   void SysTick_Handler(void)
   {
       HAL_IncTick();
       hal_systick_irq_handler();  /* 链到调度器 */
   }
   ```

---

## 5. 已规避（不修）

| 编号 | 问题 | 处理 |
| :--- | :--- | :--- |
| P10 | 经典 Keil µVision 集成 | 本仓库不提供、不跟进，见 [keil_integration.md](keil_integration.md) |
| P11 | ARMCC v5 编译 | 不支持，仅 ARMCLANG (AC6) |

---

## 相关文档

- [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md)
