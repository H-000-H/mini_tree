# mini_tree — 架构设计决策 / Architecture Design Decisions

> 当前中间件 shelf 仍生效的设计决策摘要。
> A summary of the design decisions still in effect for the middleware shelf.
>
> 第三方归属与许可证清单见根目录 [NOTICE](../NOTICE)（Apache 惯例）；本文件不是法律 NOTICE。
> Third-party attribution and licenses live in the root [NOTICE](../NOTICE) (Apache convention); this file is not a legal NOTICE.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 需要理解「为什么这样分层」的人 / Anyone who needs to understand "why it is layered this way" |
| **相关 / Related** | [architecture.md](architecture.md) · [references.md](references.md) · [ecosystem.md](ecosystem.md) · [CHANGELOG.md](../CHANGELOG.md) · [NOTICE](../NOTICE) |

---

## 核心架构决策 / Core Architecture Decisions

| # | 决策 / Decision | 后果 / Consequence |
| :---: | :--- | :--- |
| 1 | 中间件与厂商 SDK 解耦 / Decouple the middleware from vendor SDKs | HAL 头中立 + weak `.c`；平台强符号 + DTS / Neutral HAL headers + weak `.c`; platform strong symbols + DTS |
| 2 | 硬件直投 / Hardware direct-inject | DTSI 宏进结构体，无 enum 映射层 / DTSI macros into structs; no enum mapping layer |
| 3 | 编译期 probe / Compile-time probe | `DRIVER_REGISTER` + dtc-lite 静态表 / `DRIVER_REGISTER` + dtc-lite static tables |
| 4 | Bus 强制隔离 / Enforced bus isolation | `#pragma GCC poison` 防上层直调 HAL / `#pragma GCC poison` blocks upper layers from calling HAL directly |
| 5 | OSAL 三栖 / OSAL triple-backend | FreeRTOS / RT-Thread / NULL，Kconfig 裁剪 / FreeRTOS / RT-Thread / NULL, trimmed by Kconfig |
| 6 | 双系统后端 / Dual system backends | `SYSTEM_C` / `SYSTEM_CPP` 编译期二选一 / compile-time choice of `SYSTEM_C` / `SYSTEM_CPP` |
| 7 | 统一错误码 / Unified error codes | `status.h` 的 `VFS_ERR_*` / `OSAL_ERR_*` |

---

## 安全相关决策 / Safety-Related Decisions

- `compiler_compat_poison`：限制堆、stdio、裸 `mem*` / restrict heap, stdio, and bare `mem*`
- `safe_state` + 可选 WDT / Flash Scrubber
- probe 期 `board_safety_register_shutdown` / at probe time
- `ERR_PTR` + `error_symbols.ld`

---

## 与平台仓库的关系 / Relationship with Platform Repos

| 本仓库 / This repo | 平台仓库（如 Heterogeneous-Multicore）/ Platform repo (e.g. Heterogeneous-Multicore) |
| :--- | :--- |
| 通用中间件、文档、ide stubs、占位 DTS / Generic middleware, docs, IDE stubs, placeholder DTS | SoC HAL、完整 dtsi、厂商 `-I`、板级启动与验证 / SoC HAL, full dtsi, vendor `-I`, board bring-up & validation |

---

## 已知限制 / Known Limitations

1. 无厂商 SDK 时本仓只能做静态分析 / 空 stub 链接，不能单独验证外设。
   Without a vendor SDK, this repo can only do static analysis / empty-stub linking; peripherals cannot be validated standalone.
2. OSAL 优先级数值语义随后端变化。
   OSAL priority number semantics vary with the backend.
3. USB 依赖板级 `usb_tusb_port`；中间件不内嵌具体 MCU 的 TinyUSB port。
   USB depends on the board-level `usb_tusb_port`; the middleware does not embed any MCU-specific TinyUSB port.
4. 默认 `board.dts` 无真实外设节点。
   The default `board.dts` has no real peripheral nodes.

---

## 作者偏好与取舍 / Author Preferences & Trade-offs

> **不是硬规范**。与 [service_spec.md](service_spec.md) / [fast_path.md](fast_path.md) 冲突时，以硬规范为准。
> **Not hard rules.** When in conflict with [service_spec.md](service_spec.md) / [fast_path.md](fast_path.md), the hard specs win.
>
> 选型对照与外部项目见 [references.md](references.md)。
> See [references.md](references.md) for external comparisons.

### 语言分层 / Language Layering

| 层次 / Layer | 偏好 / Preference | 取舍说明 / Trade-off |
| :--- | :--- | :--- |
| **应用层以下**（HAL / Bus / VFS / board / core / OSAL）| 默认 **全 C**；有能力团队可上 **Rust** / C by default; capable teams may use **Rust** | C 链接面最干净、与厂商 SDK / 弱符号最合拍。Rust 适合边界清晰、愿维护 FFI 的团队，不作为本仓默认路径。 / C has the cleanest link surface and fits vendor SDKs / weak symbols best. Rust suits teams with crisp boundaries willing to maintain FFI; it is not this repo's default path. |
| **应用层及以上**（业务服务、UI、策略、工具链侧）| **C++** 或 **Rust** | 表达力与类型约束更重要；C++ 可与本仓 `SYSTEM_CPP` / ETL 路径衔接。中间件公共头仍避免强绑 C++ 运行时。 / Expressiveness and type constraints matter more; C++ can plug into this repo's `SYSTEM_CPP` / ETL path. Middleware public headers still avoid hard-binding a C++ runtime. |

本仓提供 `SYSTEM_C` / `SYSTEM_CPP` 二选一：系统模块语言可按板级选；**南向栈仍以 C ABI 为主**。
This repo offers a `SYSTEM_C` / `SYSTEM_CPP` choice: the system-module language is selected per board; **the southbound stack remains C-ABI-first**.

### RTOS / OS 选型 / RTOS & OS Selection

| 选项 / Option | 态度 / Stance | 理由 / Rationale |
| :--- | :---: | :--- |
| **FreeRTOS** | 首选内核 / Preferred kernel | **最纯粹**：调度与 IPC 模型清晰，和本仓 OSAL 垫片最贴；ESP-IDF 等自带内核时不要硬塞另一份 FreeRTOS。 / **Pristine**: clear scheduler & IPC model, closest to this repo's OSAL shim; do not force in a second FreeRTOS when the platform (e.g. ESP-IDF) already has one. |
| **RT-Thread** | 可选 / Optional | **组件最丰富**，生态软绑定（包/设备框架可取舍接入）；与本仓设备模型并存时，驱动仍走 mini_tree，勿混两套 probe。 / **Richest components** with soft-bound ecosystem (packages/device framework are optional); when coexisting with this repo's device model, drivers still go through mini_tree — do not mix two probe systems. |
| **裸机 `OSAL_NULL` + xtask** | 小系统默认 / Default for small systems | 无调度器开销；协作式，不适合复杂抢占业务。 / No scheduler overhead; cooperative, unfit for complex preemptive workloads. |
| **Zephyr** | **暂未接入 / Not currently integrated** | 生态成熟、设备树模型完备；但 **dts 生成宏层级深、展开后难以追踪，现场几乎无法调试**——这也是本仓选择 Linux 式设备树 + `dtc-lite`（生成物为普通 C 静态表，可直接断点/看符号）的原因之一。其 dts 与 Linux 设备树理念同源，若需要该模型，直接以 Linux 为参照即可。本仓 OSAL 后端当前为裸机 / FreeRTOS / RT-Thread，Zephyr 暂未纳入集成计划（无 Zephyr 后端(不如rt-thread和直接上linux定位尴尬写法奇怪)）。 / A mature ecosystem with a complete device-tree model; however, **its dts-generated macro layer is deep — hard to trace after expansion and nearly impossible to debug on target**, one reason this repo prefers a plain Linux-style device tree + `dtc-lite` (which emits ordinary C static tables you can breakpoint and inspect). Its dts is rooted in the same idea as Linux's device tree, so if that model is needed, Linux itself is the direct reference. This repo's OSAL backends are currently bare-metal / FreeRTOS / RT-Thread, and Zephyr is not yet on the integration roadmap (no Zephyr backend). |

优先级数值、ISR 进临界区等行为差异见 [osal_switching.md](osal_switching.md)。
See [osal_switching.md](osal_switching.md) for behavioral differences such as priority numbers and ISR critical sections.

### 工具链 / IDE / Toolchain & IDE

| 选项 / Option | 态度 / Stance | 理由 / Rationale |
| :--- | :---: | :--- |
| **Cursor** / **VS Code** + clangd | **推荐 / Recommended** | 与本仓 `compile_flags.txt` / `ide/stubs` 合拍；跳转、补全、诊断顺畅；AI 辅助改中间件效率高。 / Fits this repo's `compile_flags.txt` / `ide/stubs`; smooth navigation, completion, diagnostics; AI-assisted middleware editing is productive. |
| **CLion** | **推荐 / Recommended** | CMake 一等公民；C/C++ 索引与重构强；适合大仓库与 `SYSTEM_CPP`。 / First-class CMake; strong C/C++ indexing and refactoring; good for large repos and `SYSTEM_CPP`. |
| **Qoder** 等现代 AI IDE / Modern AI IDEs | **推荐 / Recommended** | 与 Cursor 同类：以现代语言服务 + AI 集成为中心，跟得上本仓文档/多文件重构节奏。 / Same category as Cursor: centered on modern language servers + AI integration, keeping pace with this repo's doc/multi-file refactor cadence. |
| **Zed** | **推荐（只写代码 / coding only）** | clangd 语言服务 + 快速编辑体验好；无调试/烧录一体化，定位为纯代码编辑器。 / Great clangd-based language service and fast editing; no integrated debug/flash — positioned as a pure code editor. |
| **CMake + Ninja/Make + GCC/Clang** | **推荐 / Recommended** | 构建与生成物（genconfig / dtc-lite）的主路径。 / The main path for building and generated artifacts (genconfig / dtc-lite). |
| **Keil µVision** | **不推荐；已不支持 / Not recommended; unsupported** | 几乎无法干净集成；跳转/C++/AI 弱。降级时最简单是 **自写/自改 Python 生成 `.uvprojx`**（远古有过类似思路，**现已不维护**）。详见 [keil_integration.md](keil_integration.md)。 / Nearly impossible to integrate cleanly; weak navigation/C++/AI. The simplest fallback is a **self-written/modified Python script generating `.uvprojx`** (a legacy idea, **no longer maintained**). See [keil_integration.md](keil_integration.md). |
| **ARMCC v5** | **不支持 / Unsupported** | 过旧，缺现代 C/GNU 扩展。 / Too old; lacks modern C/GNU extensions. |

作者偏好：**用现代编辑器写代码，用 CMake 出固件**；Keil 不作受支持路径。若必须交付 µVision 工程，自备 Python 生成 `.uvprojx`（见 [keil_integration.md](keil_integration.md)），作者不维护该生成器。
Author's preference: **write code in a modern editor, build firmware with CMake**; Keil is not a supported path. If a µVision project must be delivered, bring your own Python generator for `.uvprojx` (see [keil_integration.md](keil_integration.md)) — the author does not maintain it.

### 和本架构的关系（一句话）/ This Architecture in One Sentence

学 Linux / ESP 的 **分层与 VFS 心智**，用 FreeRTOS（或 RT-Thread 组件）做 **调度**，用本仓 dtc-lite 做 **编译期、可裁剪的板级描述**（板级描述走 Linux 式设备树；与 Zephyr 的 dts 宏生成路径取舍见上表）。开发侧则避开 Keil 当主战场，留在 **Cursor / VS Code / CLion / Qoder / Zed（只写代码 / coding only）** 一类现代工具上。
Borrow Linux/ESP's **layering & VFS mindset**, use FreeRTOS (or RT-Thread components) for **scheduling**, and this repo's dtc-lite for **compile-time, trimmable board description** (Linux-style device tree; the trade-off vs Zephyr's dts macrogen path is in the table above). On the dev side, avoid making Keil the main battlefield; stay on modern tools such as **Cursor / VS Code / CLion / Qoder / Zed (coding only)**.

---

## 相关文档 / Related Documents

- [architecture.md](architecture.md) · [roadmap.md](roadmap.md) · [api_compatibility.md](api_compatibility.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) · [osal_switching.md](osal_switching.md) · [keil_integration.md](keil_integration.md)
