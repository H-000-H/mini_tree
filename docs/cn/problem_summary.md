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
| P2 | 裸机 C++ 任务在 `CONFIG_XTASK_PREEMPT` 下的栈归属 | 栈由谁分配不清，可能溢出 | 暂由平台在 `（C++ 封装已移除）` 手动指定；长期归 `system` 任务管理 |
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
| P9 | `ide/stubs/` 与 real 头不同步 | clangd 误报 | 重跑 CMake configure 再生 |

---

## 4. 已修复

| 编号 | 问题 | 根因 | 影响 | 修复 |
| :--- | :--- | :--- | :--- | :--- |
| P12 | 抢占式调度器 (xtask_preempt) TIM 路径不工作 | 见下方详细分析 | TIM7 中断不触发 → fall through 到 SysTick 兜底 → tick_count 恒为 0 → 任务不到期 → LED 不闪 | `xtask_preempt.c` 补 `interrupt_hw_enable()`；`interrupt_stm32.c` SysTick_Handler 桥接 `hal_systick_irq_handler()` |
| P13 | mini-os 启动即卡死在 `svc_handler` | `mini_os_psp_set` / `mini_os_set_control` 缺 `bx lr`，控制流顺序掉进紧随其后的 `svc_handler`；该函数用 `bl` 调 C 分发器却未保护 `EXC_RETURN`，`bx lr` 跳回自身 | 调度器启动前即永久死循环，串口无任何输出 | `port.S` 两个函数各补 `bx lr`；`svc_handler` 补 `push {r1, lr}` / `pop {r1, lr}` |
| P14 | mini-os 后端下串口日志全部丢失 | `mini_os_mutex_lock` 在无线程上下文时**无条件拒绝**（连空闲锁也不放行），而启动早期日志必须经 `device_lock` 输出 | `_write` 静默吞掉全部日志，控制台无输出 | `mini_backend_mini_os.c` 引导阶段（`mini_os_thread_current() == NULL`）放行 lock/unlock，与 FreeRTOS / 裸机后端语义对齐 |
| P15 | 任务 `delay` 到期后永不唤醒 | `mini_os_systick_handler` 的时间轮到期路径只把线程放回就绪队列，未触发 PendSV；Cortex-M 异常返回不会自动调度 | 819 次 tick 仅 5 次切换，任务停在 `delay` 之后不再运行（LED 只翻转一次） | `mini_os_systick_handler` 末尾补 `mini_os_schedule_yield_isr()`，与 `mini_os_schedule_delay` 挂起时的 yield 对称 |
| P16 | 首个 PendSV 的异常压栈越界 | `mini_os_schedule_start` 先把 `CONTROL.SPSEL` 切到 PSP、再把 PSP 设为"无线程"标记 `0`，开中断瞬间硬件用 PSP 压异常帧 | 帧落到 `0xFFFFFFE0~0xFFFFFFFC`（Vendor_SYS 区）；STM32F4 上被静默丢弃，部分芯片可能 BusFault | 启动期不再动 CONTROL（保持 MSP），由 `pendsv_handler` 在 `EXC_RETURN` 上声明返回 Thread+PSP，硬件自动恢复 SPSEL |
| P17 | 多任务争锁时在 `mini_os_mutex_propagate` 原地死循环 | 应用任务栈不足而溢出，踩坏同堆相邻的 TCB `hold_list`；PI 传播遍历该链表时反推出垃圾 mutex，其 `wait_list.next` 为 0 使内层循环条件恒真 | 11 万次无效地址读写后系统停滞 | 应用侧任务栈 512 → 2048 B（内核 TCB 与栈同堆相邻分配属既有设计，正确做法是调栈） |

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

### 分析：mini-os 后端接入链路（P13–P17）

五项**依次暴露**：前者修好，后者才显现。定位手段为 Renode 函数入口打点（`AddHook` 统计命中次数）配合反汇编比对，全部为可复现的确定性缺陷，非偶发。

| 阶段 | 现象 | 断点位置 | 判定依据 |
| :--- | :--- | :--- | :--- |
| 启动 | PC 恒为 `0x80202a2`，无输出 | `svc_handler` 内的 `bx lr` | 该地址前一条是 `bl`，LR 已被覆盖 |
| 启动 | 同上（P13 修复后） | `hal_uart_write` 命中 0 次 | `_write` 内 `console_dev_get()` 返回 NULL |
| 运行 | LED 只翻转一次 | `device_ioctl` 命中 1 次 | 819 次 tick 仅 5 次 `schedule_switch` |
| 运行 | 无效地址访问 11 万次 | `mini_os_mutex_propagate` 内层循环 | `[0+0x2C]` / `[0+0x0]` 反复读，节点恒为 0 |

**几个通用结论**：

- **端口汇编的返回指令必须逐个核对**：`mini_os_psp_set` / `mini_os_set_control` 这类"只写一个寄存器"的函数极易漏 `bx lr`，而漏掉后控制流会顺序流入下一个函数 —— 在按功能排序的汇编文件里后果不可预测；
- **异常入口一律自保 `EXC_RETURN`**：任何在 handler 里 `bl` 进 C 代码的路径，都必须先把 LR 保存起来再调用；
- **ISR 里唤醒线程后必须显式置 PendSV**：Cortex-M 的异常返回不参与调度，`mini_os_schedule_yield_isr()` 是这套约定的统一出口，新增唤醒路径时不能漏；
- **后端适配层的职责是抹平语义差**：同一个 `device_lock`，FreeRTOS 的空闲锁可直接获取、裸机自旋可取，而 mini-os 要求线程上下文 —— 这种差异应在 `mini_backend_*.c` 收口，不能漏到调用方；
- **线程栈要按调用链深度留量**：TCB 与栈同堆相邻分配，栈溢出会先踩到邻居的链表节点，表现为"毫不相关"的内核崩溃。

---

## 5. 已规避（不修）

| 编号 | 问题 | 处理 |
| :--- | :--- | :--- |
| P10 | 经典 Keil µVision 集成 | 本仓库不提供、不跟进，见 [keil_integration.md](keil_integration.md) |
| P11 | ARMCC v5 编译 | 不支持，仅 ARMCLANG (AC6) |

---

## 相关文档

- [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md)
