# mini_tree: Pure Generic & Architecture-Isolated Embedded Middleware

![Version](https://img.shields.io/badge/version-v1.5.0-blue.svg)
![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![C/C++](https://img.shields.io/badge/standard-C%2B%2B17%20%7C%20C17-orange.svg)
![Platform](https://img.shields.io/badge/platform-ARM%20%7C%20RISC--V%20%7C%20Xtensa-lightgrey.svg)

`mini_tree` 是一个面向嵌入式系统的通用中间件框架。通过 **VFS → Bus → HAL** 三层解耦架构，在编译链接层隔离芯片 SDK 与上层业务代码，使核心逻辑可跨平台复用。

**架构基准**：以 **ARM Cortex-M 系列** 与 **RISC-V RV32** 为通用基准，覆盖裸机 / FreeRTOS / RT-Thread 三种 OSAL 后端。主开发与验证环境为 **Linux**（原生 / WSL / Docker），同时作为通用中间件天然跨平台，**Windows 原生编译**同样可用。ESP32（Xtensa LX）作为异构架构，通过 **原生 Linux / Windows** 工具链接入（ESP-IDF 官方双端支持，不走 Docker），不作为通用基准。

| 架构基准 | 主平台 | 也支持 (中间件跨平台) | 工具链 |
|----------|--------|----------------------|--------|
| ARM Cortex-M（通用基准） | Linux (原生/WSL/Docker) | Windows 原生 | ARM GCC / ARM Clang |
| RISC-V RV32（通用基准） | Linux (原生/WSL/Docker) | Windows 原生 | RISC-V GCC |
| ESP32 Xtensa LX（异构架构） | Linux | Windows (ESP-IDF 官方双端) | Xtensa GCC（随 ESP-IDF） |

## 适用场景

| 底层环境 | 推荐用法 | mini_tree 的角色 |
|---------|---------|-----------------|
| 裸机 (Bare-Metal) | 推荐 | 提供 OSAL 抽象、EventBus、设备树 Probe、BufferPool 等基础设施 |
| FreeRTOS | 推荐 | 在 FreeRTOS 基础上补充设备模型、安全回路（WDT/Scrubber）、事件总线 |
| Zephyr / RT-Thread | 视情况选用 | 通过 OSAL 垫片接入 RTOS 内核，可将业务 Service 层迁移至此复用；驱动部分建议使用原生框架 |

## 集成内核版本

| 内核 | 版本 | 说明 |
|------|------|------|
| FreeRTOS | v2026.04.00 LTS | ARM Cortex-M3/M4F/M7, RISC-V RV32IMAC, Xtensa LX6/LX7 (ESP-IDF SMP) |
| RT-Thread | v5.2.2 | 标准微内核 |
| 裸机 | — | 无 RTOS，1ms SysTick 状态机 + 原子操作替代 IPC |

> **完整使用指南请移步 [USAGE.md](USAGE.md)** — 术语说明 + 各模块文档索引。
> **架构总览请移步 [ARCHITECTURE.md](ARCHITECTURE.md)** — 层次划分、核心数据流、启动时序、安全架构。
> **架构演进记录请移步 [NOTICE.md](NOTICE.md)** — 重构全记录、核心架构决策、安全防御层次。
> **API 兼容性声明请移步 [API_COMPATIBILITY.md](API_COMPATIBILITY.md)** — 接口稳定性等级。
> **文件索引请移步 [FILE_INDEX.md](FILE_INDEX.md)** — 完整文件列表与说明。

---

## 架构总览

mini_tree 在物理上将"芯片 SDK / RTOS"与"应用业务"隔离，外设 I/O 路径采用 **VFS → Bus → HAL** 三层解耦：VFS 负责设备框架与 DTS 解析，Bus 负责 host/client 池与 I/O 分派，HAL 负责硬件直投（零翻译透传厂商 LL 库 / ESP-IDF driver）。

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      用户工程 / 应用业务层 (Host Apps)                    │
│         main.cpp / 业务 Service / 外设控制 / 算法控制流                  │
│   ─── 仅依赖 mini_tree 接口库 (device_find_by_label/device_open/...) ──  │
├──────────────────────────────────────────────────────────────────────────┤
│  VFS 层 (vfs/)                                平台中立                    │
│    file_operations 挂载 + dev_lifecycle 生命周期 + DTS 解析               │
│    spi_vfs.{c,h} │ uart_vfs.{c,h} │ vfs-gpio.{c,h}                        │
│    I/O 全部走 bus 层, 不直接调 HAL                                         │
├──────────────────────────────────────────────────────────────────────────┤
│  Bus 层 (bus/, board/src/bus.c)               平台中立                    │
│    host/client 静态池 + atomic ref_count + I/O 分派                       │
│    spi_bus.{c,h} │ uart_bus.{c,h} │ dma_core.c / dma.h / dma_internal.h   │
│    直接调 HAL 函数 (无 vtable, 无 ops 表)                                  │
├──────────────────────────────────────────────────────────────────────────┤
│  HAL 层 (hal/)                                头平台中立, .c 在平台目录    │
│    头只用 uintptr_t/int/void*, 不暴露 vendor 类型                          │
│    统一头: hal_spi.h / hal_uart.h / hal_gpio.h (STM32/WCH/ESP32 共用)      │
│    顶层 .c: hal_if_dummy.c / hal_pwm.c / hal_cpu_amp.c                    │
│    平台 .c: hal_spi_stm32.c / hal_spi_ch32.c / hal_spi.c (ESP32) 等        │
│    DTSI 直投厂商宏值, 零翻译透传 LL 库 / 标准外设库 / ESP-IDF driver      │
├──────────────────────────────────────────────────────────────────────────┤
│            厂商 SDK (STM32 LL / WCH 标准库 / ESP-IDF driver)              │
├──────────────────────────────────────────────────────────────────────────┤
│                  mini_tree 核心中间件层 (Pure Middleware)                 │
│  ┌───────────────────┬───────────────────┬────────────────────────────┐  │
│  │   core/           │   board/          │   system_(c/cpp)/          │  │
│  │   EventBus 总线   │   设备框架/Probe  │   两段式安全点火回路        │  │
│  │   无锁 BufferPool │   dev_lifecycle   │   软看门狗 / 闪存扫描仪    │  │
│  └───────────────────┴───────────────────┴────────────────────────────┘  │
│  ┌───────────────────┬───────────────────┬────────────────────────────┐  │
│  │   osal/           │   drivers/        │   algorithm/               │  │
│  │   三后端抽象      │   w25q64 等外设    │   环形 FIFO                │  │
│  └───────────────────┴───────────────────┴────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────┤
│                      OSAL 操作系统抽象层 (OS Abstraction)                │
│      osal_freertos.c    │     osal_rtthread.c    │     osal_null.c      │
├──────────────────────────────────────────────────────────────────────────┤
│                   微内核与硬件芯片生态层 (Low-Level Engine)               │
│       FreeRTOS 内核     │     RT-Thread 内核     │   裸机 SysTick 驱动  │
└──────────────────────────────────────────────────────────────────────────┘
```

### 三层解耦契约

| 依赖方向 | 约束 | 隔离手段 |
|---------|------|---------|
| 应用层 → VFS 层 | 应用通过 `device_find_by_label` / `device_open` / `device_ioctl` 访问设备 | 仅依赖 `device.h` 接口 |
| VFS 层 → Bus 层 | VFS 解析 DTS、挂 `file_operations`、维护 `dev_lifecycle`；I/O 全部转发 bus | `#pragma GCC poison` 禁止 VFS 直接调 HAL |
| Bus 层 → HAL 层 | bus 维护 host/client 静态池 + atomic ref_count；I/O 直接调 HAL 函数 | `#pragma GCC poison` 禁止 bus 层外部调 HAL |
| HAL 层 → 厂商 SDK | 头平台中立（`uintptr_t/int/void*`），.c 内部 include vendor 头，零翻译透传 | 头不暴露 vendor 类型 |

### 硬件直投模式

HAL 层采用**硬件直投**，无 vtable、无 ops 表、直接函数调用：

- **统一头结构体**（STM32/WCH/ESP32 共用）：
  - `hal_spi_pin_cfg { uintptr_t port, uint16_t pin, uint32_t clk_periph, uint32_t af }`
  - `hal_uart_pin_cfg { uintptr_t port, uint16_t pin, uint32_t clk_periph, uint32_t af }`
  - `hal_gpio_obj_t { uintptr_t port, uint16_t pin, uint32_t clk_periph, bool is_used }`
- **HAL 对象嵌入 bus 层**（非指针）：`hal_spi_bus_host` 嵌入 `spi_bus_host`，`hal_uart_dev` 嵌入 `uart_bus_host`，`hal_gpio_obj_t` 嵌入 VFS priv。HAL 无池管理、无 alloc/free。
- **DTSI 直投厂商宏值**：STM32 LL 库宏 / WCH 标准库宏 / ESP-IDF 枚举直接写入 DTSI，HAL .c 零计算灌入 LL 库 / ESP-IDF driver。
- **ESP32 适配**：`port=0, clk_periph=0, af=0, pin=SoC GPIO 编号`（无 AF 概念）。
- **container_of** 用于从 `file_operations ops` 指针获取 VFS 私有数据。

### 统一 compatible strings（无平台前缀）

| compatible | 含义 |
|------------|------|
| `spi-master` | SPI master host controller |
| `spi-slave` | SPI slave host controller |
| `uart` | UART host |
| `uart-client` | UART client |
| `heterogeneous,gpios` | GPIO |
| `heterogeneous,spi-master-client` | SPI master bus client |
| `heterogeneous,fft-spi-slave` | SPI slave client |
| `*-platform-cap` | 平台能力节点 |

---

## 跨平台验证

主环境为 Linux，详见 [ARCHITECTURE.md §7](ARCHITECTURE.md#7-跨平台验证矩阵)：

CMake 工具链：Linux ARM GCC 14.2.1 / Docker ARM GCC 14.2.1 / Windows 原生 ARM GCC 13.3.1 (STM32CubeCLT) ；RISC-V GCC (WCH 工具链) / Windows 原生 RISC-V GCC 15.2.0 (WCH MounRiver Studio) ；Xtensa GCC (ESP-IDF) / MinGW 8.1.0 (host test)

> **跨平台验证说明**：本架构以 ARM Cortex-M 与 RISC-V RV32 为通用基准，编译验证基于主机侧的多工具链混合测试（ARM GCC / ARM Clang / RISC-V GCC 等），统一通过 CMake 构建系统管理。作为通用中间件，核心层与环境无关，Windows 同样可编译运行。具体板级运行可能因芯片 errata、启动代码差异或外设配置引入额外问题，期待社区开发者在实际硬件上的测试反馈与贡献。

### 当前构建验证状态（硬件直投重构后）

| 平台 | 架构 | 板级 | 工具链 | 状态 |
|------|------|------|--------|------|
| WCH CH32V307 | RISC-V RV32 | mini_tree 副本 | RISC-V GCC (WCH) | 通过 |
| ST STM32F407ZGT6 | ARM Cortex-M4F | mini_tree 副本 | ARM GCC 14.2.1 | 编译中 |
| Espressif ESP32-S3 | Xtensa LX7 | components/mini_tree | Xtensa GCC (ESP-IDF) | 待验证 |

### 三平台参考实现

| 节点 | 架构 | 平台目录 | HAL .c 实现 |
|------|------|----------|------------|
| STM32F407ZGT6 | ARM Cortex-M4F | `platform/STMicroelectronics/stm32f407zgt6/` | `hal_spi_stm32.c` / `hal_uart_stm32.c` / `hal_gpio_stm32.c` 等 |
| CH32V307 | RISC-V RV32 | `platform/WCH/ch32v307/` | `hal_spi_ch32.c` / `hal_uart_ch32.c` / `hal_gpio_ch32.c` 等 |
| ESP32-S3 | Xtensa LX7 | `platform/Espressif/esp32s3/` | `hal_spi.c` / `hal_uart.c` / `hal_gpio_esp32.c` 等 |

每平台目录下有 `mini_tree/` 副本（平台中立文件与顶层一致），HAL .c 实现在各平台自己的 `hal/` 子目录中。

### ESP32 异构架构适配

ESP32（Xtensa LX6/LX7）与 ARM/RISC-V 在中断模型、FPU、启动流程、SDK 体系上差异显著，**不作为通用基准**，按以下策略接入：

- **构建路径**：ESP32 主环境走 **Linux 原生工具链**（`idf.py` + Xtensa GCC），同时 **Windows 原生也支持**（ESP-IDF 官方双端），不走 Docker
- **RTOS 后端**：直接使用 Esp-IDF 自带的 SMP FreeRTOS，不编译本仓库 `lib/freeRTOS/` 中的 ARM/RISC-V 汇编端口
- **组件集成**：通过 `idf_component_register()` 注册 `components/mini_tree`，沿用原生 Kconfig 配置流程（`tools/menuconfig.py` + `tools/genconfig.py`），与 `sdkconfig` 不冲突
- **裸机后端**（`OSAL_NULL`）无 RTOS 端口限制，可正常使用
- **DMA 适配**：统一 DMA API `hal_uart_write_dma` / `hal_uart_dma_abort`，ESP32 返回 `VFS_ERR_NOTSUPP`

配置示例：
```bash
cd components/mini_tree && python tools/menuconfig.py
# 或直接编辑 components/mini_tree/.config，idf.py build 时 genconfig.py 自动生成 config.h
```

---

## 致谢与设计参考
本项目的设计深受以下开源项目与社区的启发：

- **[LVGL](https://github.com/lvgl/lvgl)** — 裸机状态机设计，EventBus 和 Service 生命周期管理参考了它的事件驱动模式。
- **[Linux 内核](https://github.com/torvalds/linux)** — 设备树自动 Probe 机制、VFS 文件操作抽象和分层解耦思想，贯穿了框架的整体设计。
- **[RT-Thread](https://github.com/RT-Thread/rt-thread)** — 设备驱动框架和 IPC 设计是重要的参考。
- **[Zephyr](https://github.com/zephyrproject-rtos/zephyr)** — OSAL 抽象层和模块化编译隔离的思路来源。
- **[Espressif ESP-IDF](https://github.com/espressif/esp-idf)** — ESP32 移植的参考平台，其 FreeRTOS SMP 实现和组件化构建系统为多平台适配提供了实践验证。
- **抖音 & B 站的嵌入式 up 主们** — 精致的项目演示视频提供了很多实践视角。
- **GitHub 上未能一一提及的开源项目** — 开发过程中参考了许多嵌入式项目的工程实践。

---

## License

本项目基于 [Apache License 2.0](LICENSE) 开源。所有源文件头部统一标注 `SPDX-License-Identifier: Apache-2.0`。欢迎任何形式的商业与非商业使用、修改及分发。
