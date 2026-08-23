# Top-Level Documentation Summary

> A consolidated bilingual navigation after de-duplicating scattered docs. This is a **summary**, not the full index: each entry gives key points and priority; details are in the cross-reference links.

| Item | Content |
| :--- | :--- |
| **Audience** | Everyone (especially first-time integrators) |
| **Related** | [docs/en/README.md](README.md) (full index) · [README.md](../README.md) (repo entry) |

---

## 0. One-Line Overview

- **English**: Platform-agnostic embedded middleware using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal / FreeRTOS / RT-Thread; zero vendor SDK lock-in.

Key terms kept verbatim: `Device Tree (DTS/DTSI)`, `DRIVER_REGISTER`, `dtc-lite`, `OSAL (NULL/FREERTOS/RTTHREAD)`, `VFS`, `BUS`, `HAL`, `EventBus`, `VIRQ`.

---

## 1. By Topic

### 1.1 Getting Started & Concepts

| Document | English Title / Key Points | Priority | Links |
| :--- | :--- | :---: | :--- |
| `usage.md` | Terminology + reading paths | P2 | [en](usage.md) |
| `getting_started.md` | Dependencies (CMake ≥ 3.16, `lark`, `kconfiglib`), Kconfig dual-track, CMake integration, two-phase boot, clangd | **P0** | [en](getting_started.md) |
| `faq.md` | FAQ (regenerate artifacts, install `lark`, …) | P2 | [en](faq.md) |
| `architecture.md` | Layering `app→board→vfs→bus→hal(weak)→vendor`, data flow, boot sequence | **P0** | [en](architecture.md) |
| `patterns.md` | Key mechanisms anatomy: pre_execution chain / two-phase boot / compile-time probe table / xtask scheduling / VIRQ top-bottom halves / SPSC lock-free channel / dev_lifecycle / non-blocking state machines | P1 | [en](patterns.md) |
| `ecosystem.md` | Brick-style linking: `lib/` vendors only; TinyUSB/lwIP are **config-time** FetchContent, rest link-time; OSS libs with versions | **P0** | [en](ecosystem.md) |

### 1.2 Porting

| Document | English Title / Key Points | Priority | Links |
| :--- | :--- | :---: | :--- |
| `device_tree_porting.md` | **Device-tree porting guide (consolidated, full examples)**: dtc-lite pipeline, node templates, board DTS, generated-artifacts, driver wiring, CMake injection (`BOARD_DTS`/`BOARD_DTSI_DIR`/`MINI_TREE_BOARD_PORT`), verification, troubleshooting. In practice porting usually needs only DTS + HAL changes | **P0** | [en](device_tree_porting.md) |
| `esp_idf_cmake.md` | ESP-IDF component CMake — **moved to the `esp` branch** (full guide: `docs/en/esp_idf_cmake.md` there) | **P0** (ESP path, on `esp` branch) | [en](esp_idf_cmake.md) |
| `driver_guide.md` | DTS layout, `dtc-lite` pipeline, `DRIVER_REGISTER`, compatible props, `board_*` runtime API, remove lifecycle | **P0** | [en](driver_guide.md) |
| `peripherals.md` | Peripheral compatible / ioctl overview | P1 | [en](peripherals.md) |
| `usb_tusb_port.md` | TinyUSB board-level contract (`usb_tusb_port`) | P1 (USB) | [en](usb_tusb_port.md) |
| `amp.md` | Dual-core heterogeneous AMP | P2 | [en](amp.md) |
| `osal_switching.md` | OSAL backend switching (NULL/FREERTOS/RTTHREAD; priority semantics vary by backend) | P1 | [en](osal_switching.md) |
| `net.md` | Network protocol stack glue: coreMQTT v5 thin wrapper / TCP / transport adapter / PPP·USB NIC / `NET_*` error codes | P1 (network) | [en](net.md) |
| `esp_idf_notes.md` | ESP fixes log + specifics + dependency strategy — **moved to the `esp` branch** | P1 (ESP, on `esp` branch) | [en](esp_idf_notes.md) |

### 1.3 Application & Coding

| Document | English Title / Key Points | Priority | Links |
| :--- | :--- | :---: | :--- |
| `service_spec.md` | App-layer do's/don'ts; `device_find` returns `ERR_PTR` → use `IS_ERR`; two-phase boot | **P0** | [en](service_spec.md) |
| `app_cpp_guide.md` | Upper-layer C++ restrictions (ETL containers, tiers, forbidden) | P1 (C++) | [en](app_cpp_guide.md) |
| `coding_style.md` | `.clang-format` (LLVM/Allman/RemoveBracesLLVM/Left pointer/200 cols) + layered `.clang-tidy` + `compiler_compat_poison.h` (on by default, `ALLOW_*` opt-out) | **P0** | [en](coding_style.md) |
| `runtime_services.md` | EventBus / VIRQ / SYSTEM_C·CPP / BufferPool | P1 | [en](runtime_services.md) |
| `fast_path.md` | ISR / hot-path red lines (no printf/mutex/malloc/heavy logic) | **P0** (drivers) | [en](fast_path.md) |
| `can_hook.md` | CAN protocol superset hooks | P2 | [en](can_hook.md) |
| `memory_footprint.md` | Memory/flash baseline (flash total; different metric from CHANGELOG's RAM floor) + trimming knobs | P2 | [en](memory_footprint.md) |
| `api_compatibility.md` | API stability surface (stability promise) | P2 | [en](api_compatibility.md) |

### 1.4 Debug, Design & History

| Document | English Title / Key Points | Priority | Links |
| :--- | :--- | :---: | :--- |
| `debug_monitor.md` | Logging (`SYS_LOG*`/`DRV_LOG*`), generated artifacts, `compile_commands.json`, clangd | P1 | [en](debug_monitor.md) |
| `keil_integration.md` | Keil Studio supported / classic µVision not recommended | P2 (IDE) | [en](keil_integration.md) |
| `design_decisions.md` | Design decisions still in force | P1 | [en](design_decisions.md) |
| `references.md` | External references (ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt) | P2 | [en](references.md) |
| `problem_summary.md` | Historical problem timeline | P2 | [en](problem_summary.md) |

### 1.5 Planning, Index & Toolchain

| Document | English Title / Key Points | Priority | Links |
| :--- | :--- | :---: | :--- |
| `file_index.md` | Source navigation (directory → module map) | P1 | [en](file_index.md) |
| `roadmap.md` | Roadmap | P2 | [en](roadmap.md) |
| `todolist.md` | TODO list | P2 | [en](todolist.md) |
| `tools_guide.md` | `dtc-lite` / `genconfig` / `menuconfig` / `gen_compile_db` / scrubber stub usage; `dtc-lite` supports `-I`/`-D` | **P0** (toolchain) | [en](tools_guide.md) |
| `board_linux_vs_device_model.md` | generic: mini_tree device model vs Linux comparison | P2 | [en](board_linux_vs_device_model.md) |

---

## 2. Quick Index by Priority

- **P0 (Must-read)**: `getting_started.md` · `architecture.md` · `ecosystem.md` · `device_tree_porting.md` · `esp_idf_cmake.md` (ESP, **on `esp` branch**) · `driver_guide.md` · `service_spec.md` · `coding_style.md` · `fast_path.md` · `tools_guide.md`
- **P1 (As-needed)**: `patterns.md` · `peripherals.md` · `usb_tusb_port.md` · `osal_switching.md` · `app_cpp_guide.md` · `runtime_services.md` · `debug_monitor.md` · `design_decisions.md` · `file_index.md`
- **P2 (Deep-dive)**: `usage.md` · `faq.md` · `amp.md` · `can_hook.md` · `memory_footprint.md` · `api_compatibility.md` · `keil_integration.md` · `references.md` · `problem_summary.md` · `roadmap.md` · `todolist.md` · `board_linux_vs_device_model.md`

---

## 3. Key Facts at a Glance

| Topic | English |
| :--- | :--- |
| Product drivers | 39, in `drivers/<chip>/{include,src}`, GLOB-scanned |
| OSAL backends | `CONFIG_OSAL_NULL` (bare-metal, default) / `FREERTOS` (v11.3.0) / `RTTHREAD` (v5.3.0) |
| Targets | Cortex-M0/M0+/M3/M4F/M7 · RISC-V 32-bit · dual-core AMP |
| Peripheral coverage | Bus-based 6 (SPI/I2C/I2S/UART/CAN/USB) · Bus-less 7 (GPIO/ADC/DAC/TIM/RTC/IWDG/WWDG) · HAL-Only: AMP/Storage/Platform Safety/**SDIO (reserved)** |
| Error codes | `MINI_OK=0`; `VFS_ERR_*` (full name, see `status.h`); `device_find` failure returns `ERR_PTR` not `NULL` |
| Build | CMake ≥ 3.16; `lark` (dtc-lite); vendored `kconfiglib` 14.1.0; `ETL` vendored and linked by default |

---

## 4. Documentation Conventions

- Each topic document includes: title + summary, audience/prerequisites, table of contents (for long docs), body (tables and commands first), related-document links.
- Paths and symbols in back-ticks; error codes as full `VFS_ERR_*`; technical terms kept verbatim (e.g. `Device Tree`, `OSAL`, `VFS`).
- New docs go into `docs/cn/` and `docs/en/` bilingually; the root keeps only `README` / `CHANGELOG` / `CONTRIBUTING` and legal files.

---

## Related Documents

- [docs/en/README.md](README.md) (full index) · [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md)
