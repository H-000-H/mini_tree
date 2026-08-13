# Building-Block Open-Source Ecosystem

> The mini_tree middleware core provides the device model, VFS/Bus/HAL, OSAL and runtime services; it does **not** cram every capability into the core.
>
> Capability expansion follows a **link-as-a-block** model: link in the needed open-source library on demand, and supply configuration plus hardware glue through a board-level port.
>
> **`lib/` holds only the ETL vendor**; all other open-source blocks come through the **ESP-IDF component ecosystem** (`idf_component.yml` / registry), no more FetchContent. Closed-source middleware requiring paid commercial licenses is **not** integrated. Licenses live in each library and in [`NOTICE`](../NOTICE).

| Item | Content |
| :--- | :--- |
| **Audience** | Platform integrators, application developers, and anyone extending the ecosystem |
| **Related** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 0. Dependency Strategy

| Strategy | Behavior | Components |
| :--- | :--- | :--- |
| **Vendor (in git)** | Sources in `lib/`, committed with the repo | **ETL** (only) |
| **IDF components** | Pulled via `idf_component.yml` / `idf.py add-dependency` from the registry | FreeRTOS, TinyUSB, lwIP, cJSON, LVGL, mbedtls, etc. (on demand) |
| **C++ base (in by default)** | ETL in `lib/etl`; `cmake/esp_idf.cmake` links `lib/etl/include` by default | Upper-layer C++ / `SYSTEM_CPP` base |

> This branch no longer uses `cmake/dep_fetch.cmake` / FetchContent / `mini_tree_link_*` (removed). FreeRTOS is provided by ESP-IDF; `CONFIG_OSAL_FREERTOS` pairs with the IDF built-in kernel.

---

## 1. Why Blocks

| Principle | Meaning |
| :--- | :--- |
| **Open-source blocks** | All open source; re-check each library's `LICENSE` before commercial use (e.g. libmodbus is LGPL) |
| **IDF components on demand** | Keeps the tree small; declare the component in `idf_component.yml` and let the IDF Component Manager handle version and download |
| **Core stays lean** | The middleware never binds a vendor SDK, nor forces GUI / TLS / filesystems in |
| **Link on demand** | Optional blocks are not built into firmware by default; they enter the image only when declared in `idf_component.yml` |
| **ETL ships by default** | **Not an optional block**: it is the C++ foundation for upper layers, source lives in `lib/etl`, and `cmake/esp_idf.cmake` links it in by default |
| **Board supplies the port** | Config headers (e.g. `lv_conf.h`, `lwipopts.h`) and diskio/SPI/display-flush glue come from the platform |

```
┌──────────────────────────────────────────────────────────┐
│  App / product strategy (pick blocks: net? GUI? OTA? FS?) │
└────────────────────────────┬─────────────────────────────┘
                             │ declare deps in idf_component.yml
┌────────────────────────────▼─────────────────────────────┐
│  IDF component ecosystem (registry/managed) + lib/ETL      │
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
(Layering: app picks blocks → IDF component ecosystem → core → board hardware.)

You can keep adding more **open-source** libraries on this model: declare the corresponding component in `idf_component.yml`.

---

## 2. Integrated Open-Source Libraries

Grouped by capability. Versions follow the ESP Component Registry / IDF component declaration.

"Integration" means how to enable the block in an ESP project (mostly by declaring it in `idf_component.yml`).

### 2.1 Kernels & Scheduling (Infrastructure)

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| FreeRTOS | ESP-IDF built-in | IDF | RTOS kernel | `CONFIG_OSAL_FREERTOS` (default) |
| (Bare metal) | `time_slice/task` | — | Cooperative scheduling | `CONFIG_OSAL_NULL` |

### 2.2 Connectivity & Protocols

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| TinyUSB | `esp_tinyusb` (registry) | IDF | USB device/host stack | Board-level `usb_tusb_port` |
| lwIP | ESP-IDF built-in | IDF | TCP/IP | IDF network component + `lwipopts.h` |
| coreMQTT | registry component | registry | MQTT client | declare in `idf_component.yml` + `core_mqtt_config.h` |
| coreHTTP | registry component | registry | HTTP client | declare in `idf_component.yml` + `core_http_config.h` |
| libmodbus | registry component | registry | Modbus RTU/TCP | declare in `idf_component.yml` (prefer POSIX/RTOS) |
| FreeModbus | registry component | registry | Modbus RTU slave | declare in `idf_component.yml` + `mbport.h` |
| mbedtls | ESP-IDF built-in | IDF | TLS & crypto | IDF built-in component + `mbedtls_config.h` |

### 2.3 Storage & Upgrade

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| SFUD | registry component | registry | Unified SPI Flash driver | declare in `idf_component.yml` + `sfud_cfg.h` |
| littlefs | registry component | registry | Power-loss-safe filesystem | declare in `idf_component.yml` |
| FatFs | registry component | registry | FAT/exFAT | declare in `idf_component.yml` + `ffconf.h` |
| EasyFlash | registry component | registry | Flash ENV/IAP | declare in `idf_component.yml` |
| FlashDB | registry component | registry | KV + time-series DB | declare in `idf_component.yml` + `fdb_cfg.h` |
| MCUBoot | registry component | registry | Secure Boot / OTA | declare in `idf_component.yml` |

### 2.4 HMI & Input

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | `lvgl` / `esp_lvgl_port` (registry) | registry | Color GUI | declare in `idf_component.yml` + `lv_conf.h` |
| u8g2 | registry component | registry | Monochrome/OLED | declare in `idf_component.yml` |
| MultiButton | registry component | registry | Multi-button state machine | declare in `idf_component.yml` |

### 2.5 Data, Logging & Compute

| Library | Path | Version | Role | Integration |
| :--- | :--- | :--- | :--- | :--- |
| cJSON | registry component | registry | JSON | declare in `idf_component.yml` |
| ETL | `lib/etl` | 20.48.1 | **C++ foundation for upper layers** | **ships by default** |
| nanopb | registry component | registry | Protobuf | declare in `idf_component.yml` |
| EasyLogger | registry component | registry | Logging | declare in `idf_component.yml` |
| CMSIS-DSP | registry component | registry | DSP | declare in `idf_component.yml` |
| miniz | registry component | registry | zlib-compatible compression | declare in `idf_component.yml` |

---

## 3. Typical Block Combinations (Examples)

| Product Form | Suggested Blocks |
| :--- | :--- |
| Bare-metal instrument / small display | OSAL_NULL + u8g2 or LVGL + MultiButton + EasyLogger |
| Networked sensor | FreeRTOS + lwIP + coreMQTT/coreHTTP + mbedtls + cJSON/nanopb |
| SPI-Flash data logger | SFUD + littlefs or FlashDB + EasyLogger (+ optionally miniz) |
| USB mass storage / NIC | TinyUSB (+ optionally) FatFs / lwIP |
| OTA-capable production device | MCUBoot + mbedtls (signature verify) + download channel (USB/network) (+ optionally miniz) |
| Industrial slave | FreeModbus (RTU) or libmodbus (POSIX) |

---

## 4. Adding a New Block

1. Declare the component in the ESP project's `idf_component.yml` (`idf.py add-dependency` or edit by hand).
2. If it needs a board-level port (display, network, etc.), provide the port in the board project.
3. Update `README` / this doc / [`NOTICE`](../NOTICE).

Policy: **open source only; go through the IDF component system; never commit bulk third-party sources.**

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
