# 问题总结 — 通用排查轴 / Problem Summary — A Generic Troubleshooting Axis

> 将历史「上电正常 / 复位异常、烧录后常量区怪异」等经验，收敛为**与 SoC 无关**的检查清单。
> Converges historical experience ("powers up fine / misbehaves on reset, weird constant section after flashing") into a **SoC-agnostic** checklist.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 已按 FAQ 仍搞不定的人 / those still stuck after the FAQ |
| **相关 / Related** | [faq.md](faq.md) · [debug_monitor.md](debug_monitor.md) · [porting_guide.md](porting_guide.md) |

---

## 目录 / Contents

1. [现象分类](#1-现象分类) / Symptom categories
2. [中间件侧检查](#2-中间件侧检查) / Middleware-side checks
3. [平台侧检查](#3-平台侧检查) / Platform-side checks
4. [OSAL / 启动](#4-osal--启动) / OSAL / startup
5. [记录模板](#5-记录模板) / Report template

---

## 1. 现象分类 / Symptom Categories

| 现象 / Symptom | 优先怀疑 / Suspect first |
| :--- | :--- |
| 首次上电 OK，复位后挂 / OK at first power-up, hangs after reset | 外设时钟门控、WDT、全局/静态未再初始化 / peripheral clock gating, WDT, globals/statics not re-initialized |
| 常量/只读数据异常 / corrupted constants / read-only data | 链接脚本、Cache（多核）、错误段属性 / linker script, cache (multicore), wrong section attributes |
| Probe 大批失败 / mass probe failures | DTS status、宏未展开、compatible 不匹配 / DTS status, unexpanded macros, incompatible match |
| 单独外设一直 NOTSUPP / one peripheral always NOTSUPP | HAL 仍为 weak / HAL is still weak |
| 开优化后坏、Debug 正常 / broken with -O, fine in Debug | UB、缺 `volatile`、错误 ISR 属性 / UB, missing `volatile`, wrong ISR attributes |

---

## 2. 中间件侧检查 / Middleware-Side Checks

1. `config.h` 与预期 OSAL/SYSTEM 一致。 / `config.h` matches the expected OSAL/SYSTEM.
2. 生成 `board_nodes.h` 的 `DEV_ID_COUNT`、chosen 是否合理。 / Check `DEV_ID_COUNT` and chosen in the generated `board_nodes.h`.
3. `board_driver_probe_all` 日志：谁失败、criticality 是什么。 / `board_driver_probe_all` logs: who failed and what criticality.
4. 是否误在业务路径触发 `compiler_compat_poison`。 / Is `compiler_compat_poison` wrongly triggered on a business path?
5. `system_init_complete` 是否在开调度前调用。 / Is `system_init_complete` called before the scheduler starts?

---

## 3. 平台侧检查 / Platform-Side Checks

1. HAL 强符号：`nm` 看定义文件。 / HAL strong symbols: use `nm` to see which file defines them.
2. `VENDOR_INC_DIRS` 是否让 dtsi 宏变成非零。 / Does `VENDOR_INC_DIRS` make the dtsi macros non-zero?
3. 向量表是否进到约定 ISR / `interrupt` 分发。 / Does the vector table reach the agreed ISR / `interrupt` dispatch?
4. 堆、栈、MSP/PSP（或 RISC-V 等价）是否在启动文件正确设置。 / Are heap, stack, MSP/PSP (or RISC-V equivalents) set correctly in the startup file?
5. 多核 AMP：从核入口、共享内存与 spinlock 配置。 / Multicore AMP: secondary core entry, shared memory, and spinlock config.

---

## 4. OSAL / 启动 / OSAL & Startup

| 配置 / Config | 常见误用 / Common misuse |
| :--- | :--- |
| `OSAL_NULL` | 调用了 `vTaskStartScheduler` / called `vTaskStartScheduler` |
| FreeRTOS | 未提供 heap / tick；优先级与业务假设相反（相对 RT-Thread）/ no heap / tick; priorities opposite to business assumptions (vs RT-Thread) |
| RT-Thread | 未 `rt_system_scheduler_start`；组件未初始化 / `rt_system_scheduler_start` missing; components not initialized |

详见 [osal_switching.md](osal_switching.md)。
See [osal_switching.md](osal_switching.md).

---

## 5. 记录模板 / Report Template

提交 Issue 时建议附上（`text` 块，按需填写）。
Suggested fields when filing an issue (fill in the `text` block as needed).

```text
SoC / 板级:
OSAL / SYSTEM:
优化等级:
现象（上电/复位/烧录）:
config.h 关键宏:
DEV_ID_COUNT:
失败的 compatible / probe 返回值:
是否已确认 HAL 强符号:
```

---

## 相关文档 / Related Docs

- [faq.md](faq.md) · [debug_monitor.md](debug_monitor.md)
- [design_decisions.md](design_decisions.md)
