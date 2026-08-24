# mini_tree 使用手册

> 总手册入口：术语、阅读路线、文档地图。操作步骤见同目录专题；架构细节见 [architecture.md](architecture.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 首次接触本仓库的应用/平台工程师 |
| **前置** | 会用 CMake；不要求已读 Linux 内核 |
| **产出** | 知道该打开哪篇文档、核心名词含义 |

---

## 目录

1. [术语说明](#1-术语说明)
2. [推荐阅读路线](#2-推荐阅读路线)
3. [仓库里有什么](#3-仓库里有什么)
4. [专题文档地图](#4-专题文档地图)
5. [最小心智模型](#5-最小心智模型)

---

## 1. 术语说明

| 术语 | 含义 | 主要落点 |
| :--- | :--- | :--- |
| **DTS / DTSI** | Linux 风格设备树源文件 | 平台 `board/dts`、`board/dtsi`；中间件默认占位 `board/dts/board.dts` |
| **dtc-lite** | 编译期设备树编译器 | `tools/dtc-lite.py`、`tools/dtc_lite/` |
| **dt-bindings** | 仅 `#define` 的共享常量 | `board/dt-bindings/` |
| **DRIVER_REGISTER** | 驱动注册宏 → 静态 probe/remove 符号 | `board/include/driver.h` |
| **probe 表** | dtc-lite 生成的函数指针表 | `board_probe.c` / `board_devtable.*` |
| **HAL weak** | 中间件空实现，平台强符号覆盖 | `hal/*/*.c` |
| **硬件直投** | DTSI 宏值进入配置结构体，HAL 零翻译 | HAL 头字段注释 |
| **Bus poison** | 未定义 `*_BUS_IMPL` 时禁止调 `hal_*` | `bus/*/*_bus.h` |
| **VFS（本仓库）** | 设备 `file_operations` 层，**不是** Linux 内核 VFS | `vfs/*` |
| **OSAL** | 操作系统抽象三后端：裸机 / FreeRTOS v11.3.0 / RT-Thread v5.3.0 | `osal/` |
| **VIRQ** | 虚拟中断号 + 上/下半部 | `interrupt/` |
| **status / MINI_ERR_*** | 统一错误码 | `core/include/status.h` |
| **积木 / Brick** | 可选开源能力块（GUI/网络/FS…） | [ecosystem.md](ecosystem.md)；`mini_tree_link_*` |
| **vendor / Fetch** | 仅 FreeRTOS / RT-Thread / ETL 常驻 `lib/`；TinyUSB / lwIP 为配置期 FetchContent，其余链接期 | [ecosystem.md](ecosystem.md) §0 |
| **两段式点火** | pre-os → start-tasks → complete → 调度 | `system_init.h` / `system_init.hpp` |

---

## 2. 推荐阅读路线

### 路径 A — 平台移植（把板子跑起来）

1. [getting_started.md](getting_started.md) — 配置与 CMake
2. [device_tree_porting.md](device_tree_porting.md) — HAL + DTS 清单
3. [driver_guide.md](driver_guide.md) — compatible / 属性契约
4. [osal_switching.md](osal_switching.md) — 选定 RTOS
5. [faq.md](faq.md) · [problem_summary.md](problem_summary.md)

### 路径 B — 应用开发（写业务）

1. [getting_started.md](getting_started.md) § 点火时序
2. [service_spec.md](service_spec.md) — 允许/禁止依赖
3. [ecosystem.md](ecosystem.md) — 需要 GUI/网络/FS 时选积木
4. [fast_path.md](fast_path.md) — ISR / GPIO 红线
5. [architecture.md](architecture.md) § 数据流

### 路径 C — 改中间件本身

1. [architecture.md](architecture.md) · [design_decisions.md](design_decisions.md)
2. [file_index.md](file_index.md) · [CONTRIBUTING.md](../CONTRIBUTING.md)
3. [api_compatibility.md](api_compatibility.md) · [tools_guide.md](../tools_guide.md)

---

## 3. 仓库里有什么

| 区域 | 作用 | 是否含厂商 SDK |
| :--- | :--- | :---: |
| `docs/` | 专题文档（入口见 [README.md](README.md)） | 否 |
| `board` / `vfs` / `bus` / `hal` | 设备模型与外设栈 | HAL 实现 **否**（仅 weak） |
| `core` / `osal` / `interrupt` / `system_*` | 运行时基础设施 | OSAL 后端可选依赖 `lib/` 内核 |
| `tools` | dtc-lite、genconfig、gen_compile_db、menuconfig | 否 |
| `ide/stubs` | 无构建时的 clangd 占位头 | 否 |
| `lib/` | vendor：FreeRTOS / RT-Thread / ETL；TinyUSB / lwIP 为配置期 Fetch，其余链接期 | 开源积木，见 [ecosystem.md](ecosystem.md) |

---

## 4. 专题文档地图

精简地图如下；完整目录见 [docs/README.md](README.md)。

| 分组 | 文档 |
| :--- | :--- |
| 上手 | [getting_started](getting_started.md) · [faq](faq.md) · [keil_integration](keil_integration.md) |
| 生态 | [ecosystem](ecosystem.md)（积木 / Fetch） · [architecture](architecture.md) |
| 移植 | [device_tree_porting](device_tree_porting.md) · [driver_guide](driver_guide.md) · [usb_tusb_port](usb_tusb_port.md) · [amp](amp.md) · [osal_switching](osal_switching.md) |
| 编码 | [service_spec](service_spec.md) · [peripherals](peripherals.md) · [runtime_services](runtime_services.md) · [can_hook](can_hook.md) · [fast_path](fast_path.md) · [api_compatibility](api_compatibility.md) |
| 诊断 | [debug_monitor](debug_monitor.md) · [problem_summary](problem_summary.md) |
| 选型 | [design_decisions](design_decisions.md) · [references](references.md) |
| 索引 | [file_index](file_index.md) · [roadmap](roadmap.md) · [todolist](todolist.md) |
| 工具 | [tools_guide](../tools_guide.md) |

---

## 5. 最小心智模型

一次完整 I/O 的数据流：

```text
业务 device_ioctl(dev, cmd, …)
  → board 持锁检查状态
  → vfs 驱动 fops
  → bus_*（若该外设有 bus）
  → hal_*（平台强符号）
  → 寄存器 / SDK
```

一次启动的两段式点火：

```text
mini_tree_pre_os_init()
  → （业务/平台可选准备）
mini_tree_start_tasks()      // 含 board_driver_probe_all
system_init_complete()
  → vTaskStartScheduler / rt_system_scheduler_start / mini_tree_system_loop（裸机）
```

---

## 相关文档

- [architecture.md](architecture.md)
- [README.md](../README.md)
- [getting_started.md](getting_started.md)
