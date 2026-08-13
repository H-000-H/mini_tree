# mini_tree — 架构设计决策

> 当前中间件 shelf 仍生效的设计决策摘要。
>
> 第三方归属与许可证清单见根目录 [NOTICE](../NOTICE)（Apache 惯例）；本文件不是法律 NOTICE。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 需要理解「为什么这样分层」的人 |
| **相关** | [architecture.md](architecture.md) · [references.md](references.md) · [ecosystem.md](ecosystem.md) · [CHANGELOG.md](../CHANGELOG.md) · [NOTICE](../NOTICE) |

---

## 核心架构决策

| # | 决策 | 后果 |
| :---: | :--- | :--- |
| 1 | 中间件与厂商 SDK 解耦 | HAL 头中立 + weak `.c`；平台强符号 + DTS |
| 2 | 硬件直投 | DTSI 宏进结构体，无 enum 映射层 |
| 3 | 编译期 probe | `DRIVER_REGISTER` + dtc-lite 静态表 |
| 4 | Bus 强制隔离 | `#pragma GCC poison` 防上层直调 HAL |
| 5 | OSAL 三栖 | FreeRTOS / RT-Thread / NULL，Kconfig 裁剪 |
| 6 | 双系统后端 | `SYSTEM_C` / `SYSTEM_CPP` 编译期二选一 |
| 7 | 统一错误码 | `status.h` 的 `VFS_ERR_*` / `OSAL_ERR_*` |

---

## 安全相关决策

- `compiler_compat_poison`：限制堆、stdio、裸 `mem*`
- `safe_state` + 可选 WDT / Flash Scrubber
- probe 期 `board_safety_register_shutdown`
- `ERR_PTR` + `error_symbols.ld`

---

## 与平台仓库的关系

| 本仓库 | 平台仓库（如 Heterogeneous-Multicore） |
| :--- | :--- |
| 通用中间件、文档、ide stubs、占位 DTS | SoC HAL、完整 dtsi、厂商 `-I`、板级启动与验证 |

---

## 已知限制

1. 无厂商 SDK 时本仓只能做静态分析 / 空 stub 链接，不能单独验证外设。
2. OSAL 优先级数值语义随后端变化。
3. USB 依赖板级 `usb_tusb_port`；中间件不内嵌具体 MCU 的 TinyUSB port。
4. 默认 `board.dts` 无真实外设节点。

---

## 作者偏好与取舍

> **不是硬规范**。与 [service_spec.md](service_spec.md) / [fast_path.md](fast_path.md) 冲突时，以硬规范为准。
>
> 选型对照与外部项目见 [references.md](references.md)。

### 语言分层

| 层次 | 偏好 | 取舍说明 |
| :--- | :--- | :--- |
| **应用层以下**（HAL / Bus / VFS / board / core / OSAL）| 默认 **全 C**；有能力团队可上 **Rust** | C 链接面最干净、与厂商 SDK / 弱符号最合拍。Rust 适合边界清晰、愿维护 FFI 的团队，不作为本仓默认路径。 |
| **应用层及以上**（业务服务、UI、策略、工具链侧）| **C++** 或 **Rust** | 表达力与类型约束更重要；C++ 可与本仓 `SYSTEM_CPP` / ETL 路径衔接。中间件公共头仍避免强绑 C++ 运行时。 |

本仓提供 `SYSTEM_C` / `SYSTEM_CPP` 二选一：系统模块语言可按板级选；**南向栈仍以 C ABI 为主**。

### RTOS / OS 选型

| 选项 | 态度 | 理由 |
| :--- | :---: | :--- |
| **FreeRTOS** | 首选内核 | **最纯粹**：调度与 IPC 模型清晰，和本仓 OSAL 垫片最贴；ESP-IDF 等自带内核时不要硬塞另一份 FreeRTOS。 |
| **RT-Thread** | 可选 | **组件最丰富**，生态软绑定（包/设备框架可取舍接入）；与本仓设备模型并存时，驱动仍走 mini_tree，勿混两套 probe。 |
| **裸机 `OSAL_NULL` + xtask** | 小系统默认 | 无调度器开销；协作式（默认）或 N+1 多优先级抢占式（`XTASK_PREEMPT`，可延迟/休眠/抢占，无就绪时精确 WFI），由 Kconfig 三态 choice 选择；无独立任务栈（run-to-completion，复用主循环栈）。 |
| **Zephyr** | **暂未接入** | 生态成熟、设备树模型完备；但 **dts 生成宏层级深、展开后难以追踪，现场几乎无法调试**——这也是本仓选择 Linux 式设备树 + `dtc-lite`（生成物为普通 C 静态表，可直接断点/看符号）的原因之一。其 dts 与 Linux 设备树理念同源，若需要该模型，直接以 Linux 为参照即可。本仓 OSAL 后端当前为裸机 / FreeRTOS / RT-Thread，Zephyr 暂未纳入集成计划（无 Zephyr 后端）。 |

优先级数值、ISR 进临界区等行为差异见 [osal_switching.md](osal_switching.md)。

### 工具链 / IDE

| 选项 | 态度 | 理由 |
| :--- | :---: | :--- |
| **Cursor** / **VS Code** + clangd | **推荐** | 与本仓 `compile_flags.txt` / `ide/stubs` 合拍；跳转、补全、诊断顺畅；AI 辅助改中间件效率高。 |
| **CLion** | **推荐** | CMake 一等公民；C/C++ 索引与重构强；适合大仓库与 `SYSTEM_CPP`。 |
| **Qoder** 等现代 AI IDE | **推荐** | 与 Cursor 同类：以现代语言服务 + AI 集成为中心，跟得上本仓文档/多文件重构节奏。 |
| **Zed** | **推荐（只写代码）** | clangd 语言服务 + 快速编辑体验好；无调试/烧录一体化，定位为纯代码编辑器。 |
| **CMake + Ninja/Make + GCC/Clang** | **推荐** | 构建与生成物（dtc-lite / ESP-IDF）的主路径。 |
| **VS Code / Cursor / Qoder** 等 | **推荐** | 基于 VSCode 的编辑器/IDE，配 clangd 与 ESP-IDF 扩展。 |
| **传统 Keil 5 / µVision** | **不提供、不跟进** | 本分支为 ESP 组件，主路径是 VSCode 系 + ESP-IDF；非 VSCode 平台不作为主战场。 |

我的习惯：**用现代编辑器写代码，用 CMake / ESP-IDF 出固件**；本分支主路径是 **VSCode 系（VS Code / Cursor / Qoder）+ clangd + ESP-IDF**，不跟进传统 Keil 等非 VSCode 平台。

### 和本架构的关系（一句话）

学 Linux / ESP 的 **分层与 VFS 心智**，用 FreeRTOS 做 **调度**，用本仓 dtc-lite 做 **编译期、可裁剪的板级描述**（板级描述走 Linux 式设备树；与 Zephyr 的 dts 宏生成路径取舍见上表）。开发侧留在 **VS Code / Cursor / Qoder** 一类现代工具上。

---

## 相关文档

- [architecture.md](architecture.md) · [roadmap.md](roadmap.md) · [api_compatibility.md](api_compatibility.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) · [osal_switching.md](osal_switching.md)
