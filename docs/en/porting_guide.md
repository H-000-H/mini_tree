# Porting Guide

> Steps to bring the middleware onto a new SoC / board / RTOS. Involves: board `dtsi`, HAL backend, OSAL backend, CMake injection. Core conventions: see [architecture.md](architecture.md) and [design_decisions.md](design_decisions.md).

| Item | Content |
| :--- | :--- |
| **Audience** | Porting to a new platform |
| **Related** | [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [getting_started.md](getting_started.md) |

---

## 1. Overview

1. Create a board directory and place the `dtsi` (set `BOARD_DTSI_DIR`).
2. Write / reuse a HAL backend (weak empties without a board).
3. Pick an OSAL backend (`CONFIG_OSAL_NULL` / `FREERTOS` / `RTTHREAD`).
4. Platform CMake injects `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR`.
5. Run `genconfig` + `dtc-lite` to validate.
6. Build the `mini_tree` library and wire the platform linker script (incl. `ERR_SECTION_BASE`).

---

## 2. Board Device Tree

The board `dtsi/` supplies node templates for cpus / soc / gpio / uart (see `board/dtsi/example-soc.dtsi`). The platform overrides via `BOARD_DTSI_DIR`; a fully custom `BOARD_DTS` is also possible. It builds without board nodes (placeholder DTS).

| Item | Injection |
| :--- | :--- |
| `BOARD_DTSI_DIR` | CMake var, points to board `dtsi/` |
| `BOARD_DTS` | CMake var, points to the full board `board.dts` |
| `MINI_TREE_BOARD_PORT` | CMake var, points to the board port directory |

---

## 3. HAL Backend

Without a board, HAL ships weak empty implementations (`hal/hal_if_dummy.c`) — it builds but does nothing; to drive real hardware, implement the corresponding functions in `hal_<name>.c`. Multicore platforms also need `hal/amp`.

---

## 4. OSAL Backend

| Backend | Kconfig | Description |
| :--- | :--- | :--- |
| Bare metal | `CONFIG_OSAL_NULL` | one of three backends; with `time_slice` cooperative/preemptive scheduler |
| FreeRTOS | `CONFIG_OSAL_FREERTOS` | prebuilt / link-time integration |
| RT-Thread | `CONFIG_OSAL_RTTHREAD` | prebuilt / link-time integration |

The bare-metal C++ task wrapper is in `osal/src/osal_task.cpp` (`CONFIG_OSAL_NULL_TASK_CPP`).

---

## 5. CMake Injection

The platform project injects via these variables (see [getting_started.md](getting_started.md) §2):

| Variable | Role |
| :--- | :--- |
| `MINI_TREE_BOARD_PORT` | board port directory |
| `BOARD_DTS` | full board DTS path |
| `BOARD_DTSI_DIR` | board dtsi directory |
| `CMAKE_SOURCE_DIR` | platform project root (for `add_subdirectory(mini_tree)`) |

> Build is generic CMake: HAL ships weak empties, the board is injected via the variables above; an ESP-IDF component path (`cmake/esp_idf.cmake`) is also provided.

---

## 6. Validation

1. `genconfig` emits `config.h`; confirm `CONFIG_*` matches `.config`.
2. dtc-lite compile-time probe hits (driver registration).
3. Build the `mini_tree` static lib with no undefined symbols.
4. Wire the platform linker script; confirm the `ERR_SECTION_BASE` section is placed correctly.

---

## Related Docs

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [getting_started.md](getting_started.md) · [keil_integration.md](keil_integration.md)
