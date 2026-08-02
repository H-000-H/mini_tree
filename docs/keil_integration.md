# Keil / IDE 集成说明 / Keil & IDE Integration

> 作者偏好：**不推荐、且已不再支持以 Keil 作为主开发环境**。
> Author's stance: Keil is **not recommended and no longer supported** as the primary development environment.
>
> 本仓库主路径是 **CMake + clangd + 现代 IDE**（Cursor / VS Code / CLion / Qoder 等）。
> The main path for this repo is **CMake + clangd + modern IDEs** (Cursor / VS Code / CLion / Qoder, etc.).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 选型 IDE 的人；或客户强制要求 Keil 交付的团队 / IDE decision-makers; teams forced to deliver with Keil by customers |
| **相关 / Related** | [design_decisions.md](design_decisions.md)（工具链偏好 / toolchain preference）· [getting_started.md](getting_started.md) · [faq.md](faq.md) |

---

## 目录 / Contents

1. [官方立场 / Official Stance](#1-官方立场-official-stance)
2. [为何不推荐 / 已不支持 / Why Not Recommended / No Longer Supported](#2-为何不推荐-已不支持-why-not-recommended-no-longer-supported)
3. [推荐日常方式 / Recommended Daily Workflow](#3-推荐日常方式-recommended-daily-workflow)
4. [降级路径（若必须出 Keil 工程）/ Degradation Path (If You Must Ship a Keil Project)](#4-降级路径若必须出-keil-工程-degradation-path-if-you-must-ship-a-keil-project)

---

## 1. 官方立场 / Official Stance

| 工具 / Tool | 态度 / Stance |
| :--- | :---: |
| **Cursor** / **VS Code** + clangd | **推荐 / Recommended** |
| **CLion** | **推荐 / Recommended** |
| **Zed** | **推荐（只写代码 / coding only）**：clangd 编辑体验佳；不用于调试/烧录 / Great clangd-based editing; not for debug/flash |
| **Qoder** 等现代 AI IDE / Modern AI IDEs | **推荐 / Recommended** |
| **EIDE 插件（VS Code）/ Keil Studio** | **若必须用 Keil，优先走这两条路**（仅为建议 / If you must use Keil, prefer these two paths — advisory only） |
| CMake + Ninja/Make + GCC/Clang | **推荐 / Recommended**（构建 / for building） |
| Keil MDK + ARMCLANG (AC6) | **不推荐**；作者**已停止支持** / **Not recommended**; **no longer supported** by the author |
| ARMCC v5 | **不支持 / Unsupported** |

完整取舍见 [design_decisions.md](design_decisions.md)「工具链 / IDE」。
See the "Toolchain / IDE" section of [design_decisions.md](design_decisions.md) for the full trade-off.

> **如果必须用 Keil / If you must use Keil**：
> 推荐 **EIDE 插件（VS Code）** 或 **Keil Studio**（这只是建议，不承诺官方支持）。若选用 Keil 并使用本组件，**请自行编写 py 移植脚本**完成工程生成（见 §4.1）。**不推荐用纯 Keil 写代码与烧录**——写代码/索引/AI 仍走 CMake + clangd 现代 IDE；Keil **仅推荐用于调试**。
> Prefer the **EIDE plugin (VS Code)** or **Keil Studio** (advisory only; no official support implied). If you adopt Keil with this component, **write your own py porting script** to generate the project (see §4.1). **Do not use raw Keil for coding & flashing** — keep CMake + clangd modern IDEs for coding/indexing/AI; Keil is **recommended for debugging only**.

---

## 2. 为何不推荐 / 已不支持 / Why Not Recommended / No Longer Supported

对本仓这种「CMake 生成头 + 多目录中间件 + 可选 C++/AI 协作」的结构，Keil µVision 典型痛点：
For a repo structured as "CMake-generated headers + multi-directory middleware + optional C++/AI collaboration", Keil µVision has typical pain points:

| 问题 / Problem | 说明 / Details |
| :--- | :--- |
| **几乎无法干净集成 / Almost impossible to integrate cleanly** | 无第一公民 CMake；`BOARD_DTS` / genconfig / dtc-lite / `ide/stubs` 工作流都要手工或旁路脚本搬进工程 / No first-class CMake; the `BOARD_DTS` / genconfig / dtc-lite / `ide/stubs` workflow has to be moved in by hand or via side scripts |
| **跳转与索引弱 / Weak navigation & indexing** | 相对 clangd / CLion 差一截；不做自动化时 Include/文件列表极易漂 / Weaker than clangd / CLion; include/file lists drift easily without automation |
| **C++ 支持差 / Poor C++ support** | `SYSTEM_CPP`、ETL、现代方言体验弱 / Weak experience with `SYSTEM_CPP`, ETL, and modern dialects |
| **AI 集成度差 / Poor AI integration** | 与 Cursor / Qoder 多文件理解工作流不兼容 / Incompatible with the multi-file understanding workflow of Cursor / Qoder |
| **工程文件难协作 / Hard-to-collaborate project files** | `.uvprojx` 易冲突、难 diff / `.uvprojx` conflicts easily and diffs poorly |

因此：**仓库不提供、不维护官方 `.uvprojx`，也不再维护远古「生成 Keil 工程」脚本为受支持功能。**
Therefore: **the repo neither ships nor maintains an official `.uvprojx`, and the legacy "generate Keil project" script is no longer a supported feature.**

远古版曾有过「用 Python 扫源文件生成 µVision 工程」的思路，仅可作历史参考；当前作者立场是迁到现代工具。
A legacy approach once used "a Python script scanning sources to generate a µVision project"; it is historical reference only. The current stance is to move to modern tooling.

---

## 3. 推荐日常方式 / Recommended Daily Workflow

```text
Cursor / VS Code / CLion / Qoder
  → 打开 mini_tree 仓库根 / open the mini_tree repo root
  → clangd 读 compile_flags.txt（或 CMake 导出的 compile_commands.json）
  → clangd reads compile_flags.txt (or the compile_commands.json exported by CMake)
  → 平台工程 CMake 负责真机链接与烧录
  → the platform project CMake handles real-hardware linking and flashing
  → （可选）任意调试器外壳；不必绑死 Keil
  → (optional) any debugger front-end; no need to lock into Keil
```

IDE 验收见 [getting_started.md](getting_started.md) §7、[debug_monitor.md](debug_monitor.md)。
See [getting_started.md](getting_started.md) §7 and [debug_monitor.md](debug_monitor.md) for IDE acceptance.

---

## 4. 降级路径（若必须出 Keil 工程）/ Degradation Path (If You Must Ship a Keil Project)

> **不受支持 / Unsupported**：下列步骤由使用方自行承担；本仓不接 Keil 相关 issue / PR（除非顺带修了与工具无关的中间件 bug）。
> The steps below are at your own risk; this repo does not take Keil-related issues / PRs (unless they incidentally fix a tool-unrelated middleware bug).

### 4.1 最简单做法：Python 自动生成 `.uvprojx` / Simplest Approach: Generate `.uvprojx` with Python

(远古文档/工具链)最省事的降级路径是：
The least-effort degradation path (per the legacy docs/toolchain) is:

1. 仍用 **CMake**（或独立调用）跑完 `genconfig` + `dtc-lite`，得到 `config.h`、`board_*` 等生成物。
   Still run **CMake** (or invoke standalone) through `genconfig` + `dtc-lite` to produce `config.h`, `board_*`, etc.
2. 写（或从远古拷贝后自改）一个 **Python 脚本**：扫描中间件 + 平台源、`IncludePath`、预定义宏（对齐 `.config`）、链接脚本意图（含 `ERR_SECTION_BASE`），**生成 / 刷新** `.uvprojx`（必要时连带 `.uvoptx`）。
   Write (or copy-and-modify from the legacy one) a **Python script** that scans middleware + platform sources, `IncludePath`, preprocessor macros (aligned with `.config`), and linker-script intent (including `ERR_SECTION_BASE`), then **generates / refreshes** `.uvprojx` (plus `.uvoptx` if needed).
3. 用 **ARMCLANG (AC6)** 打开生成工程编译；**不要用 ARMCC v5**。
   Compile with **ARMCLANG (AC6)**; **do not use ARMCC v5**.
4. 源树或 Kconfig 变更后 **重新跑生成脚本**，不要长期手改 `.uvprojx`。
   **Re-run the generator** after source-tree or Kconfig changes; do not hand-edit `.uvprojx` long-term.

要点 / Key points：

| 项 / Item | 说明 / Details |
| :--- | :--- |
| 生成物目录 / Artifact directory | 必须进 Include；与 CMake 输出路径一致 / Must be in Include; match the CMake output paths |
| 宏 / Macros | `CONFIG_OSAL_*` / `CONFIG_SYSTEM_*` / `CONFIG_SYS_LOG_*` 等与 `.config` 一致 / Match `.config` |
| 文件列表 / File list | 由脚本从目录规则生成，避免手点几百个文件 / Generated by the script from directory rules; avoid clicking hundreds of files by hand |
| 本仓现状 / Repo status | **不附带**该生成器；远古脚本若还能找到，仅作模板，**作者不维护** / **No generator is shipped**; if the legacy script can still be found, treat it only as a template — the author does **not** maintain it |

手工把文件一个个加进 µVision **更差**，不推荐。
Hand-adding files one by one into µVision is **worse**; not recommended.

### 4.2 仍建议的工作流拆分 / Recommended Workflow Split

即使交付物是 Keil 工程：
Even if the deliverable is a Keil project:

- **写代码 / Review / AI**：继续用 Cursor / VS Code / CLion / Qoder。
  **Coding / Review / AI**: keep using Cursor / VS Code / CLion / Qoder.
- **Keil**：**仅用于调试**（接调试器/仿真器）；编译、烧录与日常开发仍以 CMake 流程为准。
  **Keil**: **debugging only** (via the debug adapter); build, flashing and daily development stay on the CMake flow.

不要把 Keil 当成唯一编辑器。
Do not make Keil your only editor.

---

## 相关文档 / Related Documents

- [design_decisions.md](design_decisions.md) · [getting_started.md](getting_started.md) · [CONTRIBUTING.md](../CONTRIBUTING.md)
