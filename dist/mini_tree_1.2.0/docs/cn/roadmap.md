# 路线图

> 中间件中期规划。具体任务与状态见 [todolist.md](todolist.md)、已知问题见 [problem_summary.md](problem_summary.md)。季度为大致窗口，非承诺。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 规划 / 评估方向 |
| **相关** | [todolist.md](todolist.md) · [problem_summary.md](problem_summary.md) |

---

## 1. 近期（本季度）

| 方向 | 内容 |
| :--- | :--- |
| `err_section` 独立段 | 推动链接脚本默认单独放置（解决 P1） |
| 裸机 C++ 栈归属 | 归 `system` 任务管理（解决 P2） |
| dtc-lite 嵌套 include | 固定单一 `BOARD_DTSI_DIR`（解决 P3） |

---

## 2. 中期（下 1–2 季度）

| 方向 | 内容 |
| :--- | :--- |
| VFS 池重置自动再绑定 | 框架层统一处理（解决 P4） |
| 更多 HAL 后端 | 按需求补 GPIO / SPI / I2C 变体 |
| `ide/stubs/` 同步 | CMake configure 钩子自动再生（解决 P9） |

---

## 3. 远期（3+ 季度）

| 方向 | 内容 |
| :--- | :--- |
| 网络栈整合 | lwIP 经 VFS 暴露标准化接口 |
| Rust 互操作 | `cmake/rust.cmake` 实验性接入 |
| Keil Studio 验证 | 已试过能用，想用 Keil 直接上 Keil Studio 即可（见 [keil_integration.md](keil_integration.md)） |

---

## 4. 不做

| 方向 | 原因 |
| :--- | :--- |
| 经典 Keil µVision 官方支持 | 架构不契合，本仓库不提供、不跟进，见 [keil_integration.md](keil_integration.md) |
| ARMCC v5 支持 | 仅 ARMCLANG (AC6) |

---

## 相关文档

- [todolist.md](todolist.md) · [problem_summary.md](problem_summary.md) · [design_decisions.md](design_decisions.md)
