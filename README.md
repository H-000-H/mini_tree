# mini_tree: Pure Generic & Architecture-Isolated Embedded Middleware

![Version](https://img.shields.io/badge/version-v1.5.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![C/C++](https://img.shields.io/badge/standard-C%2B%2B23%20%7C%20C23-orange.svg)
![Platform](https://img.shields.io/badge/platform-ARM%20%7C%20RISC--V%20%7C%20POSIX-lightgrey.svg)

`mini_tree` 是一个面向嵌入式系统的通用中间件框架。通过三层解耦架构，在编译链接层隔离芯片 SDK 与上层业务代码，使核心逻辑可跨平台复用。

## 适用场景

| 底层环境 | 推荐用法 | mini_tree 的角色 |
|---------|---------|-----------------|
| 裸机 (Bare-Metal) | 推荐 | 提供 OSAL 抽象、EventBus、设备树 Probe、BufferPool 等基础设施 |
| FreeRTOS | 推荐 | 在 FreeRTOS 基础上补充设备模型、安全回路（WDT/Scrubber）、事件总线 |
| Zephyr / RT-Thread | 视情况选用 | 通过 OSAL 垫片接入 RTOS 内核，可将业务 Service 层迁移至此复用；驱动部分建议使用原生框架 |

## 集成内核版本

| 内核 | 版本 | 说明 |
|------|------|------|
| FreeRTOS | v2026.04.00 LTS | ARM Cortex-M3/M4F/M7, RISC-V RV32IMAC |
| RT-Thread | v5.2.2 | 标准微内核 |
| 裸机 | — | 无 RTOS，1ms SysTick 状态机 + 原子操作替代 IPC |

> **完整使用指南请移步 [USAGE.md](USAGE.md)** — 术语说明 + 各模块文档索引。
> **架构总览请移步 [ARCHITECTURE.md](ARCHITECTURE.md)** — 层次划分、核心数据流、启动时序、安全架构。
> **架构演进记录请移步 [NOTICE.md](NOTICE.md)** — 8 轮重构全记录、核心架构决策、安全防御层次。
> **API 兼容性声明请移步 [API_COMPATIBILITY.md](API_COMPATIBILITY.md)** — 接口稳定性等级。
> **文件索引请移步 [FILE_INDEX.md](FILE_INDEX.md)** — 完整文件列表与说明。

---

## 架构总览

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      用户工程 / 应用业务层 (Host Apps)                    │
│         main.cpp / 业务 Service / 外设控制 / 算法控制流                  │
├──────────────────────────────────────────────────────────────────────────┤
│                  mini_tree 核心中间件层 (Pure Middleware)                 │
│  ┌───────────────────┬───────────────────┬────────────────────────────┐  │
│  │   core/           │   board/          │   system_(c/cpp)/          │  │
│  │   EventBus 总线   │   拟物化 VFS 树   │   两段式安全点火回路        │  │
│  │   无锁 BufferPool │   物理 Class 抽象 │   软看门狗 / 闪存扫描仪    │  │
│  └───────────────────┴───────────────────┴────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────────┤
│                      OSAL 操作系统抽象层 (OS Abstraction)                │
│      osal_freertos.c    │     osal_rtthread.c    │     osal_null.c      │
├──────────────────────────────────────────────────────────────────────────┤
│                   微内核与硬件芯片生态层 (Low-Level Engine)               │
│       FreeRTOS 内核     │     RT-Thread 内核     │   裸机 SysTick 驱动  │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 跨平台验证

已通过 10 种组合验证，详见 [ARCHITECTURE.md §7](ARCHITECTURE.md#7-跨平台验证矩阵)：

CMake 工具链：ARM GCC 13.3.1 / ARM Clang 18+ / RISC-V GCC 15.2.0 / MinGW 8.1.0 / ARMCLANG 6+
Makefile 工具链：ARM GCC / ARM Clang / RISC-V GCC / ARMCLANG (Keil 5/6)

---

## 示例工程

| 平台 | 工程 | 说明 |
|------|------|------|
| **STM32F407ZGT6** (ARM CM4F) | [stm32_test](https://github.com/H-000-H/mini-tree-example/tree/master/stm32_test) | FreeRTOS / RT-Thread / 裸机三后端切换测试，直操作寄存器，无 SDK |
| **ESP32-S3** (Xtensa LX7) | [esp32_test](https://github.com/H-000-H/mini-tree-example/tree/master/esp32_test) | ESP-IDF 组件集成，Kconfig 配置，hal_esp32s3.c 硬件适配，WS2812 驱动示例 |

### ESP32 端口特殊说明

ESP32 端口的 mini_tree 组件沿用原生 Kconfig 配置流程，通过 `components/mini_tree/Kconfig` 和 `tools/menuconfig.py` / `tools/genconfig.py` 管理编译期选项，不依赖 ESP-IDF 的 Kconfig 体系，不与 `sdkconfig` 冲突。构建使用 `idf_component_register()` 注册组件，非原生 `add_library` 写法。

配置方式：
```bash
cd components/mini_tree && python tools/menuconfig.py
```
或直接编辑 `components/mini_tree/.config`，`idf.py build` 时 `genconfig.py` 自动生成 `config.h`。

OSAL 默认 FreeRTOS（IDF 内置），语言默认 C++（物联网场景），均可通过 Kconfig 切换。

---

## 致谢与设计参考
本项目的设计深受以下开源项目与社区的启发：

- **[LVGL](https://github.com/lvgl/lvgl)** — 裸机状态机设计，EventBus 和 Service 生命周期管理参考了它的事件驱动模式。
- **[Linux 内核](https://github.com/torvalds/linux)** — 设备树自动 Probe 机制和分层抽象思想，贯穿了框架的整体设计。
- **[RT-Thread](https://github.com/RT-Thread/rt-thread)** — 设备驱动框架和 IPC 设计是重要的参考。
- **[Zephyr](https://github.com/zephyrproject-rtos/zephyr)** — OSAL 抽象层和模块化编译隔离的思路来源。
- **抖音 & B 站的嵌入式 up 主们** — 精致的项目演示视频提供了很多实践视角。
- **GitHub 上未能一一提及的开源项目** — 开发过程中参考了许多嵌入式项目的工程实践。

---

## License

本项目基于 [MIT License](LICENSE) 开源。欢迎任何形式的商业与非商业使用、修改及分发。
