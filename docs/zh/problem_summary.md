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

## 4. 已规避（不修）

| 编号 | 问题 | 处理 |
| :--- | :--- | :--- |
| P10 | 经典 Keil µVision 集成 | 本仓库不提供、不跟进，见 [keil_integration.md](keil_integration.md) |
| P11 | ARMCC v5 编译 | 不支持，仅 ARMCLANG (AC6) |

---

## 相关文档

- [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md)
