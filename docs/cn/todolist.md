# 待办清单

> 当前进行中 / 待启动的工程任务。规划见 [roadmap.md](roadmap.md)、已知问题见 [problem_summary.md](problem_summary.md)。状态：待办 / 进行中 / 待验 / 完成。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 接任务 / 跟进 |
| **相关** | [roadmap.md](roadmap.md) · [problem_summary.md](problem_summary.md) |

---

## 1. 待办

| 编号 | 任务 | 关联 | 优先级 |
| :--- | :--- | :--- | :---: |
| T1 | `err_section` 独立段进默认链接脚本 | P1 | 高 |
| T2 | 裸机 C++ 栈归 `system` 任务管理 | P2 | 高 |
| T3 | dtc-lite 固定单一 `BOARD_DTSI_DIR` | P3 | 高 |
| T4 | VFS 池重置自动再绑定 | P4 | 中 |
| T5 | `ide/stubs/` CMake configure 钩子再生 | P9 | 低 |

---

## 2. 进行中

| 编号 | 任务 | 关联 | 进度 |
| :--- | :--- | :--- | :---: |
| T6 | 补 GPIO / SPI / I2C HAL 变体 | — | 30% |
| T7 | `LOG_*` 关日志后去符号 | P6 | 60% |

---

## 3. 待验

| 编号 | 任务 | 关联 | 阻塞 |
| :--- | :--- | :--- | :--- |
| T8 | `build_size.py --format=baseline` 首次跑生成基线 | P8 | 需平台环境 |
| T9 | `hal/amp` 单核默认不编验证 | P5 | 需单核板 |

---

## 4. 完成

| 编号 | 任务 | 关联 |
| :--- | :--- | :--- |
| T10 | 文档双语拆分 | — |
| T11 | `compiler_compat_poison.h` 接入 | — |

---

## 相关文档

- [roadmap.md](roadmap.md) · [problem_summary.md](problem_summary.md)
