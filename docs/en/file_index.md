# File Index

> Middleware source navigation (excluding the `lib/**` brick tree). Paths are relative to the repo root. Optional bricks may live only in the Fetch cache — see [ecosystem.md](ecosystem.md).

| Item | Content |
| :--- | :--- |
| **Audience** | Locating files / code review |
| **Related** | [architecture.md](architecture.md) · [design_decisions.md](design_decisions.md) |

---

## Top Level

| Path | Description |
| :--- | :--- |
| `CMakeLists.txt` | ESP component entry (routes to `cmake/esp_idf.cmake`) |
| `Kconfig.projbuild` / `Kconfig.mini_tree` | ESP-IDF Kconfig (`idf.py menuconfig` → `sdkconfig.h`) |
| `compile_flags.txt` / `.clangd` | clangd compilation database |
| `.clang-format` · `.clang-format-ignore` · layered `.clang-tidy` | code style: formatting + naming rules; suggested at app layer, enforced below |
| `error_symbols.ld` | `ERR_SECTION_BASE` |
| `LICENSE` / `NOTICE` | Apache-2.0 full text / third-party attribution |
| `.gitignore` | build artifacts & local IDE noise (aligned with platform repos) |
| `README.md` / `CHANGELOG.md` / `CONTRIBUTING.md` | entry, changelog, contributing (kept at root per OSS convention) |
| `docs/` | all topical docs (see README.md) |

> Build is generic CMake: HAL ships weak empty implementations, and the board is injected via `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR`; an ESP-IDF component path (`cmake/esp_idf.cmake`) is also provided.

---

## board/ (Device Model & Device Tree)

| Path | Description |
| :--- | :--- |
| `include/device.h` | device model, properties, lock-holding VFS wrappers |
| `include/driver.h` | `DRIVER_REGISTER`, `board_driver_probe_all` |
| `include/bus.h` | bus controller abstraction |
| `include/dev_lifecycle.h` | driver I/O lifecycle |
| `include/board_config.h` | capacity macro aggregation, `dt_config_gen` / `config.h` |
| `include/VFS.h` | compatibility wrapper (forwards status, etc.) |
| `src/board_device.c` | device instances and lookup |
| `src/board_driver.c` | probe scheduling, safety-hw registration |
| `src/bus.c` | controller table |
| `src/dev_lifecycle.c` | lifecycle implementation |
| `src/config_store.c` | config storage |
| `src/task_config.c` · `task_utils.c` | task helpers |
| `dts/board.dts` | **placeholder** DTS (no real peripherals); override with `BOARD_DTS`; builds even with no board nodes |
| `dtsi/example-soc.dtsi` | generic example (cpus/soc/gpio/uart template, no SoC-specific fragments); override with `BOARD_DTSI_DIR` |
| `dt-bindings/` | parameter macros shared by middleware |

---

## hal / vfs / bus

Peripheral matrix: see [architecture.md §2](architecture.md#2-module-responsibilities).

Naming convention:

- HAL: `hal/<name>/hal_<name>.{h,c}`
- VFS: `vfs/<name>/vfs-<name>.{c,h}`
- Bus: `bus/<name>/<name>_bus.{c,h}`

Also: `hal/amp`, `hal/storage`, `hal/system`, `hal/hal_if_dummy.c` (HAL weak empty implementations), `hal/paths.cmake`.

---

## core / osal / interrupt / system

| Path | Description |
| :--- | :--- |
| `core/include/status.h` | `VFS_ERR_*`, `ERR_PTR` |
| `core/include/compiler_compat.h` | portable attributes & mem API |
| `core/include/compiler_compat_poison.h` | poison layer |
| `core/include/event_bus.h` · `event_bus.hpp` | event bus |
| `core/include/buffer_pool.h` | buffer pool |
| `core/include/system_log.h` · `production_log.h` | logging |
| `core/src/*.c` | implementations above |
| `osal/include/osal.h` | OSAL master header |
| `osal/include/osal_null.h` | bare-metal helper header + C++ task overload declaration |
| `osal/src/osal_{null,freertos}.c` | two backends (bare-metal / FreeRTOS) |
| `osal/src/osal_task.cpp` | bare-metal C++ task wrapper |
| `interrupt/interrupt.{c,h}` | VIRQ |
| `system_c/` · `system_cpp/` | init, wdt, scrubber, safe_state, task_manager, cmd (C or C++ via Kconfig) |
| `time_slice/task/xtask*.{c,h}` | bare-metal scheduler |

---

## tools / ide / Others

| Path | Description |
| :--- | :--- |
| `tools/dtc-lite.py` · `tools/dtc_lite/` | device tree compiler package |
| `tools/system_scrubber_crc_stub.h` | CRC stub |
| `ide/stubs/` | generated-header stubs for clangd |
| `drivers/<chip>/` | **37** product drivers (`include/` + `src/`, `DRIVER_REGISTER` + dtc-lite compile-time probe); e.g. `w25qxx`, `st7789`, `ssd1306`…; no legacy `drivers/flash` |
| `can_hook/` | CAN protocol superset hooks |
| `algorithm/buffer/` | ring & double buffers |
| `cmake/esp_idf.cmake` | ESP component entry (`idf_component_register`) |
| `cmake/etl.cmake` | ETL link helper (kept; the ESP path uses `lib/etl/include` directly) |

> `lib/` status: only **ETL** is vendored; other third-party libs (FreeRTOS, TinyUSB, cJSON, etc.) come through the **ESP-IDF component system** (see [ecosystem.md](ecosystem.md)).

---

## List All Source Files

```bash
find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
  ! -path './lib/*' ! -path './build/*' | sort
```

The command above lists middleware sources (excluding bricks and build output).

---

## Related Docs

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md)
