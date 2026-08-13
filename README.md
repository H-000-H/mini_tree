# mini_tree (ESP-IDF 专用分支)

> ESP-IDF component build of mini_tree — the platform-agnostic middleware core.
>
> Using a Linux-style Device Tree & Driver Model to unify peripheral access; **this branch is a pure ESP-IDF component** — configuration, build and dependencies all go through the ESP-IDF (Kconfig / Component Manager) system. Bare-metal / other-RTOS support lives in the main mini_tree repo.

---

## Overview

mini_tree is a Linux-style Device Tree & Driver Model middleware. **This branch (`esp-mini-tree`) is the pure ESP-IDF component build**: it plugs into any ESP-IDF project as `components/mini_tree`, fully integrated with the IDF toolchain — `idf.py menuconfig` for Kconfig, `sdkconfig.h` for `CONFIG_*`, and the Component Manager / registry for third-party dependencies. Chip HAL, pinmux, and board DTS are supplied by your board project (`board_port.cmake` + `hal_<soc>`).

---

## Architecture

- **Device Tree**: DTS/DTSI compiled via `dtc-lite` into static probe tables; zero runtime string matching, zero parsing overhead.
- **Hardware Direct-Inject**: Vendor macros expand directly into config structs; no intermediate enum mapping.
- **Layer Isolation**: `app → board → vfs → bus → hal(weak) → vendor SDK`; strict layering with poison guard on `hal_*` in all public headers.
- **Two-Phase Boot**: Standardized boot pipeline `pre_os_init` → `start_tasks` → `system_init_complete` → scheduler or cooperative main loop.
- **ESP Integration**: `if(ESP_PLATFORM)` routes to `cmake/esp_idf.cmake` (`idf_component_register`); Kconfig injected via `Kconfig.projbuild`; HAL stubs compiled empty on ESP, strong `hal_*` supplied by the board component.

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

## OSAL

| Backend | Model | Dependency |
|:---|:---|:---|
| `CONFIG_OSAL_FREERTOS` (default) | Preemptive | ESP-IDF built-in FreeRTOS |
| `CONFIG_OSAL_NULL` | Cooperative Time-Slice | None (bare-metal fallback) |

On the ESP path, the default is **FreeRTOS** (pairs with the IDF built-in kernel, forced by `cmake/esp_idf.cmake`); the bare-metal backend (`CONFIG_OSAL_NULL`) is kept as a no-RTOS fallback.

---

## Runtime Services

- **EventBus** — Range subscription, ISR-safe post, seal-after-boot (on by default).
- **VIRQ** — Virtual IRQ blocks, top-half / bottom-half (SPSC deferred queue).
- **BufferPool** — Pooled static allocator; ring FIFO & double buffer.
- **Safe State** — Shutdown callbacks, watchdogs, flash scrubber (optional brick).
- **Production Log** — Black-box fault recording for field diagnostics.

---

## Build & Toolchain

- **ESP-IDF** — This branch is a pure ESP-IDF component. Add it under `components/mini_tree`; the IDF build auto-routes to `cmake/esp_idf.cmake` on `ESP_PLATFORM`.
- **Kconfig** — Configuration is fully managed by ESP-IDF: `Kconfig.projbuild` (`orsource Kconfig.mini_tree`) injects the mini_tree menu into `idf.py menuconfig`; all `CONFIG_*` are written into `sdkconfig.h`. No standalone kconfiglib / genconfig needed.
- **dtc-lite** — Lightweight DTS compiler (`pip install lark`), auto-generating probe tables & board headers.
- **Board injection** — `board_port.cmake` supplies `BOARD_DTS`, chip `-I/-D`, and extra driver scan dirs; `hal_<soc>` supplies strong HAL implementations (`WHOLE_ARCHIVE`).
- **Coding style** — `.clang-format` (Allman, no braces for single statements, one-line short functions, 4-space, 100 cols) + layered `.clang-tidy` (naming); recommended in `app/`, mandatory below.

---

## Ecosystem

Core stays lean; extend on demand:

> **Vendored in `lib/`:**
> **ETL** (heap-free C++ containers, always linked)
>
> Third-party bricks (TinyUSB, cJSON, LVGL, etc.) are pulled through the **ESP-IDF Component Manager / registry** — not vendored or FetchContent-managed in this branch.

---

## Getting Started

```bash
# In your ESP-IDF project
git clone https://github.com/H-000-H/mini_tree.git  # or add as a submodule / symlink under components/
ln -s path/to/mini_tree components/mini_tree

# Add the component dependency
idf.py add-dependency "h-000-h/mini_tree"   # or list it in idf_component.yml
idf.py build
```

Also required at board level: `board_port.cmake` (board DTS, chip `-I/-D`), strong-symbol `hal_*` (`hal_<soc>` component), and per-brick port headers where needed.

Step-by-step: [docs/en/getting_started.md](docs/en/getting_started.md) · ESP-IDF specifics: [docs/en/esp_idf_cmake.md](docs/en/esp_idf_cmake.md)

---

## Documentation

Root keeps only entry & legal files; all topics live in [`docs/`](docs/en/README.md).

| I want to… | Read |
| :--- | :--- |
| Get an overview | [docs/en/usage.md](docs/en/usage.md) |
| Integrate | [docs/en/getting_started.md](docs/en/getting_started.md) |
| ESP-IDF specifics | [docs/en/esp_idf_cmake.md](docs/en/esp_idf_cmake.md) |
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

`lib/` keeps its own licenses (see [NOTICE](NOTICE)).

---

## Acknowledgements

mini_tree's ecosystem builds on the open-source community (ETL, ESP-IDF, lwIP, LVGL, cJSON, littlefs, …).

Full credits: [docs/en/ecosystem.md](docs/en/ecosystem.md) §6. Corrections to credits or licenses welcome via Issue / PR.
