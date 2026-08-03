# Keil / IDE 集成说明 / Keil & IDE Integration

> 作者偏好：**传统 Keil 5（经典 µVision / MDK）不支持**——不提供官方工程与生成脚本、不维护、不接相关 issue，仅可自行用于调试；**Keil Studio 单独列为理论支持项**——它与 µVision 本质不同（VS Code 内核 + CMake 一等公民），见 §1。
> Author's stance: **traditional Keil 5 (classic µVision / MDK) is NOT supported** — no official projects or generator scripts, unmaintained, no related issues; debugging-only at your own risk. **Keil Studio is listed separately as a theoretically supported entry** — fundamentally different from µVision (VS Code core + first-class CMake), see §1.
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
2. [为何不支持经典 µVision / 传统 Keil 5 / Why Classic µVision / Traditional Keil 5 Is Not Supported](#2-为何不支持经典-µvision--传统-keil-5--why-classic-µvision--traditional-keil-5-is-not-supported)
3. [推荐日常方式 / Recommended Daily Workflow](#3-推荐日常方式-recommended-daily-workflow)
4. [降级路径（若必须出 µVision 工程）/ Degradation Path (If You Must Ship a µVision Project)](#4-降级路径若必须出-µvision-工程-degradation-path-if-you-must-ship-a-µvision-project)

---

## 1. 官方立场 / Official Stance

| 工具 / Tool | 态度 / Stance |
| :--- | :---: |
| **Cursor** / **VS Code** + clangd | **推荐 / Recommended** |
| **CLion** | **推荐 / Recommended** |
| **Zed** | **推荐（只写代码 / coding only）**：clangd 编辑体验佳；不用于调试/烧录 / Great clangd-based editing; not for debug/flash |
| **Qoder** 等现代 AI IDE / Modern AI IDEs | **推荐 / Recommended** |
| **Keil Studio**（基于 VS Code / VS Code-based） | **理论支持 / Theoretically supported**：VS Code 内核 + CMake 一等公民，架构上可接入（见 §2.1）；但作者在 Linux 上的 Keil Studio 遇到本机问题、尚未在 Windows 验证——**如需接入请自行接入** / VS Code core + first-class CMake, architecturally integrable (see §2.1); however the author hit machine-specific issues with Keil Studio on Linux and has not validated on Windows — **DIY integration until then** |
| **EIDE 插件（VS Code）** | **若必须用经典 µVision，优先走这条**（仅为建议 / If you must use classic µVision, prefer this path — advisory only） |
| CMake + Ninja/Make + GCC/Clang | **推荐 / Recommended**（构建 / for building） |
| **传统 Keil 5**：Keil µVision (MDK) + ARMCLANG (AC6) | **不支持 / Not supported**——不提供官方工程/脚本、不接相关 issue；仅可自行用于调试 / no official projects/scripts, no related issues; debugging-only at your own risk |
| ARMCC v5 | **不支持 / Unsupported** |

完整取舍见 [design_decisions.md](design_decisions.md)「工具链 / IDE」。
See the "Toolchain / IDE" section of [design_decisions.md](design_decisions.md) for the full trade-off.

> **Keil Studio（理论支持 / Theoretically supported）**：ARM 基于 VS Code 的下一代开发环境。与经典 µVision 不同——原生 CMake 构建（CMSIS-Toolbox / cbuild）、可装 clangd 扩展获得与本仓 `compile_flags.txt` / `ide/stubs` 一致的编辑体验，并保留 ARM 官方调试器（DAP-Link / ULINK）与云编译。**理论上可用 Keil Studio Pack 插件接入本仓 CMake 流程**；但作者在 **Linux 上的 Keil Studio 实测遇到本机问题**（作者电脑自身原因），**尚未在 Windows 验证通过**——**如需接入 Keil Studio，请自行接入与验证**，作者暂不提供官方验收。
> **Keil Studio (Theoretically supported)**: ARM's next-gen VS Code-based environment. Unlike classic µVision — native CMake builds (CMSIS-Toolbox / cbuild), clangd extension for an editing experience consistent with this repo's `compile_flags.txt` / `ide/stubs`, plus ARM official debug (DAP-Link / ULINK) and cloud build. **In theory the Keil Studio Pack plugin can consume this repo's CMake flow**; however the author's hands-on **Keil Studio on Linux hit machine-specific issues** (the author's own machine) and it has **not been validated on Windows** — **integrating Keil Studio is DIY until then**; the author offers no official acceptance yet.
>
> **如果必须用经典 µVision / If you must use classic µVision**：
> 推荐 **EIDE 插件（VS Code）**（仅为建议，不承诺官方支持）。若选用 µVision 并使用本组件，**请自行编写 py 移植脚本**完成工程生成（见 §4.1）。**不推荐用纯 µVision 写代码与烧录**——写代码/索引/AI 仍走 CMake + clangd 现代 IDE；µVision **仅推荐用于调试**。
> Prefer the **EIDE plugin (VS Code)** (advisory only; no official support implied). If you adopt µVision with this component, **write your own py porting script** to generate the project (see §4.1). **Do not use raw µVision for coding & flashing** — keep CMake + clangd modern IDEs for coding/indexing/AI; µVision is **recommended for debugging only**.

---

## 2. 为何不支持经典 µVision / 传统 Keil 5 / Why Classic µVision / Traditional Keil 5 Is Not Supported

> **支持状态：不支持。** 本节痛点与立场仅针对经典 Keil µVision (MDK)（传统 Keil 5）；**Keil Studio 不同**，见 §2.1。
> The pain points and stance in this section apply **only to classic Keil µVision (MDK)**; **Keil Studio differs**, see §2.1.

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

### 2.1 Keil Studio 与本仓的适配 / Keil Studio Fit

架构上确认 **Keil Studio 与经典 µVision 本质不同**（VS Code 内核 + CMake 一等公民），理论可接入；但作者在 **Linux 上的 Keil Studio 实测遇到本机问题**（作者电脑自身原因，非中间件问题），需换 Windows 验证——**在作者验证通过前，接入 Keil Studio 需自行完成**：
Architecturally **Keil Studio is fundamentally different from classic µVision** (VS Code core + first-class CMake) and should be integrable in theory; however the author hit **machine-specific issues with Keil Studio on Linux** (the author's own machine, not a middleware problem) and needs to validate on Windows — **until then, integrating Keil Studio is DIY**:

| 维度 / Aspect | 经典 µVision (MDK) | Keil Studio |
| :--- | :--- | :--- |
| 内核 / Core | 自研老式 IDE / legacy proprietary IDE | **VS Code**（含 VS Code for the Web / 云版 / incl. cloud variant） |
| 工程格式 / Project format | `.uvprojx`（私有、难 diff）/ private, diff-hostile | **CMake / CMSIS-Toolbox (`cbuild`)** / 标准格式 / standard formats |
| CMake 支持 / CMake support | 无第一公民 / no first-class support | **一等公民**：可直接构建本仓 CMake 流程 / first-class: builds this repo's CMake flow directly |
| 索引 / Indexing | 弱 / weak | **clangd 扩展**：与本仓 `compile_flags.txt` / `ide/stubs` 一致 / clangd extension: consistent with `compile_flags.txt` / `ide/stubs` |
| 调试 / Debug | 官方调试器 / official debug | **保留官方调试体验**（DAP-Link / ULINK） + 云编译 / keeps official debug (DAP-Link / ULINK) + cloud build |
| 本仓立场 / Repo stance | **不推荐、不维护 / not recommended, unmaintained** | **理论支持（待 Windows 验证，接入自理）/ theoretically supported (pending Windows validation, DIY integration)** |

配合要点 / Fit notes：

1. **构建**：Keil Studio 的 CMake 支持直接吃本仓 `add_subdirectory(mini_tree)` 流程；生成头（`genconfig` / `dtc-lite`）随 CMake configure 自动产出。
   **Build**: Keil Studio's CMake support consumes this repo's `add_subdirectory(mini_tree)` flow directly; generated headers (`genconfig` / `dtc-lite`) are produced during CMake configure.
2. **编辑**：装 clangd 扩展后，跳转/补全/诊断与本仓 `compile_flags.txt` / `ide/stubs` 一致，写代码体验等同 VS Code。
   **Editing**: with the clangd extension, navigation/completion/diagnostics match this repo's `compile_flags.txt` / `ide/stubs`; coding feels like VS Code.
3. **调试**：ARM 官方调试器与断点/寄存器/内存视图保留，推荐作为**调试环境**（本仓 `debug_monitor.md` 的日志/监控流程不变）。
   **Debug**: ARM official debugger with breakpoints/registers/memory views is retained; recommended as the **debug environment** (the logging/monitoring flow in `debug_monitor.md` is unchanged).
4. **云编译**：无本地工具链时可走 Keil Studio Cloud；生成物仍按 `.config` 对齐（`CONFIG_OSAL_*` / `CONFIG_SYSTEM_*` 等）。
   **Cloud build**: use Keil Studio Cloud when no local toolchain is available; artifacts still align with `.config` (`CONFIG_OSAL_*` / `CONFIG_SYSTEM_*`, etc.).

> 注：Keil Studio 是**通用 VS Code 生态**的一部分，因此本仓对 VS Code 的既有建议（见 §3）对它同样适用；唯一额外收益是 ARM 官方调试/烧录与云编译的整合。
> Note: Keil Studio is part of the **general VS Code ecosystem**, so the existing VS Code advice in §3 applies to it too; the extra benefit is the integration of ARM official debug/flash and cloud build.
>
> **接入现状 / Integration status**：理论可接入（架构层面无阻碍——`add_subdirectory(mini_tree)` + Keil Studio Pack 插件即可走通 CMake 流程，生成头随 configure 自动产出）；但**作者在 Linux 上的 Keil Studio 实测有本机问题**（作者电脑自身原因），**尚未在 Windows 验证通过**。若你需要接入 Keil Studio，请**自行完成接入与验证**（参考 §1 / §3 的 VS Code + clangd 工作流），作者暂不提供官方验收。

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

## 4. 降级路径（若必须出 µVision 工程）/ Degradation Path (If You Must Ship a µVision Project)

> **不受支持 / Unsupported**：下列步骤针对**经典 Keil µVision (MDK)**，由使用方自行承担；本仓不接 µVision 相关 issue / PR（除非顺带修了与工具无关的中间件 bug）。**Keil Studio 不在此列**（见 §2.1——属理论支持，待 Windows 验证）。
> The steps below target **classic Keil µVision (MDK)** and are at your own risk; this repo does not take µVision-related issues / PRs (unless they incidentally fix a tool-unrelated middleware bug). **Keil Studio is not covered here** (see §2.1 — it is a supported entry).

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

- **写代码 / Review / AI**：继续用 Cursor / VS Code / CLion / Qoder（Keil Studio 亦可，因其即 VS Code 生态）。
  **Coding / Review / AI**: keep using Cursor / VS Code / CLion / Qoder (Keil Studio works too, as it is the VS Code ecosystem).
- **µVision**：**仅用于调试**（接调试器/仿真器）；编译、烧录与日常开发仍以 CMake 流程为准。
  **µVision**: **debugging only** (via the debug adapter); build, flashing and daily development stay on the CMake flow.

不要把 µVision 当成唯一编辑器。
Do not make µVision your only editor.

---

## 相关文档 / Related Documents

- [design_decisions.md](design_decisions.md) · [getting_started.md](getting_started.md) · [CONTRIBUTING.md](../CONTRIBUTING.md)
