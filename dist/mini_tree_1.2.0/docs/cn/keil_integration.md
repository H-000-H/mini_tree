# Keil / IDE 集成说明

> 我**不用传统 Keil 5（经典 µVision / MDK）**，这个仓库也**不提供、不跟进**它的工程与脚本。我试过 **Keil Studio（即 Keil 6）能正常跑本组件（mini-tree）**，而且它的调试和 Keil 5 没区别、就是更现代（VS Code 内核 + CMake 原生支持，跟 µVision 不是一回事）。**传统 Keil 5 这条路在本仓库里走不通，需要的话只能自己按 §4 的降级路径生成工程**；想用 Keil 的话，直接上 Keil 6 更省事，见 §1。
>
> 本仓库主路径是 **CMake + clangd + 现代 IDE**（Cursor / VS Code / CLion / Qoder 等）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 选型 IDE 的人；或客户强制要求 Keil 交付的团队 |
| **相关** | [design_decisions.md](design_decisions.md)（工具链偏好）· [getting_started.md](getting_started.md) · [faq.md](faq.md) |

---

## 目录

1. [我的选择](#1-我的选择)
2. [为何不支持经典 µVision / 传统 Keil 5](#2-为何不支持经典-µvision--传统-keil-5)
3. [推荐日常方式](#3-推荐日常方式)
4. [降级路径（若必须出 µVision 工程）](#4-降级路径若必须出-µvision-工程)

---

## 1. 我的选择

| 工具 | 态度 |
| :--- | :---: |
| **Cursor** / **VS Code** + clangd | **推荐** |
| **CLion** | **推荐** |
| **Zed** | **推荐（只写代码）**：clangd 编辑体验佳；不用于调试/烧录 |
| **Qoder** 等现代 AI IDE | **推荐** |
| **Keil Studio**（基于 VS Code） | **推荐（已验证）**：VS Code 内核 + CMake 一等公民，已实测可正常使用本组件（见 §2.1）；推荐用 Keil Studio 搭配 mini_tree 开发 |
| **EIDE 插件（VS Code）** | **若必须用经典 µVision，优先走这条**（仅为建议） |
| CMake + Ninja/Make + GCC/Clang | **推荐**（构建） |
| **传统 Keil 5**：Keil µVision (MDK) + ARMCLANG (AC6) | **不提供、不跟进**——仓库不配工程/脚本；**这条路在本仓库走不通，需要只能按 §4 降级路径自己生成** |
| ARMCC v5 | 不用 |

完整取舍见 [design_decisions.md](design_decisions.md)「工具链 / IDE」。

> **Keil Studio（推荐，我试过能用）**：ARM 基于 VS Code 的下一代开发环境。跟经典 µVision 不一样——原生 CMake 构建（CMSIS-Toolbox / cbuild）、能装 clangd 扩展，编辑体验和本仓 `compile_flags.txt` / `ide/stubs` 一致，还保留 ARM 官方调试器（DAP-Link / ULINK）和云编译。**实测 Keil Studio Pack 插件直接接本仓 CMake 流程就能跑、组件正常用**；想用 Keil 就选它搭配 mini_tree。
>
> **如果非要用经典 µVision**：建议走 **EIDE 插件（VS Code）**这条。要是真拿 µVision 接本组件，**自己写个 py 移植脚本**生成工程（见 §4.1）。**纯 µVision 写代码、烧录、调试本仓库都不跟进**——因为我已经试过 Keil Studio（Keil 6）能正常用本组件，而且它调试跟 Keil 5 没区别、就是更现代（VS Code 内核 + CMake 原生），所以直接上 Keil 6、用 CMake + clangd + 现代 IDE 更顺手。

---

## 2. 为何不支持经典 µVision / 传统 Keil 5

> 下面这些坑只针对经典 Keil µVision (MDK)（传统 Keil 5）；**Keil Studio 是另一回事**，见 §2.1。

对本仓这种「CMake 生成头 + 多目录中间件 + 可选 C++/AI 协作」的结构，Keil µVision 典型痛点：

| 问题 | 说明 |
| :--- | :--- |
| **几乎无法干净集成** | 无第一公民 CMake；`BOARD_DTS` / genconfig / dtc-lite / `ide/stubs` 工作流都要手工或旁路脚本搬进工程 |
| **跳转与索引弱** | 相对 clangd / CLion 差一截；不做自动化时 Include/文件列表极易漂 |
| **C++ 支持差** | `SYSTEM_CPP`、ETL、现代方言体验弱 |
| **AI 集成度差** | 与 Cursor / Qoder 多文件理解工作流不兼容 |
| **工程文件难协作** | `.uvprojx` 易冲突、难 diff |

所以：**仓库里没放、也不打算维护官方 `.uvprojx`，远古那套「生成 Keil 工程」脚本也不再当作功能来维护。**

远古版曾有过「用 Python 扫源文件生成 µVision 工程」的思路，仅可作历史参考；当前作者立场是迁到现代工具。

### 2.1 Keil Studio 与本仓的适配

架构上确认 **Keil Studio 与经典 µVision 本质不同**（VS Code 内核 + CMake 一等公民），且**已实测可正常使用本组件**：

| 维度 | 经典 µVision (MDK) | Keil Studio |
| :--- | :--- | :--- |
| 内核 | 自研老式 IDE | **VS Code**（含 VS Code for the Web / 云版） |
| 工程格式 | `.uvprojx`（私有、难 diff） | **CMake / CMSIS-Toolbox (`cbuild`)** / 标准格式 |
| CMake 支持 | 无第一公民 | **一等公民**：可直接构建本仓 CMake 流程 |
| 索引 | 弱 | **clangd 扩展**：与本仓 `compile_flags.txt` / `ide/stubs` 一致 |
| 调试 | 官方调试器 | **保留官方调试体验**（DAP-Link / ULINK） + 云编译 |
| 我的态度 | **不推荐、懒得维护** | **推荐（试过能用，搭配 mini_tree 正常）** |

配合要点：

1. **构建**：Keil Studio 的 CMake 支持直接吃本仓 `add_subdirectory(mini_tree)` 流程；生成头（`genconfig` / `dtc-lite`）随 CMake configure 自动产出。
2. **编辑**：装 clangd 扩展后，跳转/补全/诊断与本仓 `compile_flags.txt` / `ide/stubs` 一致，写代码体验等同 VS Code。
3. **调试**：ARM 官方调试器与断点/寄存器/内存视图保留，推荐作为**调试环境**（本仓 `debug_monitor.md` 的日志/监控流程不变）。
4. **云编译**：无本地工具链时可走 Keil Studio Cloud；生成物仍按 `.config` 对齐（`CONFIG_OSAL_*` / `CONFIG_SYSTEM_*` 等）。

> 注：Keil Studio 是**通用 VS Code 生态**的一部分，因此本仓对 VS Code 的既有建议（见 §3）对它同样适用；唯一额外收益是 ARM 官方调试/烧录与云编译的整合。
>
> **接入现状**：实测能用——`add_subdirectory(mini_tree)` + Keil Studio Pack 插件就能走通 CMake 流程，生成头随 configure 自动产出；**想用 Keil 就选它搭配 mini_tree**。传统 Keil 5（经典 µVision / MDK）我是不用的，直接上 Keil Studio 吧。

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

## 4. 降级路径（若必须出 µVision 工程）

> 下面这些是针对**经典 Keil µVision (MDK)**的降级办法，要用的话得自己折腾；**本仓库不接受任何传统 Keil 5 工程相关的 issue / PR，分支也不行**（除非顺手修了跟工具无关的中间件 bug）。**Keil Studio 不在此列**（见 §2.1——试过能用，直接上就行）。

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
| 本仓现状 | **不自带**这个生成器；远古脚本要是还能翻出来，当模板看看就行，**我不维护** |

> **欢迎大家为自己的 Keil 5 场景写兼容/生成工具**：只要不污染本仓库公共头与构建主路径，单独成仓或作为 `tools/` 子模块都行；做得好我可以收录进 [docs/ecosystem.md](ecosystem.md)。但**仓库本体不接受传统 Keil 5 工程 PR/issue/分支**（见上文与 [CONTRIBUTING.md](../../CONTRIBUTING.md)）。

手工把文件一个个加进 µVision **更差**，不推荐。

### 4.2 仍建议的工作流拆分

即使交付物是 Keil 工程：

- **写代码 / Review / AI**：继续用 Cursor / VS Code / CLion / Qoder（Keil Studio 亦可，因其即 VS Code 生态）。
- **µVision**：**调试和写代码都不推荐**（接调试器/仿真器也优先 Keil Studio / 现代 IDE）；编译、烧录和日常开发还是走 CMake 流程，整体转到更现代体系更舒服。

不要把 µVision 当成唯一编辑器。

---

## 相关文档

- [design_decisions.md](design_decisions.md) · [getting_started.md](getting_started.md) · [CONTRIBUTING.md](../CONTRIBUTING.md)
