# Building-Block Open-Source Ecosystem

> The mini_tree middleware core provides the device model, VFS/Bus/HAL, OSAL and runtime services; it does **not** cram every capability into the core.
>
> Capability expansion follows a **link-as-a-block** model: link in the needed open-source library on demand, and supply configuration plus hardware glue through a board-level port.
>
> **`lib/` holds only the vendors** (FreeRTOS, RT-Thread, ETL); TinyUSB / lwIP are **config-time FetchContent** (the root CMake directly `include`s their `cmake/*.cmake`), and all other open-source blocks are **link-time FetchContent** (a local `lib/<Name>` still wins; clone manually for offline use). Closed-source middleware requiring paid commercial licenses is **not** integrated. Licenses live in each library and in [`NOTICE`](../NOTICE).

| Item | Content |
| :--- | :--- |
| **Audience** | Platform integrators, application developers, and anyone extending the ecosystem |
| **Related** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 0. Infrastructure Vendors vs. Fetched Blocks

| Strategy | Behavior | Components |
| :--- | :--- | :--- |
| **Vendor (in git)** | Sources in `lib/`, committed with the repo | **FreeRTOS**, **RT-Thread**, **ETL** |
| **Config-time Fetch** | Root CMake directly `include`s `cmake/*.cmake`, local-or-fetch | **TinyUSB**, **lwIP** |
| **Link-time Fetch** | Pulled only when `mini_tree_link_*` is called; clone to `lib/<Name>` for offline | littlefs, FatFs, MultiButton, MCUBoot, coreMQTT, LVGL, u8g2, FlashDB, SFUD, EasyFlash, EasyLogger… |
| **C++ base (in by default)** | ETL in `lib/etl`; root CMake always `mini_tree_link_etl(mini_tree)` | Upper-layer C++ / `SYSTEM_CPP` base |

Implementation: `mini_tree_dep_get()` in `cmake/dep_fetch.cmake` (uses the local copy when its marker file exists, otherwise `FetchContent`).

Optional block paths are listed in the root [`.gitignore`](../.gitignore).

> **Change**: `cmake/tinyusb.cmake` tolerates the offline case where `src/CMakeLists.txt` is not provided locally — the TinyUSB core sources are left empty instead of failing (the `mini_tree` static library does not link tinyusb by default; only the board-level USB port needs it).

---

## 1. Why Blocks

| Principle | Meaning |
| :--- | :--- |
| **Open-source blocks** | All open source; re-check each library's `LICENSE` before commercial use |
| **Vendors for infrastructure, Fetch for the rest** | Keeps the tree small; OS/ETL are resident; every block needs network or a local copy at first link |
| **Core stays lean** | The middleware never binds a vendor SDK, nor forces GUI / filesystems in |
| **Link on demand** | Optional blocks are not built into firmware by default; they enter the image only when `mini_tree_link_*` (or the OSAL Kconfig) is used |
| **ETL ships by default** | **Not an optional block**: it is the C++ foundation for upper layers, source lives in `lib/etl`, and the root CMake links it into `mini_tree` by default |
| **One CMake entry per block** | Most libraries have a `cmake/<name>.cmake` exposing `mini_tree_link_<name>(target …)` |
| **Board supplies the port** | Config headers (e.g. `lv_conf.h`, `lwipopts.h`) and diskio/SPI/display-flush glue come from the platform |

```
┌──────────────────────────────────────────────────────────┐
│  App / product strategy (pick blocks: net? GUI? OTA? FS?) │
└────────────────────────────┬─────────────────────────────┘
                             │ mini_tree_link_* / Kconfig
┌────────────────────────────▼─────────────────────────────┐
│  Infrastructure lib/ (OS·ETL) + on-demand Fetch blocks     │
└────────────────────────────┬─────────────────────────────┘
                             │ device / ioctl / EventBus
┌────────────────────────────▼─────────────────────────────┐
│  mini_tree core: board · vfs · bus · hal · osal · system  │
└────────────────────────────┬─────────────────────────────┘
                             │ board DTS + strong-symbol HAL
┌────────────────────────────▼─────────────────────────────┐
│  Chip SDK / pins / Flash layout / display & NIC hardware   │
└──────────────────────────────────────────────────────────┘
```
(Layering: app picks blocks → infrastructure lib/Fetch → core → board hardware.)

You can keep adding more **open-source** libraries on this model: rely on Fetch or drop the sources into `lib/<Name>`, add a `cmake/<name>.cmake`, and call `mini_tree_link_*` from your product CMake.

---

## 2. Integrated Open-Source Libraries

Grouped by capability. Versions are pinned by `*_VERSION` / `GIT_TAG` in the matching `cmake/*.cmake`.

A `lib/...` path is the conventional location; **fetched blocks may exist only in the build cache**.

### 2.1 Kernels & Scheduling (Infrastructure)

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| FreeRTOS | `lib/freeRTOS` | Kernel V11.3.0 | RTOS kernel | `CONFIG_OSAL_FREERTOS` |
| RT-Thread | `lib/rtthread` | v5.3.0 | RTOS kernel | `CONFIG_OSAL_RTTHREAD` |
| (Bare metal) | `time_slice/task` | — | Cooperative scheduling | `CONFIG_OSAL_NULL` |

### 2.2 Connectivity & Protocols

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| TinyUSB | Fetch / `lib/tinyusb` | 0.21.0 | USB device/host stack | Board-level `usb_tusb_port` |
| lwIP | Fetch / `lib/lwip` | 2.2.1 | TCP/IP | `mini_tree_link_lwip` + `lwipopts.h` |
| coreMQTT | Fetch / `lib/coreMQTT` | v5.0.2 | MQTT client | `mini_tree_link_coremqtt` + `core_mqtt_config.h` |

### 2.3 HMI & Input

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | Fetch / `lib/lvgl` | v9.5.0 | Color GUI | `mini_tree_link_lvgl` + `lv_conf.h` |
| u8g2 | Fetch / `lib/u8g2` | 2.37.1 | Monochrome/OLED | `mini_tree_link_u8g2` |
| MultiButton | Fetch / `lib/MultiButton` | master | Multi-button state machine | `mini_tree_link_multibutton` |

### 2.4 Logging

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| ETL | `lib/etl` | 20.48.1 | **C++ foundation for upper layers** | **ships by default** |
| EasyLogger | Fetch / `lib/EasyLogger` | 2.2.0 | Logging | `mini_tree_link_easylogger` |

---

## 3. Typical Block Combinations (Examples)

| Product Form | Suggested Blocks |
| :--- | :--- |
| Bare-metal instrument / small display | OSAL_NULL + u8g2 or LVGL (via `ui/` glue layer + `DISPLAY_CMD_*`) + MultiButton + EasyLogger |
| Networked sensor | FreeRTOS/RTT + lwIP + coreMQTT |
| USB mass storage / NIC | TinyUSB (+ optionally) FatFs / lwIP |

---

## 4. Adding a New Block

1. Prefer `mini_tree_dep_get()` + Fetch; clone into `lib/<Name>` only when offline.
2. Add `cmake/<name>.cmake`: do **not** link it by default; provide `mini_tree_link_<name>(target …)`.
3. `include` it in the root `CMakeLists.txt`; update `README` / this doc / [`NOTICE`](../NOTICE); add it to `.gitignore`.
4. The product project provides the port, then calls the link function.

Policy: **open source only; prefer Fetch for everything except infrastructure; never commit bulk third-party sources.**

---

## 5. Boundary with the Middleware Core

- **Allowed**: Call open-source library APIs from applications or board services; cooperate with the middleware via `device_*` / EventBus.
- **Avoid**: Hard-binding a GUI implementation in `vfs/` / `bus/` public headers, or leaking vendor HAL typedefs into the middleware public API.
- **Southbound**: Flash/display/NIC still touch hardware through board-level HAL or port callbacks, keeping "hardware direct-inject, middleware never binds an SDK".
- **UI glue layer (`ui/`)**: LVGL / u8g2 flush callbacks go through the unified entry point `ui/display/display_ui_bridge.h`, which calls `device_ioctl(DISPLAY_CMD_*)` to reach display hardware; no direct `bus_*` / `hal_*` calls — swapping displays only requires changing the device pointer.

The 39 product drivers live in `drivers/<chip>/{include,src}`; they are part of the ecosystem but follow this repo's `DRIVER_REGISTER` contract and stay independent of the block libraries.

---

## 6. Acknowledgements

mini_tree's block ecosystem stands on the shoulders of many open-source authors and communities. Thanks (in no particular order):

| Project | Upstream | What We Thank Them For |
| :--- | :--- | :--- |
| FreeRTOS | Amazon FreeRTOS / FreeRTOS.org | Real-time kernel |
| RT-Thread | RT-Thread team | Homegrown RTOS and component ecosystem |
| TinyUSB | Ha Thach & contributors | Portable USB stack |
| lwIP | Savannah / lwIP community | Lightweight TCP/IP |
| coreMQTT | FreeRTOS / Amazon | Embedded MQTT |
| SFUD / EasyFlash / FlashDB / EasyLogger | armink & contributors | Flash drivers & logging toolchain |
| littlefs | littlefs-project | Power-loss-safe filesystem |
| FatFs | ChaN | General-purpose FAT filesystem |
| MCUBoot | MCUBoot, Zephyr & contributors | Secure boot & upgrade |
| LVGL | kisvegabor & LVGL community | Embedded GUI |
| u8g2 | olikraus & contributors | Monochrome display library |
| MultiButton | 0x1abin & contributors | Button state machine |
| ETL | John Wellbelove / ETLCPP | Heap-free template library |

If a credit is missing or a license statement is wrong, feel free to open an Issue / PR. Full copyright and license statements are governed by the files inside each component and by [`NOTICE`](../NOTICE).
