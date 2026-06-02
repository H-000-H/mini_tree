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

## License

本项目基于 [MIT License](LICENSE) 开源。欢迎任何形式的商业与非商业使用、修改及分发。
