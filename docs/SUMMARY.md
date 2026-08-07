# mini_tree 文档顶层摘要 / Top-Level Documentation Summary

> 分散文档集中处理后的双语导航。本文是**摘要**而非全文索引：每篇只给核心关键点与优先级，细节见交叉引用链接。
> A consolidated bilingual navigation after de-duplicating scattered docs. This is a **summary**, not the full index: each entry gives key points and priority; details are in the cross-reference links.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 所有人 / Everyone（尤其首次接入者 / especially first-time integrators） |
| **相关 / Related** | [docs/README.md](README.md)（全量索引 / full index） · [README.md](../README.md)（仓库入口 / repo entry） · [overview.html](overview.html)（视觉总览 / visual overview） |

---

## 0. 项目一句话 / One-Line Overview

- **中文**：平台无关的嵌入式中间件，采用 Linux 风格设备树与驱动模型，统一裸机 / FreeRTOS / RT-Thread 上的外设访问；不绑定任何厂商 SDK。
- **English**: Platform-agnostic embedded middleware using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal / FreeRTOS / RT-Thread; zero vendor SDK lock-in.

关键术语保留原文 / Key terms kept verbatim: `Device Tree (DTS/DTSI)`, `DRIVER_REGISTER`, `dtc-lite`, `OSAL (NULL/FREERTOS/RTTHREAD)`, `VFS`, `BUS`, `HAL`, `EventBus`, `VIRQ`.

---

## 1. 按主题分类 / By Topic

### 1.1 入门与概念 / Getting Started & Concepts

| 文档 / Document | 中文标题 / 关键点 | English Title / Key Points | 优先级 / Priority | 链接 / Links |
| :--- | :--- | :--- | :---: | :--- |
| `usage.md` | 术语表 + 阅读路线 | Terminology + reading paths | P2 | [zh](zh/usage.md) · [en](en/usage.md) |
| `getting_started.md` | 依赖（CMake ≥ 3.16、`lark`、`kconfiglib`）、Kconfig 双轨、CMake 集成、两段式点火、clangd | Dependencies, Kconfig dual-track, CMake integration, two-phase boot, clangd | **P0** | [zh](zh/getting_started.md) · [en](en/getting_started.md) |
| `faq.md` | 常见问题（生成物重跑、`lark` 安装等） | FAQ (regenerate artifacts, install `lark`, …) | P2 | [zh](zh/faq.md) · [en](en/faq.md) |
| `architecture.md` | 分层 `app→board→vfs→bus→hal(weak)→vendor`、数据流、启动时序 | Layering, data flow, boot sequence | **P0** | [zh](zh/architecture.md) · [en](en/architecture.md) |
| `ecosystem.md` | 积木型链接：`lib/` 仅 vendor FreeRTOS/RT-Thread/ETL；TinyUSB/lwIP/cJSON 为 **config-time** FetchContent，其余为 link-time；已接入 22 个开源库版本清单 | Brick-style linking: `lib/` vendors only; TinyUSB/lwIP/cJSON are **config-time** FetchContent, rest link-time; 22 OSS libs with versions | **P0** | [zh](zh/ecosystem.md) · [en](en/ecosystem.md) |

### 1.2 平台移植 / Porting

| 文档 / Document | 中文标题 / 关键点 | English Title / Key Points | 优先级 / Priority | 链接 / Links |
| :--- | :--- | :--- | :---: | :--- |
| `porting_guide.md` | 平台移植清单（DTS 覆盖、HAL 强符号、board_port.cmake） | Platform porting checklist | **P0** | [zh](zh/porting_guide.md) · [en](en/porting_guide.md) |
| `esp_idf_cmake.md` | ESP-IDF 组件式 CMake（对照 esp32s3）、Kconfig 双轨、验收清单 | ESP-IDF component CMake, Kconfig dual-track, acceptance checklist | **P0**（ESP 路径） | [zh](zh/esp_idf_cmake.md) · [en](en/esp_idf_cmake.md) |
| `driver_guide.md` | DTS 布局、`dtc-lite` 流水线、`DRIVER_REGISTER`、compatible 属性、`board_*` 运行期 API、remove 生命周期 | DTS layout, `dtc-lite` pipeline, `DRIVER_REGISTER`, compatible props, `board_*` runtime API, remove lifecycle | **P0** | [zh](zh/driver_guide.md) · [en](en/driver_guide.md) |
| `peripherals.md` | 外设 compatible / ioctl 一览 | Peripheral compatible / ioctl overview | P1 | [zh](zh/peripherals.md) · [en](en/peripherals.md) |
| `usb_tusb_port.md` | TinyUSB 板级契约（`usb_tusb_port`） | TinyUSB board-level contract | P1（USB） | [zh](zh/usb_tusb_port.md) · [en](en/usb_tusb_port.md) |
| `amp.md` | 双核 AMP（异构多核） | Dual-core heterogeneous AMP | P2 | [zh](zh/amp.md) · [en](en/amp.md) |
| `osal_switching.md` | OSAL 后端切换（NULL/FREERTOS/RTTHREAD；优先级语义随后端变化） | OSAL backend switching (priority semantics vary by backend) | P1 | [zh](zh/osal_switching.md) · [en](en/osal_switching.md) |
| `esp_idf_notes.md` | ESP 修复记录 + ESP 特殊性 + 依赖策略 | ESP fixes log + ESP specifics + dep strategy | P1（ESP） | [zh](zh/esp_idf_notes.md) · [en](en/esp_idf_notes.md) |

### 1.3 应用编写与编码 / Application & Coding

| 文档 / Document | 中文标题 / 关键点 | English Title / Key Points | 优先级 / Priority | 链接 / Links |
| :--- | :--- | :--- | :---: | :--- |
| `service_spec.md` | 应用层允许/禁止；`device_find` 返回 `ERR_PTR` 须用 `IS_ERR` 判错；两段式启动挂载 | App-layer do's/don'ts; `device_find` returns `ERR_PTR` → use `IS_ERR`; two-phase boot | **P0** | [zh](zh/service_spec.md) · [en](en/service_spec.md) |
| `app_cpp_guide.md` | 应用层 C++ 限制（ETL 容器、编码分档、禁则） | Upper-layer C++ restrictions (ETL containers, tiers, forbidden) | P1（C++） | [zh](zh/app_cpp_guide.md) · [en](en/app_cpp_guide.md) |
| `coding_style.md` | `.clang-format`（LLVM/Allman/单语句去括号/`PointerAlignment: Left`/100 列）+ 分层 `.clang-tidy` + `compiler_compat_poison.h`（默认生效，靠 `ALLOW_*` 豁免） | `.clang-format` (LLVM/Allman/RemoveBracesLLVM/Left pointer/100 cols) + layered `.clang-tidy` + `compiler_compat_poison.h` (on by default, `ALLOW_*` opt-out) | **P0** | [zh](zh/coding_style.md) · [en](en/coding_style.md) |
| `runtime_services.md` | EventBus / VIRQ / SYSTEM_C·CPP / BufferPool | EventBus / VIRQ / SYSTEM_C·CPP / BufferPool | P1 | [zh](zh/runtime_services.md) · [en](en/runtime_services.md) |
| `fast_path.md` | ISR / 热路径红线（禁 printf/mutex/malloc/长逻辑） | ISR / hot-path red lines (no printf/mutex/malloc/heavy logic) | **P0**（驱动） | [zh](zh/fast_path.md) · [en](en/fast_path.md) |
| `can_hook.md` | CAN 协议超集钩子 | CAN protocol superset hooks | P2 | [zh](zh/can_hook.md) · [en](en/can_hook.md) |
| `memory_footprint.md` | 内存/flash 基准（flash 合计；与 CHANGELOG 的 RAM 下限口径不同）+ 裁剪开关 | Memory/flash baseline (flash total; different metric from CHANGELOG's RAM floor) + trimming knobs | P2 | [zh](zh/memory_footprint.md) · [en](en/memory_footprint.md) |
| `api_compatibility.md` | API 稳定面（稳定性承诺） | API stability surface | P2 | [zh](zh/api_compatibility.md) · [en](en/api_compatibility.md) |

### 1.4 调试、设计与历史 / Debug, Design & History

| 文档 / Document | 中文标题 / 关键点 | English Title / Key Points | 优先级 / Priority | 链接 / Links |
| :--- | :--- | :--- | :---: | :--- |
| `debug_monitor.md` | 日志（`SYS_LOG*`/`DRV_LOG*`）、生成物、`compile_commands.json`、clangd | Logging, generated artifacts, clangd | P1 | [zh](zh/debug_monitor.md) · [en](en/debug_monitor.md) |
| `keil_integration.md` | Keil Studio 支持 / 经典 µVision 不推荐 | Keil Studio supported / classic µVision not recommended | P2（IDE） | [zh](zh/keil_integration.md) · [en](en/keil_integration.md) |
| `design_decisions.md` | 仍生效的设计决策与作者偏好 | Design decisions still in force | P1 | [zh](zh/design_decisions.md) · [en](en/design_decisions.md) |
| `references.md` | 外部对照（ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt） | External references | P2 | [zh](zh/references.md) · [en](en/references.md) |
| `problem_summary.md` | 历史问题轴 | Historical problem timeline | P2 | [zh](zh/problem_summary.md) · [en](en/problem_summary.md) |

### 1.5 规划、索引与工具链 / Planning, Index & Toolchain

| 文档 / Document | 中文标题 / 关键点 | English Title / Key Points | 优先级 / Priority | 链接 / Links |
| :--- | :--- | :--- | :---: | :--- |
| `file_index.md` | 源码导航（目录→模块映射） | Source navigation | P1 | [zh](zh/file_index.md) · [en](en/file_index.md) |
| `roadmap.md` | 路线图 | Roadmap | P2 | [zh](zh/roadmap.md) · [en](en/roadmap.md) |
| `todolist.md` | 待办 | TODO list | P2 | [zh](zh/todolist.md) · [en](en/todolist.md) |
| `tools_guide.md` | `dtc-lite` / `genconfig` / `menuconfig` / `gen_compile_db` / scrubber stub 用法；`dtc-lite` 支持 `-I`/`-D` | `dtc-lite`/`genconfig`/`menuconfig`/`gen_compile_db`/scrubber stub; `dtc-lite` supports `-I`/`-D` | **P0**（工具链） | [zh](zh/tools_guide.md) · [en](en/tools_guide.md) |
| `board_devicetree.md` | 设备树编译流水线（`pip install lark`） | Device Tree pipeline (`pip install lark`) | P1 | [zh](zh/board_devicetree.md) · [en](en/board_devicetree.md) |
| `board_linux_vs_device_model.md` | 通用机制：mini_tree 设备模型 vs Linux 对照 | generic: mini_tree device model vs Linux comparison | P2 | [zh](zh/board_linux_vs_device_model.md) · [en](en/board_linux_vs_device_model.md) |

---

## 2. 按优先级速查 / Quick Index by Priority

- **P0（必须读 / Must-read）**：`getting_started.md` · `architecture.md` · `ecosystem.md` · `porting_guide.md` · `esp_idf_cmake.md`（ESP）· `driver_guide.md` · `service_spec.md` · `coding_style.md` · `fast_path.md` · `tools_guide.md`
- **P1（按需 / As-needed）**：`peripherals.md` · `usb_tusb_port.md` · `osal_switching.md` · `app_cpp_guide.md` · `runtime_services.md` · `debug_monitor.md` · `design_decisions.md` · `file_index.md` · `board_devicetree.md`
- **P2（深入 / Deep-dive）**：`usage.md` · `faq.md` · `amp.md` · `can_hook.md` · `memory_footprint.md` · `api_compatibility.md` · `keil_integration.md` · `references.md` · `problem_summary.md` · `roadmap.md` · `todolist.md` · `board_linux_vs_device_model.md`

---

## 3. 关键事实速记 / Key Facts at a Glance

| 主题 / Topic | 中文 / English |
| :--- | :--- |
| 产品驱动 / Product drivers | 37 个，在 `drivers/<chip>/{include,src}`，GLOB 扫描；唯一树外例外 `driver_ws2812`（WHOLE_ARCHIVE） |
| OSAL 后端 / OSAL backends | `CONFIG_OSAL_NULL`（裸机协作，默认）/ `FREERTOS`（v11.3.0）/ `RTTHREAD`（v5.3.0） |
| 目标架构 / Targets | Cortex-M0/M0+/M3/M4F/M7 · RISC-V 32-bit · 双核 AMP |
| 外设覆盖 / Peripheral coverage | 总线层 6（SPI/I2C/I2S/UART/CAN/USB）· 无总线层 7（GPIO/ADC/DAC/TIM/RTC/IWDG/WWDG）· HAL-Only：AMP/Storage/Platform Safety/**SDIO（预留 reserved）** |
| 错误码 / Error codes | `VFS_OK=0`；`VFS_ERR_*`（全名，见 `status.h`）；`device_find` 失败返回 `ERR_PTR` 而非 `NULL` |
| 构建 / Build | CMake ≥ 3.16；`lark`（dtc-lite）；内置 `kconfiglib` 14.1.0；`ETL` 随仓 vendor 且默认链入 |

---

## 4. 文档写作约定 / Documentation Conventions

- 每篇专题含：标题+摘要、读者/前置、目录（长文）、正文（表格与命令优先）、相关文档链接。
- 路径与符号用反引号；错误码写 `VFS_ERR_*` 全名；技术术语保留英文原文（如 `Device Tree`、`OSAL`、`VFS`）。
- 新文档放 `docs/zh/` 与 `docs/en/` 双语；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与法律文件。
- Paths and symbols in back-ticks; error codes as full `VFS_ERR_*`; technical terms kept verbatim (e.g. `Device Tree`, `OSAL`, `VFS`).
- New docs go into `docs/zh/` and `docs/en/` bilingually; the root keeps only `README` / `CHANGELOG` / `CONTRIBUTING` and legal files.

---

## 相关文档 / Related Documents

- [docs/README.md](README.md)（全量索引 / full index） · [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md) · [overview.html](overview.html)
