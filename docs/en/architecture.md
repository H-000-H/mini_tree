# mini_tree Architecture Overview

> Layering, startup, data flow, and safety model of the current middleware shelf. Not bound to any vendor SDK.

| Item | Content |
| :--- | :--- |
| **Audience** | Engineers modifying the middleware or doing deep porting |
| **Prerequisites** | Have read the terminology in [usage.md](usage.md) |
| **Related** | [design_decisions.md](design_decisions.md) · [file_index.md](file_index.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md) |

---

## Table of Contents

1. [Layering Topology & Contracts](#1-layering-topology-contracts)
2. [Module Responsibilities](#2-module-responsibilities)
3. [Startup Sequence (Two-Phase Ignition)](#3-startup-sequence-two-phase-ignition)
4. [Core Data Flow](#4-core-data-flow)
5. [Configuration & Generated Artifacts](#5-configuration-generated-artifacts)
6. [Interrupt Model](#6-interrupt-model)
7. [Safety Architecture](#7-safety-architecture)
8. [Cross-Platform Boundary](#8-cross-platform-boundary)

---

## 1. Layering Topology & Contracts

Layer topology (top-down calls; horizontal bands are base capabilities):

```
┌─────────────────────────────────────────────────────────────┐
│ Application / 平台业务                                       │
│   device_open/read/write/ioctl · EventBus · osal_task_*     │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ board/   device · driver · bus.h · lifecycle · config_store │
│          device_tree_init · board_driver_probe_all          │
└────────────────────────────┬────────────────────────────────┘
                             │ DRIVER_REGISTER → dtc-lite 表
┌────────────────────────────▼────────────────────────────────┐
│ vfs/     外设 file_operations（持 device 锁 / lifecycle）    │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ bus/     host/client 池 · 引用计数 · 对上层 poison hal_*     │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ hal/     平台无关头 + weak 空 .c                             │
└────────────────────────────┬────────────────────────────────┘
                             │ 仅平台 .c include 厂商头
┌────────────────────────────▼────────────────────────────────┐
│ 厂商 SDK / LL / 标准外设库 / ESP-IDF …（不在本仓库）           │
└─────────────────────────────────────────────────────────────┘
```

Horizontal: core · osal · interrupt · system_c|cpp · time_slice · can_hook · tools

### 1.1 Inter-Layer Contracts

| Boundary | Allowed | Forbidden |
| :--- | :--- | :--- |
| App → board | `device_*`, business ioctl | `hal_*`, vendor headers |
| vfs → bus | `*_bus_*` | direct `hal_*` (header poison) |
| bus → hal | call HAL; `.c` may define `*_BUS_IMPL` | leaking vendor types into public headers |
| HAL headers | `uintptr_t` / integer macro fields | typedefs like `SPI_HandleTypeDef` |

### 1.2 Hardware Pass-Through

In the DTSI, `#include <vendor_header>` → expanded by `cpp` → properties become integers → VFS fills `hal_*_config` → the platform HAL feeds them into the LL/driver. The middleware does **not** maintain a "Linux-style enum → vendor enum" mapping table.

---

## 2. Module Responsibilities

| Directory | Responsibility | Typical Symbols |
| :--- | :--- | :--- |
| `board/` | device instances, probe order, lifecycle, config aggregation | `device_find`, `DRIVER_REGISTER` |
| `vfs/` | drivers bound by compatible | `vfs_spi_probe`, uart/can/usb… |
| `bus/` | controller host + client sessions | `spi_bus_open`, `can_bus_transmit` |
| `hal/` | abstract register operations | `hal_gpio_fast_set_level`, `hal_uart_write` |
| `core/` | error codes, compat macros, events, buffer pools, logging | `VFS_ERR_*`, `event_bus_*` |
| `osal/` | locks, queues, tasks, time | `osal_mutex_lock` |
| `interrupt/` | VIRQ, top/bottom halves | `interrupt_virtual_dispatch` |
| `system_c` / `system_cpp` | startup, WDT, scrubber, safe_state | `mini_tree_pre_os_init` / `mini_tree::system_pre_os_init` |
| `time_slice/` | bare-metal scheduling — cooperative (`xtask_coop.c`, default) and preemptive (`xtask_preempt.c`, experimental) are mutually exclusive, sharing `xtask.h` API; dual-gated by CMake + `#ifdef`; only used under `OSAL_NULL` | `x_scheduler` / `x_task` |
| `drivers/<chip>/` | product drivers (37, `{include,src}` layout) | `DRIVER_REGISTER` / ioctl; dtc-lite compile-time probe |
| `can_hook/` | CAN hook extensions | — |
| `lib/` + `cmake/*.cmake` | vendored: FreeRTOS / RT-Thread / ETL; TinyUSB / lwIP / cJSON are config-time FetchContent, the rest link-time | OSAL kernels per Kconfig; the rest via `mini_tree_link_*` (see [ecosystem.md](ecosystem.md)) |

### 2.1 Peripheral Coverage (Current)

| Capability | HAL | Bus | VFS |
| :---: | :---: | :---: | :---: |
| GPIO | ✓ | — | ✓ |
| SPI / UART / I2C / I2S | ✓ | ✓ | ✓ |
| CAN / USB | ✓ | ✓ | ✓ |
| ADC / DAC / TIM | ✓ | — | ✓ |
| RTC / IWDG / WWDG | ✓ | — | ✓ |
| AMP / storage / platform_safety | ✓ | — | — |

---

## 3. Startup Sequence (Two-Phase Ignition)

### 3.1 C API (`system_c/include/system_init.h`)

| Phase | API | Typical Work |
| :---: | :--- | :--- |
| 1 | `mini_tree_pre_os_init()` | disable global interrupts, EventBus, safe_state, optional WDT, `device_tree_init` |
| — | (optional) business/platform prep | static config, extra registrations |
| 2 | `mini_tree_start_tasks()` | `board_driver_probe_all`, TWDT, Flash Scrubber |
| 3 | `system_init_complete()` | re-enable global interrupts |
| 4 | scheduler or bare-metal loop | `vTaskStartScheduler` / `rt_system_scheduler_start` / `mini_tree_system_loop` |

### 3.2 C++ API (`system_cpp`)

`mini_tree::system_pre_os_init()` / `mini_tree::system_start_tasks()` correspond to phases 1/2 above and still end with `system_init_complete()`; `extern "C"` wrappers `mini_tree_pre_os_init()` / `mini_tree_start_tasks()` are also provided for the C side.

### 3.3 Probe Cooperation

dtc-lite scans `DRIVER_REGISTER` and generates the probe/remove table; `board_driver_probe_all` executes in order:

```text
dtc-lite scans DRIVER_REGISTER
  → generates probe/remove table + board_probe_order()
board_driver_probe_all()
  → takes device in order → calls the matching probe_fn
  → on failure, warns or safe-stops by criticality
```

---

## 4. Core Data Flow

### 4.1 Peripheral I/O

```text
device_read/write/ioctl
  → take device->lock (framework wrapper)
  → ops->read/write/ioctl (vfs)
  → *_bus_* (peripherals with a bus)
  → hal_* (platform implementation)
```

### 4.2 EventBus

Publish/subscribe in `core`; module switch `CONFIG_EVENT_BUS` (off by default, depends on `SYSTEM`); capacity is set by `CONFIG_EVENT_BUS_QUEUE_LEN` and `CONFIG_EVENT_BUS_MAX_SUBSCRIBERS`.

### 4.3 BufferPool / algorithm

`buffer_pool` provides pooled blocks; `algorithm/buffer` provides ring/double buffers and similar structures for reuse by drivers and business logic.

### 4.4 Errors & Pointers

- Return values: `int`, `VFS_OK` or negative `VFS_ERR_*`
- Special pointers: `ERR_PTR` / `IS_ERR` / `PTR_ERR` (rely on `ERR_SECTION_BASE` from `error_symbols.ld`)

---

## 5. Configuration & Generated Artifacts

| Input | Tool | Output Directory (Typical) |
| :--- | :--- | :--- |
| `Kconfig` + `.config` | `tools/genconfig.py` | `generated/kconfig/mini_tree/config.h` |
| `BOARD_DTS` + dtsi + `VENDOR_INC_*` | `tools/dtc-lite.py` | `generated/board/mini_tree/*` |
| scrubber stub | CMake copy | `generated/scrubber/.../system_scrubber_crc_gen.h` |
| root `compile_flags.txt` | `tools/gen_compile_db.py` | `compile_commands.json` (including header entries) |

Kconfig menus: Platform, Multi-core/AMP, OSAL, Spinlock, System Log, System Runtime (`SYSTEM` master switch + C/CPP backend), Components (USB), Board Features (WDT/Scrubber/…), Runtime Capacity (`EVENT_BUS` master switch + capacity), Compiler, Build.

Key CMake cache variables: `BOARD_DTS`, `BOARD_DTSI_DIR`, `VENDOR_INC_DIRS`, `VENDOR_DEFINES`.

---

## 6. Interrupt Model

`interrupt/interrupt.h`:

- **VIRQ**: divided into blocks (system/tim/gpio/adc/uart/spi/i2c/i2s/…), block size `VIRTUAL_IRQ_BLOCK_SIZE` (a power of two).
- **Top half**: `top_half` inside the ISR, can auto-submit the bottom half.
- **Bottom half**: SPSC queue; bare-metal main loop `poll`s, RTOS may use a task + semaphore.
- **Red line**: No printf / long-held locks / unbounded work inside ISRs (see [fast_path.md](fast_path.md)).

---

## 7. Safety Architecture

| Mechanism | Purpose |
| :--- | :--- |
| `safe_state` + `hal_platform_safety` / `hal_amp` | enter safe state, disable outputs |
| `board_safety_register_shutdown` | shutdown callbacks are registered only during probe |
| IWDG / Scrubber (Kconfig) | watchdog and flash inspection |
| `compiler_compat_poison` | restrict malloc/printf/bare mem* |
| `COMPAT_*` memory API | replacements for the poisoned libc symbols |

---

## 8. Cross-Platform Boundary

| This Repository Provides | The Platform Repository Provides |
| :--- | :--- |
| middleware source, weak HAL, placeholder DTS, docs, IDE stubs | `hal_*_<soc>.c`, full dts/dtsi, vendor `-I`, board linker scripts and startup files |
| three OSAL backend skeletons | clocks, heap, SysTick/RTOS ports (if needed) |

On the generic CMake path, the platform injects its board via `MINI_TREE_BOARD_PORT` (absolute path) or a sibling `board_port.cmake`; the ESP-IDF path is triggered by `ESP_PLATFORM` and uses the component mode. The validation matrix lives in each `platform/*/mini_tree` project; this shelf does not bind to a specific SoC.

---

## Related Documents

- [usage.md](usage.md) · [getting_started.md](getting_started.md) · [ecosystem.md](ecosystem.md)
- [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md)
- [design_decisions.md](design_decisions.md) · [api_compatibility.md](api_compatibility.md)
