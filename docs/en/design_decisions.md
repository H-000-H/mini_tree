# mini_tree — Architecture Design Decisions

> A summary of the design decisions still in effect for the middleware shelf.
>
> Third-party attribution and licenses live in the root [NOTICE](../NOTICE) (Apache convention); this file is not a legal NOTICE.

| Item | Content |
| :--- | :--- |
| **Audience** | Anyone who needs to understand "why it is layered this way" |
| **Related** | [architecture.md](architecture.md) · [references.md](references.md) · [ecosystem.md](ecosystem.md) · [CHANGELOG.md](../CHANGELOG.md) · [NOTICE](../NOTICE) |

---

## Core Architecture Decisions

| # | Decision | Consequence |
| :---: | :--- | :--- |
| 1 | Decouple the middleware from vendor SDKs | Neutral HAL headers + weak `.c`; platform strong symbols + DTS |
| 2 | Hardware direct-inject | DTSI macros into structs; no enum mapping layer |
| 3 | Compile-time probe | `DRIVER_REGISTER` + dtc-lite static tables |
| 4 | Enforced bus isolation | `#pragma GCC poison` blocks upper layers from calling HAL directly |
| 5 | OSAL triple-backend | FreeRTOS / RT-Thread / NULL, trimmed by Kconfig |
| 6 | Dual system backends | compile-time choice of `SYSTEM_C` / `SYSTEM_CPP` |
| 7 | Unified error codes | `status.h`'s `VFS_ERR_*` / `OSAL_ERR_*` |

---

## Safety-Related Decisions

- `compiler_compat_poison`: restrict heap, stdio, and bare `mem*`
- `safe_state` + optional WDT / Flash Scrubber
- `board_safety_register_shutdown` at probe time
- `ERR_PTR` + `error_symbols.ld`

---

## Relationship with Platform Repos

| This repo | Platform repo (e.g. Heterogeneous-Multicore) |
| :--- | :--- |
| Generic middleware, docs, IDE stubs, placeholder DTS | SoC HAL, full dtsi, vendor `-I`, board bring-up & validation |

---

## Known Limitations

1. Without a vendor SDK, this repo can only do static analysis / empty-stub linking; peripherals cannot be validated standalone.
2. OSAL priority number semantics vary with the backend.
3. USB depends on the board-level `usb_tusb_port`; the middleware does not embed any MCU-specific TinyUSB port.
4. The default `board.dts` has no real peripheral nodes.

---

## Author Preferences & Trade-offs

> **Not hard rules.** When in conflict with [service_spec.md](service_spec.md) / [fast_path.md](fast_path.md), the hard specs win.
>
> See [references.md](references.md) for external comparisons.

### Language Layering

| Layer | Preference | Trade-off |
| :--- | :--- | :--- |
| **Below the app layer** (HAL / Bus / VFS / board / core / OSAL) | **C** by default; capable teams may use **Rust** | C has the cleanest link surface and fits vendor SDKs / weak symbols best. Rust suits teams with crisp boundaries willing to maintain FFI; it is not this repo's default path. |
| **App layer and above** (business services, UI, policy, toolchain side) | **C++** or **Rust** | Expressiveness and type constraints matter more; C++ can plug into this repo's `SYSTEM_CPP` / ETL path. Middleware public headers still avoid hard-binding a C++ runtime. |

This repo offers a `SYSTEM_C` / `SYSTEM_CPP` choice: the system-module language is selected per board; **the southbound stack remains C-ABI-first**.

### RTOS & OS Selection

| Option | Stance | Rationale |
| :--- | :---: | :--- |
| **FreeRTOS** | Preferred kernel | **Pristine**: clear scheduler & IPC model, closest to this repo's OSAL shim; do not force in a second FreeRTOS when the platform (e.g. ESP-IDF) already has one. |
| **RT-Thread** | Optional | **Richest components** with soft-bound ecosystem (packages/device framework are optional); when coexisting with this repo's device model, drivers still go through mini_tree — do not mix two probe systems. |
| **Bare-metal `OSAL_NULL` + xtask** | Default for small systems | No scheduler overhead; cooperative, unfit for complex preemptive workloads. |
| **Zephyr** | **Not currently integrated** | A mature ecosystem with a complete device-tree model; however, **its dts-generated macro layer is deep — hard to trace after expansion and nearly impossible to debug on target**, one reason this repo prefers a plain Linux-style device tree + `dtc-lite` (which emits ordinary C static tables you can breakpoint and inspect). Its dts is rooted in the same idea as Linux's device tree, so if that model is needed, Linux itself is the direct reference. This repo's OSAL backends are currently bare-metal / FreeRTOS / RT-Thread, and Zephyr is not yet on the integration roadmap (no Zephyr backend). |

See [osal_switching.md](osal_switching.md) for behavioral differences such as priority numbers and ISR critical sections.

### Toolchain & IDE

| Option | Stance | Rationale |
| :--- | :---: | :--- |
| **Cursor** / **VS Code** + clangd | **Recommended** | Fits this repo's `compile_flags.txt` / `ide/stubs`; smooth navigation, completion, diagnostics; AI-assisted middleware editing is productive. |
| **CLion** | **Recommended** | First-class CMake; strong C/C++ indexing and refactoring; good for large repos and `SYSTEM_CPP`. |
| **Qoder** and other modern AI IDEs | **Recommended** | Same category as Cursor: centered on modern language servers + AI integration, keeping pace with this repo's doc/multi-file refactor cadence. |
| **Zed** | **Recommended (coding only)** | Great clangd-based language service and fast editing; no integrated debug/flash — positioned as a pure code editor. |
| **CMake + Ninja/Make + GCC/Clang** | **Recommended** | The main path for building and generated artifacts (dtc-lite / ESP-IDF). |
| **VS Code / Cursor / Qoder** etc. | **Recommended** | VSCode-based editors/IDEs, with clangd and the ESP-IDF extension. |
| **Classic Keil 5 / µVision** | **Not provided, not followed up** | This branch is an ESP component; the main path is VSCode-based + ESP-IDF. Non-VSCode platforms are not the main battlefield. |

My habit: **write code in a modern editor, build firmware with CMake / ESP-IDF**; this branch's main path is the **VSCode family (VS Code / Cursor / Qoder) + clangd + ESP-IDF** and does not follow up on traditional Keil or other non-VSCode platforms.

### This Architecture in One Sentence

Borrow Linux/ESP's **layering & VFS mindset**, use FreeRTOS for **scheduling**, and this repo's dtc-lite for **compile-time, trimmable board description** (Linux-style device tree; the trade-off vs Zephyr's dts macrogen path is in the table above). On the dev side, stay on modern tools such as **VS Code / Cursor / Qoder**.

---

## Related Documents

- [architecture.md](architecture.md) · [roadmap.md](roadmap.md) · [api_compatibility.md](api_compatibility.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) · [osal_switching.md](osal_switching.md)
