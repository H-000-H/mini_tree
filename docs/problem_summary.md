# 问题总结 — 通用排查轴

> 将历史「上电正常 / 复位异常、烧录后常量区怪异」等经验，收敛为**与 SoC 无关**的检查清单。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 已按 FAQ 仍搞不定的人 |
| **相关** | [faq.md](faq.md) · [debug_monitor.md](debug_monitor.md) · [porting_guide.md](porting_guide.md) |

---

## 目录

1. [现象分类](#1-现象分类)
2. [中间件侧检查](#2-中间件侧检查)
3. [平台侧检查](#3-平台侧检查)
4. [OSAL / 启动](#4-osal--启动)
5. [记录模板](#5-记录模板)

---

## 1. 现象分类

| 现象 | 优先怀疑 |
| :--- | :--- |
| 首次上电 OK，复位后挂 | 外设时钟门控、WDT、全局/静态未再初始化 |
| 常量/只读数据异常 | 链接脚本、Cache（多核）、错误段属性 |
| Probe 大批失败 | DTS status、宏未展开、compatible 不匹配 |
| 单独外设一直 NOTSUPP | HAL 仍为 weak |
| 开优化后坏、Debug 正常 | UB、缺 `volatile`、错误 ISR 属性 |

---

## 2. 中间件侧检查

1. `config.h` 与预期 OSAL/SYSTEM 一致。  
2. 生成 `board_nodes.h` 的 `DEV_ID_COUNT`、chosen 是否合理。  
3. `board_driver_probe_all` 日志：谁失败、criticality 是什么。  
4. 是否误在业务路径触发 `compiler_compat_poison`。  
5. `system_init_complete` 是否在开调度前调用。  

---

## 3. 平台侧检查

1. HAL 强符号：`nm` 看定义文件。  
2. `VENDOR_INC_DIRS` 是否让 dtsi 宏变成非零。  
3. 向量表是否进到约定 ISR / `interrupt` 分发。  
4. 堆、栈、MSP/PSP（或 RISC-V 等价）是否在启动文件正确设置。  
5. 多核 AMP：从核入口、共享内存与 spinlock 配置。  

---

## 4. OSAL / 启动

| 配置 | 常见误用 |
| :--- | :--- |
| `OSAL_NULL` | 调用了 `vTaskStartScheduler` |
| FreeRTOS | 未提供 heap / tick；优先级与业务假设相反（相对 RT-Thread） |
| RT-Thread | 未 `rt_system_scheduler_start`；组件未初始化 |

详见 [osal_switching.md](osal_switching.md)。

---

## 5. 记录模板

提交 Issue 时建议附上：

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

## 相关文档

- [faq.md](faq.md) · [debug_monitor.md](debug_monitor.md)  
- [design_decisions.md](design_decisions.md)
