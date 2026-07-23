# Keil / IDE 集成说明

> 作者偏好：**不推荐、且已不再支持以 Keil 作为主开发环境**。  
> 本仓库主路径是 **CMake + clangd + 现代 IDE**（Cursor / VS Code / CLion / Qoder 等）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 选型 IDE 的人；或客户强制要求 Keil 交付的团队 |
| **相关** | [design_decisions.md](design_decisions.md)（工具链偏好） · [getting_started.md](getting_started.md) · [faq.md](faq.md) |

---

## 目录

1. [官方立场](#1-官方立场)
2. [为何不推荐 / 已不支持](#2-为何不推荐--已不支持)
3. [推荐日常方式](#3-推荐日常方式)
4. [降级路径（若必须出 Keil 工程）](#4-降级路径若必须出-keil-工程)

---

## 1. 官方立场

| 工具 | 态度 |
| :--- | :---: |
| **Cursor** / **VS Code** + clangd | **推荐** |
| **CLion** | **推荐** |
| **Qoder** 等现代 AI IDE | **推荐** |
| CMake + Ninja/Make + GCC/Clang | **推荐**（构建） |
| Keil MDK + ARMCLANG (AC6) | **不推荐**；作者**已停止支持** |
| ARMCC v5 | **不支持** |

完整取舍见 [design_decisions.md](design_decisions.md)「工具链 / IDE」。

---

## 2. 为何不推荐 / 已不支持

对本仓这种「CMake 生成头 + 多目录中间件 + 可选 C++/AI 协作」的结构，Keil µVision 典型痛点：

| 问题 | 说明 |
| :--- | :--- |
| **几乎无法干净集成** | 无第一公民 CMake；`BOARD_DTS` / genconfig / dtc-lite / `ide/stubs` 工作流都要手工或旁路脚本搬进工程。 |
| **跳转与索引弱** | 相对 clangd / CLion 差一截；不做自动化时 Include/文件列表极易漂。 |
| **C++ 支持差** | `SYSTEM_CPP`、ETL、现代方言体验弱。 |
| **AI 集成度差** | 与 Cursor / Qoder 多文件理解工作流不兼容。 |
| **工程文件难协作** | `.uvprojx` 易冲突、难 diff。 |

因此：**仓库不提供、不维护官方 `.uvprojx`，也不再维护远古「生成 Keil 工程」脚本为受支持功能。**  
远古版曾有过「用 Python 扫源文件生成 µVision 工程」的思路，仅可作历史参考；当前作者立场是迁到现代工具。

---

## 3. 推荐日常方式

```text
Cursor / VS Code / CLion / Qoder
  → 打开 mini_tree 仓库根
  → clangd 读 compile_flags.txt（或 CMake 导出的 compile_commands.json）
  → 平台工程 CMake 负责真机链接与烧录
  → （可选）任意调试器外壳；不必绑死 Keil
```

IDE 验收见 [getting_started.md](getting_started.md) §7、[debug_monitor.md](debug_monitor.md)。

---

## 4. 降级路径（若必须出 Keil 工程）

> **不受支持**：下列步骤由使用方自行承担；本仓不接 Keil 相关 issue / PR（除非顺带修了与工具无关的中间件 bug）。

### 4.1 最简单做法：Python 自动生成 `.uvprojx`

(远古文档/工具链)最省事的降级路径是：

1. 仍用 **CMake**（或独立调用）跑完 `genconfig` + `dtc-lite`，得到 `config.h`、`board_*` 等生成物。  
2. 写（或从远古拷贝后自改）一个 **Python 脚本**：扫描中间件 + 平台源、`IncludePath`、预定义宏（对齐 `.config`）、链接脚本意图（含 `ERR_SECTION_BASE`），**生成 / 刷新** `.uvprojx`（必要时连带 `.uvoptx`）。  
3. 用 **ARMCLANG (AC6)** 打开生成工程编译；**不要用 ARMCC v5**。  
4. 源树或 Kconfig 变更后 **重新跑生成脚本**，不要长期手改 `.uvprojx`。

要点：

| 项 | 说明 |
| :--- | :--- |
| 生成物目录 | 必须进 Include；与 CMake 输出路径一致 |
| 宏 | `CONFIG_OSAL_*` / `CONFIG_SYSTEM_*` / `CONFIG_SYS_LOG_*` 等与 `.config` 一致 |
| 文件列表 | 由脚本从目录规则生成，避免手点几百个文件 |
| 本仓现状 | **不附带**该生成器；远古脚本若还能找到，仅作模板，**作者不维护** |

手工把文件一个个加进 µVision **更差**，不推荐。

### 4.2 仍建议的工作流拆分

即使交付物是 Keil 工程：

- **写代码 / Review / AI**：继续用 Cursor / VS Code / CLion / Qoder。  
- **Keil**：只消费生成出的 `.uvprojx` 做客户指定的编译或烧录。  

不要把 Keil 当成唯一编辑器。

---

## 相关文档

- [design_decisions.md](design_decisions.md) · [getting_started.md](getting_started.md) · [CONTRIBUTING.md](../CONTRIBUTING.md)
