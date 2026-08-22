# mini_tree

> Platform-agnostic embedded middleware
>
> Using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal, FreeRTOS, and RT-Thread; zero vendor SDK lock-in — chip HAL, pinmux, and board DTS are entirely supplied by your platform project.

---

## Overview

Platform-agnostic embedded middleware using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal, FreeRTOS, and RT-Thread. Zero vendor SDK lock-in — chip HAL, pinmux, and board DTS are entirely supplied by your platform project.

---

## Architecture

- **Device Tree**: DTS/DTSI compiled via `dtc-lite` into static probe tables; zero runtime string matching, zero parsing overhead.
- **Hardware Direct-Inject**: Vendor macros expand directly into config structs; no intermediate enum mapping.
- **Layer Isolation**: `app → board → vfs → bus → hal(weak) → vendor SDK`; strict layering with poison guard on `hal_*` in all public headers.
- **Two-Phase Boot**: Standardized boot pipeline `pre_os_init` → `start_tasks` → `system_init_complete` → scheduler or cooperative main loop.

---

## Peripheral Coverage

| Bus-Based | Bus-Less | HAL-Only |
|:---|:---|:---|
| SPI, I2C, I2S, UART, CAN, USB | GPIO, ADC, DAC, TIM, RTC, IWDG, WWDG | AMP/CPU, Storage, Platform Safety, SDIO (reserved / HAL slot, not yet implemented) |

---

## Product Drivers (37)

| Category | Chips & Modules |
|:---|:---|
| Sensors (12) | AHT20, BME280, BMP280, BH1750, SHT30, SHT40, MPU6050, INA219, ADS1115, DS18B20, VL53L0X, NEO-M8N |
| Displays (5) | SSD1306, SH1106, ST7789, E-paper, MAX7219 |
| Touch (2) | FT5x06, XPT2046 |
| Communication (7) | NRF24L01, SX1278 (LoRa), HC-05 (BT), SN65HVD230 (CAN), RS485 Modbus RTU, A7670 (4G), Air780E (4G) |
| Storage & NFC (4) | W25Qxx (SPI NOR), AT24C02 (EEPROM), PN532, RC522 |
| Actuators (6) | SG90, DRV8833, Relay, Buzzer, DFPlayer, MAX98357A |
| Other (1) | PCF8574 (GPIO Expander) |

---

## OSAL — One API, Three Backends

| Backend | Model | Dependency |
|:---|:---|:---|
| `CONFIG_OSAL_NULL` | Cooperative Time-Slice / Preemptive (bare-metal) | None |
| `CONFIG_OSAL_FREERTOS` | Preemptive | FreeRTOS v11.3.0 |
| `CONFIG_OSAL_RTTHREAD` | Preemptive | RT-Thread v5.3.0 |

The bare-metal backend (`CONFIG_OSAL_NULL`) picks one scheduler from the `Kconfig.mini_tree` "bare-metal scheduler" choice (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`, default `XTASK_COOP`). Both implementations share the same `xtask.h` API surface and are mutually exclusive at both CMake (`MINI_TREE_XTASK_*`) and `#ifdef` level — caller code switches transparently:

- `XTASK_NONE` — no scheduler; write your own `while(1)` loop
- `xtask_coop.c` (cooperative / round-robin, default) — `XTASK_COOP`
- `xtask_preempt.c` (preemptive, N+1 linked-list multi-priority, finished & compilable) — `XTASK_PREEMPT`

---

## Runtime Services

- **EventBus** — Range subscription, ISR-safe post, seal-after-boot.
- **VIRQ** — Virtual IRQ blocks, top-half / bottom-half (SPSC deferred queue).
- **BufferPool** — Pooled static allocator; ring FIFO & double buffer.
- **Safe State** — Shutdown callbacks, watchdogs, flash scrubber (optional brick).
- **Production Log** — Black-box fault recording for field diagnostics.

---

## Build & Toolchain

- **CMake ≥ 3.16** — `add_subdirectory(mini_tree)` + `mini_tree_link_*` on-demand linking; generic chip-agnostic path.
- **Kconfig** — `.config` → `genconfig.py` → `config.h`; interactive configuration via `menuconfig.py`. Official kconfiglib (by Ulf Magnusson, ISC license) is vendored under `tools/_vendor/` — no `pip install` needed; prepended to `sys.path` by `tools/_vendor_loader.py`. The three `.py` files stay in sync with upstream, unmodified.
- **dtc-lite** — Lightweight DTS compiler (`pip install lark`), auto-generating probe tables & board headers.
- **Coding style** — `.clang-format` (Allman, no braces for single statements, one-line short functions, 4-space, 200 cols) + layered `.clang-tidy` (naming); recommended in `app/`, mandatory below.
- **Targets** — ARM Cortex-M0 / M0+ / M3 / M4F / M7, RISC-V 32-bit; dual-core heterogeneous AMP supported — covered by all three OSAL backends (Bare-Metal / FreeRTOS / RT-Thread).

---

## Ecosystem

Core stays lean; extend on demand:

> **FetchContent (on demand):**
> TinyUSB · lwIP · cJSON · LVGL · u8g2 · littlefs · FatFs · SFUD · Mbed TLS · coreMQTT · coreHTTP · nanopb · miniz · MCUBoot · FreeModbus · libmodbus · CMSIS-DSP · MultiButton · EasyFlash · EasyLogger · FlashDB

> **Vendored in `lib/`:**
> FreeRTOS · RT-Thread · **ETL** (heap-free C++ containers, always linked)

---

## Getting Started

```bash
git clone https://github.com/H-000-H/mini_tree.git
```

```cmake
add_subdirectory(path/to/mini_tree)

# Link the middleware core
target_link_libraries(your_firmware PUBLIC mini_tree)

# Opt-in bricks (examples)
# mini_tree_link_cjson(your_firmware)
# mini_tree_link_lwip(your_firmware "${CMAKE_CURRENT_SOURCE_DIR}/port")
```

Also required at board level: board DTS, strong-symbol `hal_*`, and per-brick port headers (e.g. `lwipopts.h`, `lv_conf.h`).

Step-by-step: [docs/en/getting_started.md](docs/en/getting_started.md)

---

## ESP-IDF (ESP32)

> **ESP 支持已从 `main` 分支剥离**。`main` 只保留平台无关的通用中间件；所有 ESP-IDF 相关代码（`components/mini_tree`、`board_port.cmake`、`hal_<soc>`、`esp_idf.cmake`）已迁至 **`esp` 分支**。

获取 ESP 版本二选一：

- **`esp` 分支**（完整移植工程 + 全部板级代码）：
  ```bash
  git clone -b espidf-branch https://github.com/H-000-H/mini_tree.git
  ```
- **乐鑫组件注册表（ESP-IDF Component Registry）直接下拉**：在 `idf_component.yml` 中声明
  ```yaml
  dependencies:
    h-000-h/mini_tree: ">=1.2.0"
  ```
  或在工程目录执行 `idf.py add-dependency "h-000-h/mini_tree"`，由 `idf-component-manager` 自动拉取。

ESP 移植步骤见 **`esp` 分支**上的 [docs/en/esp_idf_cmake.md](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/en/esp_idf_cmake.md)（中文版 `docs/cn/esp_idf_cmake.md`）；本仓 `main` 分支不再维护 ESP 移植细节。

---

## Documentation

Root keeps only entry & legal files; all topics live in [`docs/`](docs/en/README.md).

| I want to… | Read |
| :--- | :--- |
| Get an overview | [docs/en/usage.md](docs/en/usage.md) |
| Integrate | [docs/en/getting_started.md](docs/en/getting_started.md) |
| Coding style | [docs/en/coding_style.md](docs/en/coding_style.md) |
| Architecture | [docs/en/architecture.md](docs/en/architecture.md) |
| Ecosystem | [docs/en/ecosystem.md](docs/en/ecosystem.md) |
| Port a board | [docs/en/device_tree_porting.md](docs/en/device_tree_porting.md) · [docs/en/driver_guide.md](docs/en/driver_guide.md) |
| Write apps | [docs/en/service_spec.md](docs/en/service_spec.md) · [docs/en/peripherals.md](docs/en/peripherals.md) |
| Find files | [docs/en/file_index.md](docs/en/file_index.md) |
| FAQ | [docs/en/faq.md](docs/en/faq.md) |

中文文档 / Chinese docs: [docs/cn/README.md](docs/cn/README.md)

Toolchain: [docs/cn/tools_guide.md](docs/cn/tools_guide.md) · [docs/en/tools_guide.md](docs/en/tools_guide.md)

---

## Development

Issues & PRs welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

- Changelog: [CHANGELOG.md](CHANGELOG.md)
- Design decisions: [docs/en/design_decisions.md](docs/en/design_decisions.md)
- Roadmap: [docs/en/roadmap.md](docs/en/roadmap.md)

---

## License

Licensed under **Apache-2.0** (see [LICENSE](LICENSE)); every source file carries the `SPDX-License-Identifier: Apache-2.0` header.

`lib/` and fetched bricks keep their own licenses (see [NOTICE](NOTICE)). Review before commercial use: libmodbus (LGPL), Mbed TLS (Apache-2.0 OR GPL-2.0), FatFs (ChaN's license).

---

## Acknowledgements

mini_tree's brick ecosystem builds on the open-source community (FreeRTOS, lwIP, LVGL, cJSON, littlefs, armink toolchain, MCUBoot, Mbed TLS, …).

Full credits: [docs/en/ecosystem.md](docs/en/ecosystem.md) §6. Corrections to credits or licenses welcome via Issue / PR.
