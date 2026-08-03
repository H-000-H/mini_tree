# mini_tree

> 平台无关的嵌入式中间件 · Platform-agnostic embedded middleware
>
> 采用 Linux 风格设备树与驱动模型，统一裸机、FreeRTOS 及 RT-Thread 上的外设访问；不绑定任何厂商 SDK——芯片 HAL、引脚复用与板级 DTS 均由你的平台工程提供。
> Linux-style Device Tree & Driver Model unifying peripheral access across Bare-Metal, FreeRTOS, and RT-Thread; zero vendor SDK lock-in — chip HAL, pinmux, and board DTS are entirely supplied by your platform project.

---

## 简介 / Overview

平台无关的嵌入式中间件：采用 Linux 风格设备树与驱动模型，统一裸机、FreeRTOS 及 RT-Thread 上的外设访问。不绑定任何厂商 SDK——芯片 HAL、引脚复用与板级 DTS 均由你的平台工程提供。

Platform-agnostic embedded middleware using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal, FreeRTOS, and RT-Thread. Zero vendor SDK lock-in — chip HAL, pinmux, and board DTS are entirely supplied by your platform project.

---

## 架构特性 / Architecture

- **设备树 / Device Tree**：DTS/DTSI 编译期经 `dtc-lite` 展开为静态 probe 表，运行期零字符串匹配、零解析开销。
  Compiled via `dtc-lite` into static probe tables; zero runtime string matching, zero parsing overhead.
- **硬件直投 / Hardware Direct-Inject**：DTSI 厂商宏直接展开至配置结构体，无中间 enum 映射。
  Vendor macros expand directly into config structs; no intermediate enum mapping.
- **分层隔离 / Layer Isolation**：`app → board → vfs → bus → hal(weak) → vendor SDK`；公共头对 `hal_*` 实施 poison 保护。
  Strict layering with poison guard on `hal_*` in all public headers.
- **两段式引导 / Two-Phase Boot**：`pre_os_init` → `start_tasks` → `system_init_complete` → 调度器 / 主循环。
  Standardized boot pipeline ending at scheduler or cooperative main loop.

---

## 外设覆盖 / Peripheral Coverage

| 有总线层 / Bus-Based | 无总线层 / Bus-Less | 仅 HAL / HAL-Only |
|:---|:---|:---|
| SPI, I2C, I2S, UART, CAN, USB | GPIO, ADC, DAC, TIM, RTC, IWDG, WWDG | AMP/CPU, Storage, Platform Safety, SDIO |

---

## 产品驱动 / Product Drivers (37)

| 分类 / Category | 型号 / Chips & Modules |
|:---|:---|
| 传感器 / Sensors (12) | AHT20, BME280, BMP280, BH1750, SHT30, SHT40, MPU6050, INA219, ADS1115, DS18B20, VL53L0X, NEO-M8N |
| 显示 / Displays (5) | SSD1306, SH1106, ST7789, E-paper, MAX7219 |
| 触摸 / Touch (2) | FT5x06, XPT2046 |
| 通信 / Communication (7) | NRF24L01, SX1278 (LoRa), HC-05 (BT), SN65HVD230 (CAN), RS485 Modbus RTU, A7670 (4G), Air780E (4G) |
| 存储·NFC / Storage & NFC (4) | W25Qxx (SPI NOR), AT24C02 (EEPROM), PN532, RC522 |
| 执行器 / Actuators (6) | SG90, DRV8833, Relay, Buzzer, DFPlayer, MAX98357A |
| 其他 / Other (1) | PCF8574 (GPIO Expander) |

---

## 操作系统抽象 / OSAL — One API, Three Backends

| 后端 / Backend | 调度模型 / Model | 依赖 / Dependency |
|:---|:---|:---|
| `CONFIG_OSAL_NULL` | 协作式时间片 / Cooperative Time-Slice | 无 / None |
| `CONFIG_OSAL_FREERTOS` | 抢占式 / Preemptive | FreeRTOS v11.3.0 |
| `CONFIG_OSAL_RTTHREAD` | 抢占式 / Preemptive | RT-Thread v5.3.0 |

---

## 运行时服务 / Runtime Services

- **EventBus** — 区间订阅、ISR 安全投递、启动后封口 / Range subscription, ISR-safe post, seal-after-boot.
- **VIRQ** — 虚拟中断块 + 上/下半部（SPSC 延迟队列）/ Virtual IRQ blocks, top-half / bottom-half (SPSC deferred queue).
- **BufferPool** — 池化静态块分配；环形 FIFO、双缓冲 / Pooled static allocator; ring FIFO & double buffer.
- **Safe State** — 停机回调、IWDG/WWDG、Flash Scrubber CRC（可选积木）/ Shutdown callbacks, watchdogs, flash scrubber (optional brick).
- **Production Log** — 黑匣子式故障记录 / Black-box fault recording for field diagnostics.

---

## 构建与工具 / Build & Toolchain

- **CMake ≥ 3.16** — `add_subdirectory(mini_tree)` + `mini_tree_link_*` 按需链入积木；通用芯片无关路径 + ESP-IDF 组件路径。
  Modular on-demand linking via `mini_tree_link_*`; generic chip-agnostic path + ESP-IDF component path.
- **Kconfig** — `.config` → `genconfig.py` → `config.h`；`menuconfig.py` 可视化配置。
  Interactive configuration via `menuconfig.py`.
- **dtc-lite** — Python 轻量 DTS 编译器（`pip install lark`），生成 probe 表与板级头。
  Lightweight DTS compiler, auto-generating probe tables & board headers.
- **代码风格 / Coding style** — `.clang-format`（Allman、单语句去括号、短函数单行化、4 空格、100 列）+ 分层 `.clang-tidy`（命名强制）；app 层建议、app 以下强规定。
  `.clang-format` (Allman, no braces for single statements, one-line short functions, 4-space, 100 cols) + layered `.clang-tidy` (naming); recommended in `app/`, mandatory below.
- **目标架构 / Targets** — ARM Cortex-M0 / M0+ / M3 / M4F / M7, RISC-V 32-bit；双核 AMP。三个 OSAL 后端（裸机 / FreeRTOS / RT-Thread）均已覆盖。
  ARM Cortex-M0 / M0+ / M3 / M4F / M7, RISC-V 32-bit; dual-core heterogeneous AMP supported — covered by all three OSAL backends (Bare-Metal / FreeRTOS / RT-Thread).

---

## 生态 / Ecosystem

核心精瘦，扩展按需：

Core stays lean; extend on demand:

> **按需拉取 / FetchContent (on demand):**
> TinyUSB · lwIP · cJSON · LVGL · u8g2 · littlefs · FatFs · SFUD · Mbed TLS · coreMQTT · coreHTTP · nanopb · miniz · MCUBoot · FreeModbus · libmodbus · CMSIS-DSP · MultiButton · EasyFlash · EasyLogger · FlashDB

> **内置 / Vendored in `lib/`:**
> FreeRTOS · RT-Thread · **ETL**（无堆 C++ 容器，默认链入 / heap-free C++ containers, always linked）

---

## 快速开始 / Getting Started

```bash
git clone https://github.com/H-000-H/mini_tree.git
```

```cmake
add_subdirectory(path/to/mini_tree)

# 链上中间件核心 / Link the middleware core
target_link_libraries(your_firmware PUBLIC mini_tree)

# 按需点亮积木 / Opt-in bricks (examples)
# mini_tree_link_cjson(your_firmware)
# mini_tree_link_lwip(your_firmware "${CMAKE_CURRENT_SOURCE_DIR}/port")
```

板级还需：覆盖 DTS、实现强符号 `hal_*`、按积木提供 port 头（如 `lwipopts.h`、`lv_conf.h`）。
Also required at board level: board DTS, strong-symbol `hal_*`, and per-brick port headers (e.g. `lwipopts.h`, `lv_conf.h`).

逐步说明 / Step-by-step: [docs/getting_started.md](docs/getting_started.md)

---

## 文档 / Documentation

根目录只保留入口与开源惯例文件；专题都在 [`docs/`](docs/README.md)。
Root keeps only entry & legal files; all topics live in [`docs/`](docs/README.md).

| 你想… / I want to… | 去看 / Read |
| :--- | :--- |
| 建立整体印象 / Get an overview | [docs/usage.md](docs/usage.md) · [docs/overview.html](docs/overview.html) |
| 配进工程 / Integrate | [docs/getting_started.md](docs/getting_started.md) |
| 查命名与格式 / Coding style | [docs/coding_style.md](docs/coding_style.md) |
| 了解分层 / Architecture | [docs/architecture.md](docs/architecture.md) |
| 看积木清单 / Ecosystem | [docs/ecosystem.md](docs/ecosystem.md) |
| 移植一块板 / Port a board | [docs/porting_guide.md](docs/porting_guide.md) · [docs/driver_guide.md](docs/driver_guide.md) |
| 写应用 / Write apps | [docs/service_spec.md](docs/service_spec.md) · [docs/peripherals.md](docs/peripherals.md) |
| 查文件 / Find files | [docs/file_index.md](docs/file_index.md) |
| 常见问题 / FAQ | [docs/faq.md](docs/faq.md) |

工具链 / Toolchain: [tools/README.md](tools/README.md)

---

## 开发 / Development

欢迎 Issue 与 PR，贡献约定见 [CONTRIBUTING.md](CONTRIBUTING.md)。
Issues & PRs welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

- 变更记录 / Changelog: [CHANGELOG.md](CHANGELOG.md)
- 设计取舍 / Design decisions: [docs/design_decisions.md](docs/design_decisions.md)
- 路线图 / Roadmap: [docs/roadmap.md](docs/roadmap.md)

---

## 许可证 / License

本项目主体为 **Apache License 2.0**，全文见 [LICENSE](LICENSE)；每个源文件携带 `SPDX-License-Identifier: Apache-2.0` 头。
Licensed under **Apache-2.0** (see [LICENSE](LICENSE)); every source file carries the `SPDX-License-Identifier: Apache-2.0` header.

`lib/` 与 Fetch 积木遵循各自许可证，完整清单见 [NOTICE](NOTICE)。商用前请自行复核：libmodbus (LGPL)、Mbed TLS (Apache-2.0 OR GPL-2.0)、FatFs (ChaN's license)。
`lib/` and fetched bricks keep their own licenses (see [NOTICE](NOTICE)). Review before commercial use: libmodbus (LGPL), Mbed TLS (Apache-2.0 OR GPL-2.0), FatFs (ChaN's license).

---

## 致谢 / Acknowledgements

mini_tree 的积木生态建立在众多开源作者与社区之上（FreeRTOS、lwIP、LVGL、cJSON、littlefs、armink 工具链、MCUBoot、Mbed TLS……）。
mini_tree's brick ecosystem builds on the open-source community (FreeRTOS, lwIP, LVGL, cJSON, littlefs, armink toolchain, MCUBoot, Mbed TLS, …).

完整致谢表 / Full credits: [docs/ecosystem.md](docs/ecosystem.md) §6。若有署名或许可表述有误，欢迎提 Issue / PR 更正。
Corrections to credits or licenses welcome via Issue / PR.
