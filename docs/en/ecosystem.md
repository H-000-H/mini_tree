# Building-Block Open-Source Ecosystem

> The mini_tree middleware core provides the device model, VFS/Bus/HAL, OSAL and runtime services; it does **not** cram every capability into the core.
>
> Capability expansion follows a **link-as-a-block** model: link in the needed open-source library on demand, and supply configuration plus hardware glue through a board-level port.
>
> **`lib/` holds only the vendors** (FreeRTOS, RT-Thread, ETL); TinyUSB / lwIP / cJSON are **config-time FetchContent** (the root CMake directly `include`s their `cmake/*.cmake`), and all other open-source blocks are **link-time FetchContent** (a local `lib/<Name>` still wins; clone manually for offline use). Closed-source middleware requiring paid commercial licenses is **not** integrated. Licenses live in each library and in [`NOTICE`](../NOTICE).

| Item | Content |
| :--- | :--- |
| **Audience** | Platform integrators, application developers, and anyone extending the ecosystem |
| **Related** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 0. Infrastructure Vendors vs. Fetched Blocks

| Strategy | Behavior | Components |
| :--- | :--- | :--- |
| **Vendor (in git)** | Sources in `lib/`, committed with the repo | **FreeRTOS**, **RT-Thread**, **ETL** |
| **Config-time Fetch** | Root CMake directly `include`s `cmake/*.cmake`, local-or-fetch | **TinyUSB**, **lwIP**, **cJSON** |
| **Link-time Fetch** | Pulled only when `mini_tree_link_*` is called; clone to `lib/<Name>` for offline | littlefs, FatFs, MultiButton, MCUBoot, nanopb, coreMQTT, coreHTTP, miniz, libmodbus, LVGL, u8g2, mbedtls, CMSIS-DSP, FlashDB, SFUD, EasyFlash, EasyLogger, FreeModbus… |
| **C++ base (in by default)** | ETL in `lib/etl`; root CMake always `mini_tree_link_etl(mini_tree)` | Upper-layer C++ / `SYSTEM_CPP` base |

Implementation: `mini_tree_dep_get()` in `cmake/dep_fetch.cmake` (uses the local copy when its marker file exists, otherwise `FetchContent`).

Optional block paths are listed in the root [`.gitignore`](../.gitignore).

> **Change**: `cmake/tinyusb.cmake` tolerates the offline case where `src/CMakeLists.txt` is not provided locally — the TinyUSB core sources are left empty instead of failing (the `mini_tree` static library does not link tinyusb by default; only the board-level USB port needs it).

---

## 1. Why Blocks

| Principle | Meaning |
| :--- | :--- |
| **Open-source blocks** | All open source; re-check each library's `LICENSE` before commercial use (e.g. libmodbus is LGPL) |
| **Vendors for infrastructure, Fetch for the rest** | Keeps the tree small; OS/ETL are resident; every block needs network or a local copy at first link |
| **Core stays lean** | The middleware never binds a vendor SDK, nor forces GUI / TLS / filesystems in |
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
| coreHTTP | Fetch / `lib/coreHTTP` | v3.1.3 | HTTP client | `mini_tree_link_corehttp` + `core_http_config.h` |
| libmodbus | Fetch / `lib/libmodbus` | v3.1.10 | Modbus RTU/TCP | `mini_tree_link_libmodbus` (prefer POSIX/RTOS) |
| FreeModbus | Fetch / `lib/FreeModbus` | 1.6.0 | Modbus RTU slave | `mini_tree_link_freemodbus` + `mbport.h` |
| mbedtls | Fetch / `lib/mbedtls` | mbedtls-4.2.0 | TLS & crypto | `mini_tree_link_mbedtls` + `mbedtls_config.h` |

### 2.3 Storage & Upgrade

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| SFUD | Fetch / `lib/SFUD` | 1.1.0 | Unified SPI Flash driver | `mini_tree_link_sfud` + `sfud_cfg.h` |
| littlefs | Fetch / `lib/littlefs` | v2.11.3 | Power-loss-safe filesystem | `mini_tree_link_littlefs` |
| FatFs | Fetch / `lib/FatFs` | R0.16 | FAT/exFAT | `mini_tree_link_fatfs` + `ffconf.h` |
| EasyFlash | Fetch / `lib/EasyFlash` | master | Flash ENV/IAP | `mini_tree_link_easyflash` |
| FlashDB | Fetch / `lib/FlashDB` | 2.2.0 | KV + time-series DB | `mini_tree_link_flashdb` + `fdb_cfg.h` |
| MCUBoot | Fetch / `lib/mcuboot` | v2.4.0 | Secure Boot / OTA | `mini_tree_link_mcuboot` |

### 2.4 HMI & Input

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | Fetch / `lib/lvgl` | v9.5.0 | Color GUI | `mini_tree_link_lvgl` + `lv_conf.h` |
| u8g2 | Fetch / `lib/u8g2` | 2.37.1 | Monochrome/OLED | `mini_tree_link_u8g2` |
| MultiButton | Fetch / `lib/MultiButton` | master | Multi-button state machine | `mini_tree_link_multibutton` |

### 2.5 Data, Logging & Compute

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| cJSON | Fetch / `lib/cJSON` | 1.7.19 | JSON | `mini_tree_link_cjson` |
| ETL | `lib/etl` | 20.48.1 | **C++ foundation for upper layers** | **ships by default** |
| nanopb | Fetch / `lib/nanopb` | 0.4.9.1 | Protobuf | `mini_tree_link_nanopb` |
| EasyLogger | Fetch / `lib/EasyLogger` | 2.2.0 | Logging | `mini_tree_link_easylogger` |
| CMSIS-DSP | Fetch / `lib/CMSIS-DSP` | v1.17.1 | DSP | `mini_tree_link_cmsis_dsp` |
| miniz | Fetch / `lib/miniz` | 3.1.2 | zlib-compatible compression | `mini_tree_link_miniz` |

---

## 3. Typical Block Combinations (Examples)

| Product Form | Suggested Blocks |
| :--- | :--- |
| Bare-metal instrument / small display | OSAL_NULL + u8g2 or LVGL + MultiButton + EasyLogger |
| Networked sensor | FreeRTOS/RTT + lwIP + coreMQTT/coreHTTP + mbedtls + cJSON/nanopb |
| SPI-Flash data logger | SFUD + littlefs or FlashDB + EasyLogger (+ optionally miniz) |
| USB mass storage / NIC | TinyUSB (+ optionally) FatFs / lwIP |
| OTA-capable production device | MCUBoot + mbedtls (signature verify) + download channel (USB/network) (+ optionally miniz) |
| Industrial slave | FreeModbus (RTU) or libmodbus (POSIX) |

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
- **Avoid**: Hard-binding a GUI/TLS implementation in `vfs/` / `bus/` public headers, or leaking vendor HAL typedefs into the middleware public API.
- **Southbound**: Flash/display/NIC still touch hardware through board-level HAL or port callbacks, keeping "hardware direct-inject, middleware never binds an SDK".

The 37 product drivers live in `drivers/<chip>/{include,src}`; they are part of the ecosystem but follow this repo's `DRIVER_REGISTER` contract and stay independent of the block libraries.

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
| libmodbus | Stéphane Raimbault & contributors | Modbus protocol stack |
| Mbed TLS | TrustedFirmware / Mbed-TLS | TLS & cryptography |
| SFUD / EasyFlash / FlashDB / EasyLogger | armink & contributors | Flash & logging toolchain |
| littlefs | littlefs-project | Power-loss-safe filesystem |
| FatFs | ChaN | General-purpose FAT filesystem |
| MCUBoot | MCUBoot, Zephyr & contributors | Secure boot & upgrade |
| LVGL | kisvegabor & LVGL community | Embedded GUI |
| u8g2 | olikraus & contributors | Monochrome display library |
| MultiButton | 0x1abin & contributors | Button state machine |
| cJSON | Dave Gamble & contributors | JSON parsing |
| nanopb | Petteri Aimonen & contributors | Embedded Protobuf |
| ETL | John Wellbelove / ETLCPP | Heap-free template library |
| CMSIS-DSP | Arm & contributors | DSP algorithm library |
| coreHTTP | FreeRTOS / Amazon (incl. llhttp) | Embedded HTTP |
| miniz | Rich Geldreich & contributors | zlib-compatible compression |
| FreeModbus | Christian Walter & contributors | Modbus slave |

If a credit is missing or a license statement is wrong, feel free to open an Issue / PR. Full copyright and license statements are governed by the files inside each component and by [`NOTICE`](../NOTICE).
