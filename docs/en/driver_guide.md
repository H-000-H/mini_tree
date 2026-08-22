# Device Tree and Drivers

> DTS layout, the dtc-lite pipeline, `DRIVER_REGISTER`, and the compatible/property contracts.

| Item | Description |
| :--- | :--- |
| **Audience** | People writing platform dtsi / VFS drivers |
| **Prereq.** | [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) |
| **Related** | [tools_guide.md](../tools_guide.md) · [architecture.md](architecture.md) |

---

## Table of Contents

- [Device Tree and Drivers](#device-tree-and-drivers)
  - [Table of Contents](#table-of-contents)
  - [1. File Layout](#1-file-layout)
    - [Middleware (this repo)](#middleware-this-repo)
    - [Platform project (recommended)](#platform-project-recommended)
  - [2. dtc-lite Pipeline](#2-dtc-lite-pipeline)
  - [3. DRIVER_REGISTER](#3-driver_register)
  - [4. Registered Drivers in This Repo (scan result)](#4-registered-drivers-in-this-repo-scan-result)
  - [5. Compatible Strings and Properties](#5-compatible-strings-and-properties)
    - [5.1 Naming Conventions](#51-naming-conventions)
    - [5.2 Property Injection](#52-property-injection)
    - [5.3 status / criticality / deps](#53-status-criticality-deps)
  - [6. Runtime API](#6-runtime-api)
  - [7. Remove Lifecycle](#7-remove-lifecycle)
  - [Related Docs](#related-docs)

---

## 1. File Layout

### Middleware (this repo)

| Path | Description |
| :--- | :--- |
| `board/dts/board.dts` | **Placeholder** root node (`mini-tree,placeholder` only, no peripherals); boards override via `BOARD_DTS` |
| `board/dtsi/` | **Node-template library**: `example-soc.dtsi` (SoC skeleton: cpus/gpio/uart) + `vfs/` (one file per VFS) + `drivers/` (one file per product driver); all-0 placeholders with usage comments; `BOARD_DTSI_DIR` may point at platform-owned dtsi |
| `board/dt-bindings/{gpio,spi,uart,tim}/` | Generic `#define`s for dtsi `#include <dt-bindings/...>` |
| `drivers/<chip>/{include,src}` | Product drivers (37); GLOB-scanned by CMake / `dtc-lite` |

> **How to use the templates**: drivers templates mount on labels defined by the vfs templates (e.g. `&i2c0 { aht20: aht20@0 {...} }`). The board enables both the bus node and the device node (`status = "okay"`); the instance pool size auto-equals `DTC_GEN_COUNT_*` (count of same-compatible nodes, default 1) — no manual tuning.

### Platform project (recommended)

```text
# ESP reference: components/board_esp32s3/
dts/board.dts                      # BOARD_DTS entry
dtsi/<soc>.dtsi                    # SoC / bus / product fragments
dtsi/<soc>-product-drivers.dtsi
…
# HAL strong symbols: components/hal_esp32s3/
# Exception driver: components/driver_ws2812/
```

> Above: the `BOARD_DTS` entry, SoC/product dtsi fragments, HAL strong symbols, and the exception driver all live in the platform project (ESP reference).

CMake: `BOARD_DTS`, `BOARD_DTSI_DIR`; vendor header search: `VENDOR_INC_DIRS` / `VENDOR_DEFINES`.

Product drivers and ESP wiring: see [esp_idf_cmake.md](esp_idf_cmake.md) (on the **`esp` branch**).

---

## 2. dtc-lite Pipeline

```bash
python3 tools/dtc-lite.py <board.dts> <out_dir> \
  vfs/spi vfs/uart … drivers/w25qxx … \
  -I <vendor_include_dir> … -D <NAME[=VALUE]> …
```

> Above: `<board.dts>` is the entry, `<out_dir>` the output dir, followed by the `DRIVER_REGISTER` scan dirs; `-I` adds vendor header search dirs, `-D` adds preprocessor macros (matching root `CMakeLists.txt`'s `VENDOR_INC_DIRS` / `VENDOR_DEFINES`).

The root `CMakeLists.txt` already passes the repo's relevant vfs/bus/drivers dirs.

| Step | Behavior |
| :--- | :--- |
| Preprocess | Resolves `#include`, may run cpp on vendor headers |
| Parse | Lark grammar → AST |
| Merge | `/ { }`, `&label { }` overlay |
| Scan drivers | Finds `DRIVER_REGISTER` in the given source dirs |
| Generate | See the table below |

| Generated file | Content |
| :--- | :--- |
| `board_nodes.h` | `device_id_t`, `DEV_ID_*`, `DEV_ID_COUNT`, chosen macros |
| `board_devtable.h/.c` | `board_node_get` / `board_dev_find*` / cascade etc. |
| `board_probe.c` | probe/remove function table and order |
| `dt_config_gen.h` | `DTC_GEN_COUNT_*`, clock/capacity aggregation |
| `board_handles.h` | chosen handle-style macros |

---

## 3. DRIVER_REGISTER

Defined in `board/include/driver.h`:

```c
DRIVER_REGISTER(name, "compatible-string", probe_fn, remove_fn);
```

Expands to:

- `int board_driver_probe_<name>(struct device *dev);`
- `int board_driver_remove_<name>(struct device *dev);`

dtc-lite collects them into a static table; **no runtime `strcmp` matching of driver names** (the compatible is bound at generation time).

Rules:

- `name`: C identifier, globally unique
- `compatible`: **exactly** matches the DTS node's `compatible = "..."`
- `probe`/`remove`: return `VFS_OK` or `VFS_ERR_*`

Identifiers are uniformly lowercase (enforced by `.clang-tidy` `readability-identifier-naming`): `x_task` / `x_scheduler` / `list_node` / `k_tag` / `struct event` / `mini_tree::`, etc.; `.clang-format` uses Allman braces, no braces on single statements, 4-space indent, 200 columns. Recommended at `app`, mandatory below `app`.

---

## 4. Registered Drivers in This Repo (scan result)

| Area | Example compatible / registration |
| :--- | :--- |
| `vfs/spi` | `spi-master` / `spi-slave` / `heterogeneous,spi-*-client` |
| `vfs/uart` | host + client pair |
| `vfs/i2c` · `vfs/i2s` | master/slave + heterogeneous client |
| `vfs/can` | host + client |
| `vfs/usb` | `usb-otg-host`, `heterogeneous,usb-cdc-acm/ecm`, `heterogeneous,usb-hid` |
| `vfs/gpio` · `adc` · `dac` · `tim` · `rtc` · `iwdg` · `wwdg` | Per-peripheral compatibles |
| `drivers/<chip>/` | 37 product drivers (GLOB-scanned); e.g. `winbond,w25qxx`, `sitronix,st7789`, `solomon,ssd1306`, `modbus,rtu-rs485`, … |
| `board` | `board,safety-hw` |
| Out-of-tree `driver_ws2812` | `worldsemi,ws2812` (the only vendor-RMT exception) |

Product drivers follow `drivers/<chip>/{include,src}`. CMake / dtc-lite scan with `drivers/*/src` — **don't** maintain per-file lists.

The `DRIVER_REGISTER` entries in the source are authoritative; re-run dtc-lite after adding/removing drivers.

The **ioctl / read-write semantics summary** lives in [peripherals.md](peripherals.md).

ESP wiring details: see [esp_idf_cmake.md](esp_idf_cmake.md) (on the **`esp` branch**).

---

## 5. Compatible Strings and Properties

### 5.1 Naming Conventions

| Role | Style | Example |
| :--- | :--- | :--- |
| Controller host | Short name or `*-master` / `*-host` | `spi-master`, `can-host`, `usb-otg-host` |
| Bus client | `heterogeneous,<…>-client` | `heterogeneous,spi-master-client` |
| Board special | `board,…` | `board,safety-hw` |

### 5.2 Property Injection

Common keys (per peripheral): `reg`, `interrupts`, `status`, `*-base`, `*-clk`, `tx-port`/`rx-pin`/`*-af`, `prescaler`, `dma-*`, `irqn`, `irq-priority`, `it-enable`, …

Values should be **expanded integers** (vendor macros), filled into `hal_*_config` / `hal_*_bus_config` by the VFS probe.

### 5.3 status / criticality / deps

- `status = "okay"/"disabled"` (baked into the node default state)
- criticality: IGNORE / WARNING / FATAL on probe failure (see `device.h`)
- `deps`: dependencies on other `device_id_t`s, checked before probe

---

## 6. Runtime API

| API | Purpose |
| :--- | :--- |
| `device_find` / `_by_label` / `_by_compatible` / `_by_id` | Lookup |
| `device_get_prop_int` / `_str` / `_bool` / `_int_array` | Read properties |
| `device_get_reg` / `device_get_irq` | Read reg/irq tables |
| `device_open` / `read` / `write` / `ioctl` / `close` | Locked I/O |
| `board_dev_get` / `board_probe_order` | Table access (generated headers) |

---

## 7. Remove Lifecycle

Drivers with fops should follow this order (`driver.h` comments):

1. `dev_lc_remove_start(device_lc(dev))`
2. `device_ops_unregister(dev)`
3. `dev_lc_remove_drain(..., OSAL_WAIT_FOREVER)`
4. Release hardware / bus
5. `dev_lc_remove_finish(...)`

On the probe-success path: product drivers under `drivers/` don't call `device_lc_bind(dev)` manually (the framework's `device_tree_init` binds them all); VFS-layer drivers (`vfs/`) need an explicit re-bind after the pool resets.

---

## Related Docs

- [usb_tusb_port.md](usb_tusb_port.md) · [peripherals.md](peripherals.md) · [amp.md](amp.md)
- [tools_guide.md](../tools_guide.md)
- [service_spec.md](service_spec.md) · [faq.md](faq.md)
