# mini_tree 架构总览

> 当前中间件 shelf 的分层、启动、数据流与安全模型。不绑定厂商 SDK。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 需要改中间件或做深度移植的工程师 |
| **前置** | 已读 [usage.md](usage.md) 术语表 |
| **相关** | [design_decisions.md](design_decisions.md) · [file_index.md](file_index.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md) |

---

## 目录

1. [分层拓扑与契约](#1-分层拓扑与契约)
2. [模块职责](#2-模块职责)
3. [启动时序（两段式点火）](#3-启动时序两段式点火)
4. [核心数据流](#4-核心数据流)
5. [配置与生成物](#5-配置与生成物)
6. [中断模型](#6-中断模型)
7. [安全架构](#7-安全架构)
8. [跨平台边界](#8-跨平台边界)

---

## 1. 分层拓扑与契约

分层拓扑（自顶向下调用，横向为基础能力）：

```
┌─────────────────────────────────────────────────────────────┐
│ Application / 平台业务                                       │
│   device_open/read/write/ioctl · EventBus · osal_task_*     │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ board/   device · driver · bus.h · lifecycle · config_store │
│          device_tree_init · board_driver_probe_all          │
└────────────────────────────┬────────────────────────────────┘
                             │ DRIVER_REGISTER → dtc-lite 表
┌────────────────────────────▼────────────────────────────────┐
│ vfs/     外设 file_operations（持 device 锁 / lifecycle）    │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ bus/     host/client 池 · 引用计数 · 对上层 poison hal_*     │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ hal/     平台无关头 + weak 空 .c                             │
└────────────────────────────┬────────────────────────────────┘
                             │ 仅平台 .c include 厂商头
┌────────────────────────────▼────────────────────────────────┐
│ 厂商 SDK / LL / 标准外设库 / ESP-IDF …（不在本仓库）           │
└─────────────────────────────────────────────────────────────┘
```

横向: core · osal · interrupt · system_c|cpp · time_slice · can_hook · net · ui · tools

### 1.1 层间契约

| 边界 | 允许 | 禁止 |
| :--- | :--- | :--- |
| App → board | `device_*`、业务 ioctl | `hal_*`、厂商头 |
| vfs → bus | `*_bus_*` | 直接 `hal_*`（头文件 poison） |
| bus → hal | 调用 HAL；`.c` 可定义 `*_BUS_IMPL` | 把厂商类型泄漏到公共头 |
| HAL 头 | `uintptr_t` / 整型宏字段 | `SPI_HandleTypeDef` 等 typedef |

### 1.2 硬件直投

DTSI 中 `#include <厂商头>` → `cpp` 展开 → 属性写成整数 → VFS 填入 `hal_*_config` → 平台 HAL 灌入 LL/驱动。中间件**不**维护「Linux 式 enum → 厂商 enum」映射表。

---

## 2. 模块职责

| 目录 | 职责 | 典型符号 |
| :--- | :--- | :--- |
| `board/` | 设备实例、probe 序、生命周期、配置聚合 | `device_find`、`DRIVER_REGISTER` |
| `vfs/` | 按 compatible 绑定的驱动 | `vfs_spi_probe`、uart/can/usb… |
| `bus/` | 控制器 host + client 会话 | `spi_bus_open`、`can_bus_transmit` |
| `hal/` | 抽象寄存器操作 | `hal_gpio_fast_set_level`、`hal_uart_write` |
| `core/` | 错误码、兼容宏、事件、缓冲池、日志 | `VFS_ERR_*`、`event_bus_*` |
| `osal/` | 锁/队列/任务/时间 | `osal_mutex_lock` |
| `interrupt/` | VIRQ、上/下半部 | `interrupt_virtual_dispatch` |
| `system_c` / `system_cpp` | 启动、WDT、scrubber、safe_state | `mini_tree_pre_os_init` / `mini_tree::system_pre_os_init` |
| `time_slice/` | 裸机调度 — 由 Kconfig 三态 choice (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`) 选择; 协调式 (`xtask_coop.c`, 默认) 与抢占式 (`xtask_preempt.c`, N+1 多优先级) 二选一, 共用 `xtask.h` API; CMake + `#ifdef` 双重互斥; 仅 `OSAL_NULL` | `x_scheduler` / `x_task` |
| `drivers/<chip>/` | 产品驱动（37 个，`{include,src}` 结构） | `DRIVER_REGISTER` / ioctl；dtc-lite 编译期 probe |
| `can_hook/` | CAN 钩子扩展 | — |
| `net/` | 网络协议栈胶水（MQTT 客户端 / PPP 网卡 / USB 网卡），经 lwIP + coreMQTT 等积木，只经 device/VFS 模型触硬件 | `mqtt_client_*`、`pppif_*`、`usbethif_*` |
| `ui/` | UI 库胶水层（LVGL / u8g2 显示桥接），只经 device/VFS 模型（`DISPLAY_CMD_*`）触显示硬件 | `display_lvgl_flush_callback`、`display_u8g2_flush_frame_buffer` |
| `lib/` + `cmake/*.cmake` | vendor：FreeRTOS / RT-Thread / ETL；TinyUSB / lwIP 为配置期 FetchContent，其余积木链接期 FetchContent | OSAL 内核按 Kconfig；其余 `mini_tree_link_*`（见 [ecosystem.md](ecosystem.md)） |

### 2.1 外设覆盖（当前）

| 能力 | HAL | Bus | VFS |
| :---: | :---: | :---: | :---: |
| GPIO | ✓ | — | ✓ |
| SPI / UART / I2C / I2S | ✓ | ✓ | ✓ |
| CAN / USB | ✓ | ✓ | ✓ |
| ADC / DAC / TIM | ✓ | — | ✓ |
| RTC / IWDG / WWDG | ✓ | — | ✓ |
| AMP / storage / platform_safety | ✓ | — | — |

---

## 3. 启动时序（两段式点火）

### 3.1 C API（`system_c/include/system_init.h`）

| 阶段 | API | 典型工作 |
| :---: | :--- | :--- |
| 1 | `mini_tree_pre_os_init()` | 关全局中断、EventBus、safe_state、可选 WDT、`device_tree_init` |
| — | （可选）业务/平台准备 | 静态配置、额外注册 |
| 2 | `mini_tree_start_tasks()` | `board_driver_probe_all`、TWDT、Flash Scrubber |
| 3 | `system_init_complete()` | 释放全局中断 |
| 4 | 调度或裸机循环 | `vTaskStartScheduler` / `rt_system_scheduler_start` / `mini_tree_system_loop` |

### 3.2 C++ API（`system_cpp`）

`mini_tree::system_pre_os_init()` / `mini_tree::system_start_tasks()` 与上表阶段 1/2 对应，最后仍调用 `system_init_complete()`；另有 `extern "C"` 包装 `mini_tree_pre_os_init()` / `mini_tree_start_tasks()` 供 C 侧调用。

### 3.3 Probe 协作

dtc-lite 扫描 `DRIVER_REGISTER` 并生成 probe/remove 表，`board_driver_probe_all` 按序执行：

```text
dtc-lite 扫描 DRIVER_REGISTER
  → 生成 probe/remove 表 + board_probe_order()
board_driver_probe_all()
  → 按序取 device → 调匹配的 probe_fn
  → 失败时按 criticality 告警或安全停机
```

---

## 4. 核心数据流

### 4.1 外设 I/O

```text
device_read/write/ioctl
  → 持 device->lock（框架包装）
  → ops->read/write/ioctl (vfs)
  → *_bus_* （有 bus 的外设）
  → hal_* （平台实现）
```

### 4.2 EventBus

`core` 发布订阅；模块开关 `CONFIG_EVENT_BUS`（默认关闭，依赖 `SYSTEM`），容量由 `CONFIG_EVENT_BUS_QUEUE_LEN`、`CONFIG_EVENT_BUS_MAX_SUBSCRIBERS` 决定。

### 4.3 BufferPool / algorithm

`buffer_pool` 提供池化块；`algorithm/buffer` 提供环形/双缓冲等结构，供驱动与业务复用。

### 4.4 错误与指针

- 返回值：`int`，`MINI_OK` 或负的 `VFS_ERR_*`
- 特殊指针：`ERR_PTR` / `IS_ERR` / `PTR_ERR`（依赖 `error_symbols.ld` 的 `ERR_SECTION_BASE`）

---

## 5. 配置与生成物

| 输入 | 工具 | 输出目录（典型） |
| :--- | :--- | :--- |
| `Kconfig` + `.config` | `tools/genconfig.py` | `generated/kconfig/mini_tree/config.h` |
| `BOARD_DTS` + dtsi + `VENDOR_INC_*` | `tools/dtc-lite.py` | `generated/board/mini_tree/*` |
| scrubber stub | CMake copy | `generated/scrubber/.../system_scrubber_crc_gen.h` |
| 根 `compile_flags.txt` | `tools/gen_compile_db.py` | `compile_commands.json`（含 `.h/.hpp` 头文件条目） |

Kconfig 菜单：Platform、Multi-core/AMP、OSAL、Spinlock、System Log、System Runtime（`SYSTEM` 总开关 + C/CPP 后端）、Components（USB）、Board Features（WDT/Scrubber/…）、Runtime Capacity（`EVENT_BUS` 总开关 + 容量）、Compiler、Build。

CMake 关键缓存变量：`BOARD_DTS`、`BOARD_DTSI_DIR`、`VENDOR_INC_DIRS`、`VENDOR_DEFINES`。

---

## 6. 中断模型

`interrupt/interrupt.h`：

- **VIRQ**：按 block 划分（system/tim/gpio/adc/uart/spi/i2c/i2s/…），块大小 `VIRTUAL_IRQ_BLOCK_SIZE`（2 的幂）。
- **上半部**：ISR 内 `top_half`，可自动 submit 下半部。
- **下半部**：SPSC 队列；裸机主循环 `poll`，RTOS 可用任务 + sem。
- **红线**：ISR 内禁止 printf / 长时间锁 / 无界工作（见 [fast_path.md](fast_path.md)）。

---

## 7. 安全架构

| 机制 | 作用 |
| :--- | :--- |
| `safe_state` + `hal_platform_safety` / `hal_amp` | 进入安全态、关输出 |
| `board_safety_register_shutdown` | 仅 probe 阶段注册停机回调 |
| IWDG / Scrubber（Kconfig） | 看门狗与 Flash 巡检 |
| `compiler_compat_poison` | 限制 malloc/printf/裸 mem* |
| `COMPAT_*` 内存 API | 替代被 poison 的 libc 符号 |

---

## 8. 跨平台边界

| 本仓库提供 | 平台仓库提供 |
| :--- | :--- |
| 中间件源码、weak HAL、占位 DTS、文档、ide stubs | `hal_*_<soc>.c`、完整 dts/dtsi、厂商 `-I`、板级链接脚本与启动文件 |
| OSAL 三后端骨架 | 时钟、堆、SysTick/RTOS 端口（若需要） |

通用 CMake 路径下，平台通过 `MINI_TREE_BOARD_PORT`（绝对路径）或同级 `board_port.cmake` 注入板级；ESP-IDF 路径（由 `ESP_PLATFORM` 触发组件模式）已**迁移到 `esp` 分支**。验证矩阵以各 `platform/*/mini_tree` 工程为准，不在本 shelf 内绑定具体 SoC。

---

## 相关文档

- [usage.md](usage.md) · [getting_started.md](getting_started.md) · [ecosystem.md](ecosystem.md)
- [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md)
- [design_decisions.md](design_decisions.md) · [api_compatibility.md](api_compatibility.md)
