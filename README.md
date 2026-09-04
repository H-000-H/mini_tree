# mini_tree

> 平台无关的嵌入式中间件
> 采用 Linux 风格设备树与驱动模型，统一裸机 (Bare-Metal)、mini-os、FreeRTOS、RT-Thread 的外设访问接口；零厂商 SDK 绑定 —— 芯片 HAL、引脚复用及板级 DTS 完全由您的平台工程提供。

> Platform-agnostic embedded middleware
> Using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal, mini-os, FreeRTOS, and RT-Thread; zero vendor SDK lock-in — chip HAL, pinmux, and board DTS are entirely supplied by your platform project.

---

## 概述 / Overview

mini_tree 是一个平台无关的嵌入式中间件，采用 Linux 风格设备树与驱动模型，统一裸机 (Bare-Metal)、mini-os、FreeRTOS、RT-Thread 的外设访问接口。零厂商 SDK 绑定 —— 芯片 HAL、引脚复用及板级 DTS 完全由您的平台工程提供。

mini_tree is a platform-agnostic embedded middleware using a Linux-style Device Tree & Driver Model to unify peripheral access across Bare-Metal, mini-os, FreeRTOS, and RT-Thread. Zero vendor SDK lock-in — chip HAL, pinmux, and board DTS are entirely supplied by your platform project.

---

## 架构 / Architecture

- **设备树**：DTS/DTSI 经 `dtc-lite` 编译为静态探测表；零运行时字符串匹配，零解析开销。
- **硬件参数直注**：厂商宏直接展开到配置结构体；无中间枚举映射。
- **分层隔离**：`app → board → vfs → bus → hal(weak) → vendor SDK`；严格分层，所有公共头文件对 `hal_*` 设有毒化保护。
- **两阶段启动**：标准化启动流程 `pre_os_init` → `start_tasks` → `system_init_complete` → 调度器或协作式主循环。

- **Device Tree**: DTS/DTSI compiled via `dtc-lite` into static probe tables; zero runtime string matching, zero parsing overhead.
- **Hardware Direct-Inject**: Vendor macros expand directly into config structs; no intermediate enum mapping.
- **Layer Isolation**: `app → board → vfs → bus → hal(weak) → vendor SDK`; strict layering with poison guard on `hal_*` in all public headers.
- **Two-Phase Boot**: Standardized boot pipeline `pre_os_init` → `start_tasks` → `system_init_complete` → scheduler or cooperative main loop.

---

## 外设覆盖 / Peripheral Coverage

| 总线外设 / Bus-Based | 非总线外设 / Bus-Less | 仅 HAL / HAL-Only |
|:---|:---|:---|
| SPI、I2C、I2S、UART、CAN、USB | GPIO、ADC、DAC、TIM、RTC、IWDG、WWDG | AMP/CPU、Storage、Platform Safety、SDIO（预留 / HAL 槽位，尚未实现） |

---

## 产品驱动（39 个）/ Product Drivers (39)

| 类别 / Category | 芯片 & 模块 / Chips & Modules |
|:---|:---|
| 传感器 / Sensors (12) | AHT20、BME280、BMP280、BH1750、SHT30、SHT40、MPU6050、INA219、ADS1115、DS18B20、VL53L0X、NEO-M8N |
| 显示屏 / Displays (5) | SSD1306、SH1106、ST7789、电子墨水屏 / E-paper、MAX7219 |
| 触摸 / Touch (2) | FT5x06、XPT2046 |
| 通信 / Communication (7) | NRF24L01、SX1278 (LoRa)、HC-05 (BT)、SN65HVD230 (CAN)、RS485 Modbus RTU、A7670 (4G)、Air780E (4G) |
| 存储 & NFC / Storage & NFC (4) | W25Qxx (SPI NOR)、AT24C02 (EEPROM)、PN532、RC522 |
| 执行器 / Actuators (6) | SG90、DRV8833、继电器 / Relay、蜂鸣器 / Buzzer、DFPlayer、MAX98357A |
| 其他 / Other (1) | PCF8574 (GPIO 扩展器 / Expander) |

---

## OSAL — 一套 API，四种后端 / One API, Four Backends

| 后端 / Backend | 模型 / Model | 依赖 / Dependency |
|:---|:---|:---|
| `CONFIG_OSAL_NULL` | 协作式时间片 / 抢占式（裸机）/ Cooperative Time-Slice / Preemptive (bare-metal) | 无 / None |
| `CONFIG_OSAL_MINI_OS` | 抢占式（仅 Cortex-M）/ Preemptive (Cortex-M only) | `lib/mini-os`（自研，freestanding / in-tree, freestanding） |
| `CONFIG_OSAL_FREERTOS` | 抢占式 / Preemptive | FreeRTOS v11.3.0 |
| `CONFIG_OSAL_RTTHREAD` | 抢占式 / Preemptive | RT-Thread v5.3.0 |

裸机后端 (`CONFIG_OSAL_NULL`) 从 `Kconfig.mini_tree` "裸机调度器" 选择中选取一种调度器（`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`，默认 `XTASK_COOP`）。两种实现共享同一套 `xtask.h` API 表面，且在 CMake (`MINI_TREE_XTASK_*`) 和 `#ifdef` 层面互斥 —— 调用方代码透明切换：

The bare-metal backend (`CONFIG_OSAL_NULL`) picks one scheduler from the `Kconfig.mini_tree` "bare-metal scheduler" choice (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`, default `XTASK_COOP`). Both implementations share the same `xtask.h` API surface and are mutually exclusive at both CMake (`MINI_TREE_XTASK_*`) and `#ifdef` level — caller code switches transparently:

- `XTASK_NONE` — 无调度器；自行编写 `while(1)` 循环 / no scheduler; write your own `while(1)` loop
- `xtask_coop.c`（协作式 / 轮转，默认）/ (cooperative / round-robin, default) — `XTASK_COOP`
- `xtask_preempt.c`（抢占式，N+1 链表多优先级，已完成可编译）/ (preemptive, N+1 linked-list multi-priority, finished & compilable) — `XTASK_PREEMPT`

---

## 运行时服务 / Runtime Services

- **EventBus** — 范围订阅，ISR 安全投递，启动后密封。/ Range subscription, ISR-safe post, seal-after-boot.
- **VIRQ** — 虚拟中断块，上半部 / 下半部（SPSC 延迟队列）。/ Virtual IRQ blocks, top-half / bottom-half (SPSC deferred queue).
- **BufferPool** — 池化静态分配器；环形 FIFO 与双缓冲。/ Pooled static allocator; ring FIFO & double buffer.
- **Safe State** — 关机回调、看门狗、Flash 校验器（可选积木）。/ Shutdown callbacks, watchdogs, flash scrubber (optional brick).
- **Production Log** — 黑匣子故障记录，用于现场诊断。/ Black-box fault recording for field diagnostics.

---

## 构建与工具链 / Build & Toolchain

- **CMake ≥ 3.16** — `add_subdirectory(mini_tree)` + `mini_tree_link_*` 按需链接；通用芯片无关路径。/ on-demand linking; generic chip-agnostic path.
- **Kconfig** — `.config` → `genconfig.py` → `config.h`；通过 `menuconfig.py` 交互式配置。官方 kconfiglib（作者 Ulf Magnusson，ISC 许可证）已内置于 `tools/_vendor/` —— 无需 `pip install`；由 `tools/_vendor_loader.py` 前置到 `sys.path`。三个 `.py` 文件与上游保持同步，未做修改。/ interactive configuration via `menuconfig.py`. Official kconfiglib (by Ulf Magnusson, ISC license) is vendored under `tools/_vendor/` — no `pip install` needed; prepended to `sys.path` by `tools/_vendor_loader.py`. The three `.py` files stay in sync with upstream, unmodified.
- **dtc-lite** — 轻量级 DTS 编译器（`pip install lark`），自动生成探测表与板级头文件。/ Lightweight DTS compiler (`pip install lark`), auto-generating probe tables & board headers.
- **代码风格 / Coding style** — `.clang-format`（Allman、单语句无花括号、短函数单行、4 空格、200 列宽）+ 分层 `.clang-tidy`（命名）；`app/` 中推荐，其下层级强制。/ (Allman, no braces for single statements, one-line short functions, 4-space, 200 cols) + layered `.clang-tidy` (naming); recommended in `app/`, mandatory below.
- **目标平台 / Targets** — ARM Cortex-M0 / M0+ / M3 / M4F / M7、RISC-V 32 位；支持双核异构 AMP —— 裸机 / FreeRTOS / RT-Thread 后端全平台可覆盖，mini-os 后端覆盖 Cortex-M（详见 [docs/cn/mini-os.md](docs/cn/mini-os.md)）。/ ARM Cortex-M0 / M0+ / M3 / M4F / M7, RISC-V 32-bit; dual-core heterogeneous AMP supported — Bare-Metal / FreeRTOS / RT-Thread cover every target, while the mini-os backend covers Cortex-M (see [docs/en/mini-os.md](docs/en/mini-os.md)).

---

## 生态 / Ecosystem

核心保持精简；按需扩展：/ Core stays lean; extend on demand:

> **FetchContent（按需拉取）/ (on demand)：**
> TinyUSB · lwIP · cJSON · LVGL · u8g2 · littlefs · FatFs · SFUD · Mbed TLS · coreMQTT · coreHTTP · nanopb · miniz · MCUBoot · FreeModbus · libmodbus · CMSIS-DSP · MultiButton · EasyFlash · EasyLogger · FlashDB

> **内置于 `lib/` / Vendored in `lib/`：**
> **mini-os**（自研最小 RTOS 内核 / in-tree minimal RTOS kernel）· FreeRTOS · RT-Thread · **ETL**（无堆 C++ 容器，始终链接 / heap-free C++ containers, always linked）

---

## 快速开始 / Getting Started

```bash
git clone https://github.com/H-000-H/mini_tree.git
```

```cmake
add_subdirectory(path/to/mini_tree)

# 链接中间件核心 / Link the middleware core
target_link_libraries(your_firmware PUBLIC mini_tree)

# 按需启用积木（示例）/ Opt-in bricks (examples)
# mini_tree_link_cjson(your_firmware)
# mini_tree_link_lwip(your_firmware "${CMAKE_CURRENT_SOURCE_DIR}/port")
```

板级还需提供：板级 DTS、`hal_*` 强符号实现，以及各积木的移植头文件（如 `lwipopts.h`、`lv_conf.h`）。

Also required at board level: board DTS, strong-symbol `hal_*`, and per-brick port headers (e.g. `lwipopts.h`, `lv_conf.h`).

分步指南 / Step-by-step: [docs/en/getting_started.md](docs/en/getting_started.md)

---

## ESP-IDF (ESP32)

`main` 分支保留了完整的 ESP-IDF 构建路径（`cmake/esp_idf.cmake`、`Kconfig.projbuild`、`idf_component.yml`），但不能像普通平台一样直接 `add_subdirectory`——需要走 IDF 组件路径（由 `ESP_PLATFORM` 触发）。

The `main` branch keeps the full ESP-IDF build path (`cmake/esp_idf.cmake`, `Kconfig.projbuild`, `idf_component.yml`), but it cannot be used with a plain `add_subdirectory` like other platforms — it requires the IDF component path (triggered by `ESP_PLATFORM`).

获取 ESP 版本二选一 / Two ways to get the ESP version:

- **`esp` 分支 / branch**（完整移植工程 + 全部板级代码 / full port project + all board-level code）：
  ```bash
  git clone -b espidf-branch https://github.com/H-000-H/mini_tree.git
  ```
- **乐鑫组件注册表 / ESP-IDF Component Registry**：在 `idf_component.yml` 中声明 / declare in `idf_component.yml`
  ```yaml
  dependencies:
    h-000-h/mini_tree: ">=1.2.0"
  ```
  或在工程目录执行 / or run `idf.py add-dependency "h-000-h/mini_tree"` in the project directory to let `idf-component-manager` 自动拉取 / fetch it automatically.

ESP 完整移植指南见 `esp` 分支的 `docs/` 或 / See the `esp` branch `docs/` or [getting_started.md](docs/en/getting_started.md) §4.2 for the full ESP porting guide.

---

## 文档 / Documentation

根目录仅保留入口与法律文件；所有主题见 [`docs/`](docs/en/README.md)。

Root keeps only entry & legal files; all topics live in [`docs/`](docs/en/README.md).

| 我想… / I want to… | 阅读 / Read |
| :--- | :--- |
| 了解概览 / Get an overview | [docs/en/usage.md](docs/en/usage.md) |
| 集成使用 / Integrate | [docs/en/getting_started.md](docs/en/getting_started.md) |
| 代码风格 / Coding style | [docs/en/coding_style.md](docs/en/coding_style.md) |
| 架构设计 / Architecture | [docs/en/architecture.md](docs/en/architecture.md) |
| 生态集成 / Ecosystem | [docs/en/ecosystem.md](docs/en/ecosystem.md) |
| 移植板级 / Port a board | [docs/en/device_tree_porting.md](docs/en/device_tree_porting.md) · [docs/en/driver_guide.md](docs/en/driver_guide.md) |
| 编写应用 / Write apps | [docs/en/service_spec.md](docs/en/service_spec.md) · [docs/en/peripherals.md](docs/en/peripherals.md) |
| 查找文件 / Find files | [docs/en/file_index.md](docs/en/file_index.md) |
| 常见问题 / FAQ | [docs/en/faq.md](docs/en/faq.md) |

中文文档 / Chinese docs: [docs/cn/README.md](docs/cn/README.md)

工具链指南 / Toolchain: [docs/cn/tools_guide.md](docs/cn/tools_guide.md) · [docs/en/tools_guide.md](docs/en/tools_guide.md)

---

## 开发 / Development

欢迎提交 Issue 和 PR —— 详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

Issues & PRs welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

- 变更日志 / Changelog: [CHANGELOG.md](CHANGELOG.md)
- 设计决策 / Design decisions: [docs/en/design_decisions.md](docs/en/design_decisions.md)
- 路线图 / Roadmap: [docs/en/roadmap.md](docs/en/roadmap.md)

---

## 许可证 / License

基于 **Apache-2.0** 许可证（见 [LICENSE](LICENSE)）；每个源文件均带有 `SPDX-License-Identifier: Apache-2.0` 头部。

Licensed under **Apache-2.0** (see [LICENSE](LICENSE)); every source file carries the `SPDX-License-Identifier: Apache-2.0` header.

`lib/` 及拉取的积木保留各自许可证（见 [NOTICE](NOTICE)）。商业使用前请审查：libmodbus (LGPL)、Mbed TLS (Apache-2.0 OR GPL-2.0)、FatFs (ChaN 许可证)。

`lib/` and fetched bricks keep their own licenses (see [NOTICE](NOTICE)). Review before commercial use: libmodbus (LGPL), Mbed TLS (Apache-2.0 OR GPL-2.0), FatFs (ChaN's license).

---

## 致谢 / Acknowledgements

mini_tree 的积木生态系统建立在开源社区之上（FreeRTOS、lwIP、LVGL、cJSON、littlefs、armink 工具链、MCUBoot、Mbed TLS 等）。

mini_tree's brick ecosystem builds on the open-source community (FreeRTOS, lwIP, LVGL, cJSON, littlefs, armink toolchain, MCUBoot, Mbed TLS, …).

完整致谢 / Full credits: [docs/en/ecosystem.md](docs/en/ecosystem.md) §6。欢迎通过 Issue / PR 更正致谢或许可证信息。/ Corrections to credits or licenses welcome via Issue / PR.
