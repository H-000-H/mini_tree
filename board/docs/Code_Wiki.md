# ESP32-S3 — Code Wiki

> **注意（shelf）：** 本仓 `board/dtsi/` 仅保留通用示例
> [`example-soc.dtsi`](../dtsi/example-soc.dtsi)，无 SoC 专用片段。下文路径中的 `esp32s3-*.dtsi` /
> `esp32-s3-devkitc-1.dts` 指**平台工程**中的正式文件，不再位于本中间件默认树。
> **Note (shelf):** This repo's `board/dtsi/` keeps only the generic example
> [`example-soc.dtsi`](../dtsi/example-soc.dtsi); the `esp32s3-*.dtsi` /
> `esp32-s3-devkitc-1.dts` paths below refer to files in the **platform project**, not this middleware tree.

> 适用仓库: `Heterogeneous-Multicore-project/ESP32-S3`
> Applies to repo: `Heterogeneous-Multicore-project/ESP32-S3`
> 平台: ESP32-S3-DevKitC-1 + ESP-IDF v5.x
> Platform: ESP32-S3-DevKitC-1 + ESP-IDF v5.x
> 框架: `mini_tree`（Linux 风格设备树 + 编译期 probe 表 + 平台 HAL）
> Framework: `mini_tree` (Linux-style device tree + compile-time probe table + platform HAL)
> 应用域: **虚拟网卡（USB OTG CDC-ECM）** + **FFT 协处理器（SPI Slave）**
> Application domain: **virtual NIC (USB OTG CDC-ECM)** + **FFT coprocessor (SPI Slave)**

---

## 1. 项目总览 / Project Overview

本工程是 `mini_tree` 设备树框架在 **ESP32-S3** 上的移植。ESP32-S3 板在异构多核系统中担任两块功能外设的桥接节点：
This project ports the `mini_tree` device-tree framework to the **ESP32-S3**. The ESP32-S3 board acts as the bridge node for two functional peripherals in the heterogeneous multicore system:

| 功能 / Function | 总线 / Bus | 说明 / Description |
| --------------- | ---------- | -------------------------------------------------------------------- |
| 虚拟网卡 (NIC)  | USB OTG    | CDC-ECM 模式，接 i.MX6ULL 主机，枚举为以太网设备 `usb0` / CDC-ECM mode, attached to the i.MX6ULL host, enumerating as Ethernet device `usb0` |
| FFT 协处理器    | SPI Slave  | 接收外部主控的时域数据，回频域结果 / receives time-domain data from the external master, returns frequency-domain results |
| 烧录 / 调试     | USB Serial/JTAG | 接 PC，`idf.py flash monitor` / OpenOCD / connected to a PC; `idf.py flash monitor` / OpenOCD |

> USB OTG 与 USB Serial/JTAG 是两路不同的物理 USB 接口，开发时**不可混用**：OTG 留给 i.MX6ULL 网卡，Serial/JTAG 接 PC。
> USB OTG and USB Serial/JTAG are two distinct physical USB interfaces — do **not** mix them during development: OTG is reserved for the i.MX6ULL NIC, Serial/JTAG connects to the PC.

### 1.1 顶层目录 / Top-Level Directory

```
ESP32-S3/
├── main/                            # app_main 入口 (C++)
│   └── main.cpp                     # extern "C" app_main → app_rtos_start()
├── components/
│   ├── app/                         # 应用层 RTOS 任务 (LED / SPI 测试)
│   │   ├── inc/                     # hpp 头
│   │   └── src/                     # freertos 启动 + led/spi 任务
│   └── mini_tree/                   # 板级框架核心
│       ├── board/                   # DTS / 设备表 / probe / config_store
│       │   ├── dts/, dtsi/          # 设备树源
│       │   ├── dt-bindings/         # DTS 宏常量
│       │   ├── docs/                # 框架文档 (devicetree.md)
│       │   ├── include/, src/       # 板级 C API 与实现
│       │   └── CMakeLists.txt
│       ├── osal/                    # OSAL 抽象 (FreeRTOS / RT-Thread / NULL)
│       ├── hal_if/                  # 平台无关 HAL 接口 + 平台实现
│       ├── hal_bus/                 # 总线型 HAL 抽象 (SPI / I2C / I2S / …)
│       ├── hal_inst/                # 实例型 HAL 实现 (hal_spi.c)
│       ├── core/                    # EventBus / BufferPool / ProductionLog
│       ├── system_c/  system_cpp/   # 系统运行时 (Kconfig 选 C 或 C++)
│       ├── drivers/                 # 板级具体驱动 (ws2812, fft)
│       ├── vfs/                     # 设备树节点 → VFS 入口的桥接
│       │   └── spi/                 # SPI 总线 / 客户端驱动
│       ├── algorithm/               # 通用算法 (circle_fifo_buffer)
│       ├── tools/                   # dtc-lite / genconfig / menuconfig
│       ├── Kconfig / .config        # 编译期配置
│       └── CMakeLists.txt
├── CMakeLists.txt                   # 顶层: idf_component 入口 + 编译后反汇编
├── build.bat / flash.bat            # Windows 批处理: idf.py build / esptool 烧录
└── README.md
```

### 1.2 二进制构件 / Binary Artifacts

| 构件 / Artifact | 来源 / Source |
| --------------------------- | ----------------------------------------------------- |
| `app_main` | `main/main.cpp` |
| 业务任务 (led / spi) / business tasks (led / spi) | `components/app/src/app_*.cpp` |
| 框架运行时 (init/wdt/...) / framework runtime (init/wdt/...) | `components/mini_tree/system_c*/src/*.c|cpp` |
| OSAL 后端 / OSAL backend | `components/mini_tree/osal/src/osal_freertos.c` (当前/current) |
| HAL (CPU / GPIO / SPI / …) | `components/mini_tree/hal_if/src/hal_esp32s3.c` 等/et al. |
| VFS (SPI bus/client) | `components/mini_tree/vfs/spi/spi_*.c` |
| 板级驱动 (ws2812 / fft) / board drivers (ws2812 / fft) | `components/mini_tree/drivers/...` |
| **DTS 编译期生成代码** / **DTS compile-time generated code** | `tools/dtc-lite.py` → `board_devtable.c/.h` |
| **Kconfig 生成 config.h** / **Kconfig-generated config.h** | `tools/genconfig.py` → `generated/kconfig/config.h` |

---

## 2. 整体架构 / Overall Architecture

### 2.1 分层模型 / Layered Model

六层示意（应用 → 框架 → 驱动 → HAL → OSAL → ESP-IDF）。
Six-layer view (app → framework → drivers → HAL → OSAL → ESP-IDF).

```
┌────────────────────────────────────────────────────────────────────┐
│                          Application Layer                          │
│   main/main.cpp · app_led_task · app_spi_task · app_freertos       │
└────────────────┬───────────────────────────────────────────────────┘
                 │  device_open / device_write / device_ioctl
                 ▼
┌────────────────────────────────────────────────────────────────────┐
│                      mini_tree 框架层 (board/include)               │
│  device.h · bus.h · driver.h · dev_lifecycle.h · VFS.h             │
│  ────────────────────────────────────────────────────────────────  │
│  device_tree_init → board_driver_probe_all → board_driver_remove   │
│  device_find / device_get_prop_* / device_lock                     │
└────────────────┬───────────────────────────────────────────────────┘
                 │  board_devtable.c (dtc-lite 生成)
                 │  DRIVER_REGISTER(...) 编译期绑定
                 ▼
┌────────────────────────────────────────────────────────────────────┐
│                          Drivers (VFS 层)                           │
│  vfs/spi/spi_bus.c · vfs/spi/spi_client.c · drivers/ws2812 ·       │
│  drivers/fft/fft_spi_drv.c                                          │
│  ──── 持 pdev->lock, 调用 bus.c/HAL 接口 ────                        │
└────────────────┬───────────────────────────────────────────────────┘
                 │  hal_spi_xfer_begin / bus.write/read
                 ▼
┌────────────────────────────────────────────────────────────────────┐
│                          HAL 层 (hal_inst + hal_bus + hal_if)      │
│  hal_spi.c (ESP32 SPI slave)  · hal_spi_bus.h / hal_spi_bus_host.h │
│  hal_pulse_engine_esp32s3.c (RMT→WS2812) · hal_cpu_amp.c · …        │
└────────────────┬───────────────────────────────────────────────────┘
                 │  ESP-IDF: spi_slave_* / rmt_tx_* / gpio_*
                 ▼
┌────────────────────────────────────────────────────────────────────┐
│                       OSAL (FreeRTOS 后端)                          │
│  osal_freertos.c  →  Mutex / Spinlock / Sem / Queue / Task          │
│  ISR 检测 (IPSR) · 池分配 · 时间换算 · ISB 上下文标记                │
└────────────────┬───────────────────────────────────────────────────┘
                 │  portMUX / xSemaphore / xTaskCreate / vQueue*
                 ▼
┌────────────────────────────────────────────────────────────────────┐
│                       ESP-IDF (esp_driver_spi / rmt / gpio)         │
└────────────────────────────────────────────────────────────────────┘
```

### 2.2 启动时序 / Boot Sequence

两段式：pre-OS（关中断 + 静态初始化）→ start-tasks（开调度前完成 probe / WDT / scrubber）。
Two-phase: pre-OS (interrupts off + static init) → start-tasks (probe / WDT / scrubber complete before the scheduler starts).

```
ESP32 boot
   │
   ▼
app_main (main.cpp)
   │
   ▼ app_rtos_start (app/src/app_freertos.cpp)
   ├─ nvs_flash_init
   ├─ mini_tree_pre_os_init()                    [system_c/src/system_init.c]
   │   ├─ IRQ_DISABLE
   │   ├─ safe_state_check_bootloop
   │   ├─ system_wdt_init_iwdg
   │   ├─ device_tree_init()                     [board_device.c, 全量静态分配]
   │   ├─ event_bus_init()                       [core/event_bus.c]
   │   └─ g_system_os_initialized = true
   ├─ board_register_all_drivers()               [目前为空, 编译期注册]
   ├─ mini_tree_start_tasks()                    [system_init.c]
   │   ├─ event_bus_start
   │   ├─ board_driver_probe_all()  ◀── 核心: 3-pass 依赖解析 + 状态机
   │   ├─ system_wdt_init (TWDT)
   │   ├─ system_scrubber_init/start
   │   └─ event_bus_seal
   ├─ app_led_task_start (FreeRTOS task: led_task)
   ├─ app_spi_task_start (FreeRTOS task: spi_task)
   ├─ system_init_complete() → IRQ_ENABLE
   └─ vTaskStartScheduler()
```

---

## 3. 主要模块职责 / Module Responsibilities

### 3.1 `main/` — 入口 / Entry Point
- `main.cpp`: `extern "C" void app_main(void) { app_rtos_start(); }`，唯一职责是把控制权交给 `app_rtos_start`。/ `main.cpp`: sole responsibility is handing control to `app_rtos_start`.

### 3.2 `components/app/` — 应用层 RTOS 任务 / App-Layer RTOS Tasks
- **`app_rtos.hpp / app_freertos.cpp`**: 启动两段式初始化（pre-OS + start-tasks），并启动 LED / SPI 测试任务。/ runs two-phase init (pre-OS + start-tasks) and starts the LED / SPI test tasks.
- **`app_led_task`**: 通过 `device_find("ws2812")` 拿到设备，按 500ms 周期用 4 种颜色（红/绿/蓝/白）循环刷新。调用 `device_ioctl(WS2812_CMD_SET_COLOR)`。/ gets the device via `device_find("ws2812")` and cycles 4 colors (red/green/blue/white) every 500ms via `device_ioctl(WS2812_CMD_SET_COLOR)`.
- **`app_spi_task`**: 找到 DTS label 为 `fft_slave` 的设备（`device_find_by_label`），`device_open` 之后在循环里 `system_wdt_feed()` + `osal_delay_ms(2000)`。当前作为**最小烟雾测试**，验证 SPI slave 通路从 dtsi→probe→open 全链是否 OK。/ finds the device with DTS label `fft_slave` (`device_find_by_label`), then loops `system_wdt_feed()` + `osal_delay_ms(2000)` after `device_open`. Currently a **minimal smoke test** validating the full SPI slave path dtsi→probe→open.

### 3.3 `components/mini_tree/board/` — 板级核心 / Board Core
| 文件 / File | 职责 / Responsibility |
| -------------------------- | --------------------------------------------------------------------------- |
| `board_device.c` | `struct device` 实例表、设备查找、属性读取、状态机、VFS 转发层（持锁）/ `struct device` instance table, device lookup, property reads, state machine, lock-holding VFS forwarding |
| `board_driver.c` | `board_driver_probe_all` / `board_driver_remove_all`（3-pass 依赖解析）/ 3-pass dependency resolution |
| `bus.c` | `bus_controller` / `bus_client` 绑定表（SPI 父子节点）/ binding table for SPI parent/child nodes |
| `dev_lifecycle.c` | 设备生命周期（open/close/io/remove 的引用计数 + 持锁契约）/ device lifecycle (refcount + lock-holding contract for open/close/io/remove) |
| `config_store.c` | JSON 配置存储 (Kconfig 可选) / JSON config storage (optional via Kconfig) |
| `task_config.c` / `task_utils.c` | 任务优先级 / 栈监测辅助 / task priority / stack-monitor helpers |
| `dts/esp32-s3-devkitc-1.dts` | **平台工程**板级 DTS 入口：model / aliases / `&ws2812` / `&spi1` / `&fft_slave` 实例化 / **platform-project** board DTS entry: model / aliases / `&ws2812` / `&spi1` / `&fft_slave` instantiation |
| `dtsi/esp32s3.dtsi` | **平台工程** SoC 根节点：cpus、soc label、compatible / **platform-project** SoC root node: cpus, soc label, compatible |
| `dtsi/esp32s3-spi.dtsi` | **平台工程** SPI 总线 + fft_slave 子节点模板（`status = "disabled"`）/ SPI bus + fft_slave child template |
| `dtsi/esp32s3-ws2812.dtsi` | **平台工程** WS2812 节点模板（`status = "disabled"`）/ WS2812 node template |
| `dt-bindings/spi/spi-parameter.h` | SPI 默认参数宏（host_id / mode / freq / queue）/ SPI default parameter macros |
| `dt-bindings/led/ws2812-timing.h` | WS2812 时序宏（t0h/t0l/t1h/t1l/reset ticks）/ WS2812 timing macros |
| `docs/devicetree.md` | dts-lite 解析规则、节点约定、属性契约 / dts-lite parsing rules, node conventions, property contracts |
| `tools/dtc-lite.py` | **编译期 DTS → C**：输出 `board_devtable.c/.h`、`board_probe.c`、`board_nodes.h` / compile-time DTS → C |

> 注：上表中 `dts/esp32-s3-devkitc-1.dts` 与 `dtsi/esp32s3*.dtsi` 位于平台工程（`components/board_esp32s3/`），本中间件默认树只保留 `dts/board.dts` 占位与 `dtsi/example-soc.dtsi` 通用示例，平台通过 `BOARD_DTS` / `BOARD_DTSI_DIR`（或 `MINI_TREE_BOARD_PORT`）注入。
> Note: the `dts/esp32-s3-devkitc-1.dts` and `dtsi/esp32s3*.dtsi` above live in the platform project (`components/board_esp32s3/`); this middleware tree keeps only the `dts/board.dts` placeholder and the generic `dtsi/example-soc.dtsi`, injected by the platform via `BOARD_DTS` / `BOARD_DTSI_DIR` (or `MINI_TREE_BOARD_PORT`).

### 3.4 `components/mini_tree/osal/` — OS 抽象层 / OS Abstraction Layer
提供统一 API：mutex / recursive_mutex / spinlock / sem / queue / task / 时间 / 内存。当前 `.config` 选 **FreeRTOS**（`osal_freertos.c`）。
Provides unified APIs: mutex / recursive_mutex / spinlock / sem / queue / task / time / memory. The current `.config` selects **FreeRTOS** (`osal_freertos.c`).

- ISR 检测：`mrs ipsr` (ARMv7-M/Cortex-M) 或 `csrr mcause` (RISC-V)。/ ISR detection: `mrs ipsr` (ARMv7-M/Cortex-M) or `csrr mcause` (RISC-V).
- 池分配：`osal_pool_claim/release` 走 `taskENTER_CRITICAL`，ISR 安全。/ Pool allocation: `osal_pool_claim/release` go through `taskENTER_CRITICAL`, ISR-safe.
- 强约束：所有 `lock/unlock/create/destroy` 在 ISR 中**直接拒绝**（返回 `-1`）。/ Hard rule: every `lock/unlock/create/destroy` is **rejected outright** in ISR context (returns `-1`).

### 3.5 `components/mini_tree/hal_if/`, `hal_bus/`, `hal_inst/` — HAL 三件套 / The HAL Trio
- **`hal_if` (interface)**：平台相关 **API 接口** + **平台实现**。例如 `hal_pulse_engine.h` + `hal_pulse_engine_esp32s3.c`（RMT 编码器）。/ platform-facing **API interface** + **platform implementation**, e.g. `hal_pulse_engine.h` + `hal_pulse_engine_esp32s3.c` (RMT encoder).
- **`hal_bus` (bus abstraction)**：总线型设备抽象（`hal_spi_bus.h` 定义 `struct hal_spi_bus { write, read, write_top_half }`），平台无关的 vtable。/ bus-type device abstraction (`struct hal_spi_bus { write, read, write_top_half }` in `hal_spi_bus.h`), a platform-independent vtable.
- **`hal_inst` (instance)**：具体芯片实例实现（`hal_spi.c` 把 ESP-IDF `spi_slave_*` 包成 HAL 语义）。/ concrete chip-instance implementation (`hal_spi.c` wraps ESP-IDF `spi_slave_*` into HAL semantics).
- **总线 ↔ 实例 关系**：`hal_spi_bus_host` (全局每 host_id 一份) 由 `esp32,spi` probe 创建并常驻；`hal_spi_ctx` (interface) 由 `heterogeneous,fft-spi-slave` probe 创建，可 attach/detach。/ bus↔instance relation: `hal_spi_bus_host` (one global per host_id) is created and kept by the `esp32,spi` probe; `hal_spi_ctx` (interface) is created by the `heterogeneous,fft-spi-slave` probe and can attach/detach.

### 3.6 `components/mini_tree/core/` — 核心公共设施 / Core Common Facilities
- **`event_bus.{c,h}`**: 事件总线 (C API)。`event_bus_post / subscribe / post_from_isr / seal / drop_count`。框架事件 ID：`EVENT_SYS_BOOT / READY / FAULT / DEVICE_REMOVED`。/ event bus (C API): `event_bus_post / subscribe / post_from_isr / seal / drop_count`; framework event IDs: `EVENT_SYS_BOOT / READY / FAULT / DEVICE_REMOVED`.
- **`event_bus.cpp`**: C++ 包装（订阅者模式）。/ C++ wrapper (subscriber pattern).
- **`buffer_pool.{c,h}`**: 预分配定长缓冲池 (`bp_pool`)，支持 `BP_ALIGN_DMA` 32 字节对齐、位图分配、ISR 安全 (`bp_alloc_isr`)、峰值追踪。/ preallocated fixed-size buffer pool (`bp_pool`): `BP_ALIGN_DMA` 32-byte alignment, bitmap allocation, ISR-safety (`bp_alloc_isr`), peak tracking.
- **`production_log.{c,h}`**: 生产级黑匣子日志 (Ring buffer + CRC)。/ production-grade black-box log (ring buffer + CRC).
- **`printf_output.{c,h}`**: `my_printf_output()`，Kconfig 选 `SYS_LOG_USE_PRINTF` 时使用。/ used when Kconfig selects `SYS_LOG_USE_PRINTF`.
- **`system_log.h`**: `SYS_LOGI/W/E` 宏分发到所选后端（OSAL / ESP / printf），同时提供 `DRV_LOGI/W/E` (推到 production log)。/ dispatches `SYS_LOGI/W/E` to the selected backend (OSAL / ESP / printf), plus `DRV_LOGI/W/E` (pushed to the production log).
- **`safe_state.h`**: `enter_safe_state / safe_state_check_bootloop / safe_state_nmi_emergency_stamp`。
- **`bh/`**: bottom-half 队列（ISR → 任务上下文搬运）。/ bottom-half queue (ISR → task-context handoff).

### 3.7 `components/mini_tree/system_c{,pp}/` — 系统运行时 / System Runtime
由 `.config` 的 `CONFIG_SYSTEM_C / CONFIG_SYSTEM_CPP` 决定选 C 还是 C++。当前是 **C++**。
`CONFIG_SYSTEM_C / CONFIG_SYSTEM_CPP` in `.config` selects C or C++; currently **C++**.

- `system_init.c/cpp`: `mini_tree_pre_os_init / start_tasks / system_init_complete`，实现二段式点火。/ two-phase ignition.
- `system_wdt.cpp`: TWDT（任务看门狗）+ RTC WDT。/ TWDT (task watchdog) + RTC WDT.
- `system_scrubber.cpp`: Flash bit-rot 后台扫描 + CRC 基线（占位 stub：`system_scrubber_crc_stub.h` → 链接后由 `post_build_crc.py` 覆盖）。/ Flash bit-rot background scan + CRC baseline (stub placeholder `system_scrubber_crc_stub.h`, overwritten post-link by `post_build_crc.py`).
- `task_manager.cpp`: `task_manager_create_task`，自动 `system_wdt_subscribe`。/ auto-`system_wdt_subscribe`.
- `safe_state.c` / `safe_state.hpp`: bootloop 防烧穿。/ bootloop burn-in prevention.

### 3.8 `components/mini_tree/vfs/spi/` — VFS 桥接 / VFS Bridge
- `spi_bus.c`: 注册 `DRIVER_REGISTER(spi_bus, "esp32,spi", ...)`。职责：解析 host-id / mosi / miso / sclk / dma-tx-cfg / dma-rx-cfg → `hal_spi_bus_host_init` → 绑定 `bus_controller_bind` → 自动 enumerate 挂在本 host 下的 child (cascade)。/ registers `DRIVER_REGISTER(spi_bus, "esp32,spi", ...)`: parses host-id / mosi / miso / sclk / dma-tx-cfg / dma-rx-cfg → `hal_spi_bus_host_init` → `bus_controller_bind` → auto-enumerates children on this host (cascade).
- `spi_client.c`: 注册 `DRIVER_REGISTER(spi_client, "heterogeneous,fft-spi-slave", ...)`，由 `fft_spi_drv.c` 调用。提供 `file_operations` (open/close/read/write/ioctl)，每次 I/O 走 `dev_lc_io_begin → hal_spi_xfer_begin → bus.* → hal_spi_xfer_end`。/ registers `DRIVER_REGISTER(spi_client, "heterogeneous,fft-spi-slave", ...)`, called by `fft_spi_drv.c`; provides `file_operations` (open/close/read/write/ioctl); each I/O goes `dev_lc_io_begin → hal_spi_xfer_begin → bus.* → hal_spi_xfer_end`.
- `ex.c`: 当前空文件，保留给扩展示例。/ currently empty, reserved for extension examples.

### 3.9 `components/mini_tree/drivers/` — 板级具体驱动 / Board-Specific Drivers
- **`ws2812_drv.c`**: `DRIVER_REGISTER(ws2812, "esp32,ws2812", ...)`。DTS 解析所有 timing / color / brightness / num-leds / RMT 参数 → `hal_pulse_ws2812_open` 初始化 RMT channel → `device_write` / `WS2812_CMD_SET_COLOR` 走 RMT bytes encoder。/ DTS-parsed timing / color / brightness / num-leds / RMT params → `hal_pulse_ws2812_open` inits the RMT channel → `device_write` / `WS2812_CMD_SET_COLOR` via the RMT bytes encoder.
- **`fft_spi_drv.c`**: 极薄包装，直接调用 `spi_client_probe/remove`，让 `heterogeneous,fft-spi-slave` 节点复用 `spi_client` 通用路径。/ a very thin wrapper calling `spi_client_probe/remove` so the `heterogeneous,fft-spi-slave` node reuses the generic `spi_client` path.

### 3.10 `components/mini_tree/algorithm/` — 算法 / Algorithms
- `circle_fifo_buffer.c / m_buffer.h`: SPSC（单生产者单消费者）无锁环形 FIFO，acquire/release 内存序，cache line 隔离 `w_ptr` / `r_ptr` 防 false sharing。可用于双核 SPSC 音频通路。/ SPSC (single-producer single-consumer) lock-free ring FIFO with acquire/release memory ordering and cache-line isolation of `w_ptr` / `r_ptr` against false sharing; usable for dual-core SPSC audio paths.

### 3.11 `components/mini_tree/tools/` — 构建工具 / Build Tools
- `dtc-lite.py`: **核心工具**。无序全解耦版 DTS 编译器（向 Linux 看齐）。`#include` 展开后多个 `/ { }` 自动合并，`&label` 延迟合并或虚空创生。输出：/ **core tool** — an order-independent, fully decoupled DTS compiler (Linux-inspired). After `#include` expansion, multiple `/ { }` blocks merge automatically; `&label` merges lazily or creates empty shells. Outputs:
  - `board_nodes.h`: 节点枚举 / node enumeration
  - `board_devtable.c/.h`: 设备表、probe/remove 函数指针表、cascade 表 / device table, probe/remove function-pointer table, cascade table
  - `board_probe.c`: probe 调用分发 / probe call dispatch
  - `board_handles.h`: 句柄 / handles
  - `dt_config_gen.h`: `DTC_GEN_COUNT_*` 等宏 / macros like `DTC_GEN_COUNT_*`
- `genconfig.py`: 把 Kconfig → C 头文件 `config.h`。/ Kconfig → C header `config.h`.
- `menuconfig.py`: 文本菜单配置器。/ text-menu configurator.
- `post_build_crc.py`: 链接后计算 scrubber CRC 基线。/ computes the scrubber CRC baseline post-link.
- `convert_struct_typedef.py`: 辅助脚本。/ helper script.

---

## 4. 关键数据结构与 API / Key Data Structures & APIs

### 4.1 `struct device_node`（编译期只读 / compile-time read-only）
```c
struct device_node {
    const char* name;
    const char* label;            /* DTS label, 如 fft_slave */
    const char* compatible;       /* "esp32,spi" / "heterogeneous,fft-spi-slave" */
    const char* path;             /* "/soc/spi@0" */
    const struct device_property* props;
    const device_id_t* deps;      /* cascade child 列表 */
    const struct device_reg* regs;
    const struct device_irq* irqs;
    uint8_t status;               /* 编译期默认状态 */
    uint8_t criticality;          /* DEVICE_CRIT_IGNORE/WARNING/FATAL */
    uint8_t flags;                /* DEVICE_FLAG_DIRECT */
    uint8_t prop_count, dep_count, reg_count, irq_count;
};
```

### 4.2 `struct device`（运行时实例 / runtime instance）
```c
struct device {
    const struct device_node* node;       /* 指向 dtc-lite 生成的只读节点 */
    enum device_status        status;
    void*                     priv_data;  /* 驱动私有 (VFS 层) */
    const struct file_operations* ops;    /* 由 spi_client/ws2812 注入 */
    struct osal_mutex*        lock;       /* 递归锁, device_lock 用 */
    struct dev_lifecycle      lc;         /* open/io/close/remove 状态机 */
    void*                     platform_data;
};
```

### 4.3 `struct file_operations`（VFS 入口 / VFS entry）
```c
struct file_operations {
    int (*init)   (struct device*, void*);
    int (*open)   (struct device*, void*);
    int (*close)  (struct device*);
    int (*write)  (struct device*, const void*, size_t, uint32_t timeout_ms);
    int (*read)   (struct device*, void*, size_t, uint32_t timeout_ms);
    int (*ioctl)  (struct device*, int, void*, size_t, uint32_t timeout_ms);
    int (*suspend)(struct device*);
    int (*resume) (struct device*);
};
```

### 4.4 设备状态机 / Device State Machine
```
DISABLED ─→ READY/UNINIT ─→ READY ─→ PROBED ─→ RUNNING ─→ SUSPENDED
                                ↑          │ │            │
                                │          │ └─→ REMOVED  │
                                │          └──→ ERROR      │
                                └────────────┘             │
                                                           ▼
                                                         REMOVED
```
转换由 `device_status_can_transit` 在 `s_status_lock` 自旋锁保护下校验。
Transitions are validated by `device_status_can_transit` under the `s_status_lock` spinlock.

### 4.5 关键全局 API（`device.h`）/ Key Global APIs (`device.h`)
- 查找 / lookup: `device_find / find_by_label / find_by_id / find_by_path / find_by_compatible / get_phandle_dev`
- 属性 / properties: `device_get_prop_int / str / bool / int_array / get_reg / get_irq`
- 运行时 / runtime: `device_set_status / set_priv / get_priv`
- 遍历 / iteration: `device_get_first / get_next / get_count`
- 锁 / locking: `device_lock / unlock`（递归锁 / recursive lock）
- 卸载 / teardown: `device_ops_unregister`（持锁斩断 ops + priv_data / severs ops + priv_data while holding the lock）
- 生命周期 / lifecycle: `device_lc / device_lc_bind`
- VFS 包装 / VFS wrappers: `device_open / close / read / write / ioctl / suspend / resume`（**全部在持锁下做 check-then-act** / **all check-then-act under the lock**）

### 4.6 `dev_lifecycle`（驱动 I/O 生命周期 / Driver I/O Lifecycle）
| 状态 / State | 含义 / Meaning |
| ----------------- | ------------------------------- |
| `DEV_LC_UNINITIALIZED` | 初始（device_tree_init 中）/ initial (during device_tree_init) |
| `DEV_LC_LIVE`     | probe 完成，接收 open/io / probed, accepts open/io |
| `DEV_LC_REMOVING` | remove 已开始，拒绝新 open/io / remove started, rejects new open/io |
| `DEV_LC_DEAD`     | 已卸载 / unloaded |

`dev_lc_open_begin` 返回 1=首次、0=非首次；`io_begin` 返回 `VFS_OK/ERR`；`remove_drain` 持锁返回（持锁契约）。
`dev_lc_open_begin` returns 1=first, 0=re-entry; `io_begin` returns `VFS_OK/ERR`; `remove_drain` returns while holding the lock (lock-holding contract).

### 4.7 `DRIVER_REGISTER` 宏 / The `DRIVER_REGISTER` Macro
```c
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn) \
    int board_driver_probe_##name(struct device* dev)  { return probe_fn(dev); }  \
    int board_driver_remove_##name(struct device* dev) { return remove_fn(dev); }
```
被 dtc-lite 扫描收录入 `board_probe.c` 的静态表。运行期无 `strcmp` 匹配，开销恒定。
Collected by the dtc-lite scan into the static table in `board_probe.c`. No runtime `strcmp` matching — constant overhead.

### 4.8 OSAL 主要 API / Main OSAL APIs
| 分类 / Category | 接口 / Interface |
| --------- | -------------------------------------------------------------------------- |
| 时间 / time | `osal_time_ms / delay_ms / ticks_from_ms / timeout_to_ticks` |
| ISR 检测 / ISR detection | `osal_in_isr` |
| 调度/中断 / scheduling & interrupts | `osal_sched_freeze / int_freeze` (单向不可恢复 / one-way, non-recoverable) |
| 自旋锁 / spinlock | `osal_spinlock_init/lock/unlock`（关中断临界区 / interrupt-disabled critical sections） |
| 互斥锁 / mutex | `osal_mutex_create{,_static}{,_plain,_recursive,_typed}`、`lock/unlock/destroy` |
| 信号量 / semaphore | `osal_sem_create_binary{,_static} / wait / post / post_from_isr` |
| 队列 / queue | `osal_queue_create / send / receive (+_from_isr)` |
| 任务 / task | `osal_task_create / create_handle / self_delete / get_stack_watermark` |
| 池分配 / pool | `osal_pool_claim / release` |
| Panic | `OSAL_PANIC(fmt, ...)`、`CRITICAL_ASSERT(cond, fmt, ...)` |

---

## 5. 依赖关系 / Dependencies

### 5.1 内部模块依赖（按 `#include`）/ Internal Module Dependencies (by `#include`)

```
main.cpp
  └→ app_rtos.hpp
        └→ app_freertos.cpp
              ├→ system_init.h        (mini_tree_pre_os_init / start_tasks)
              ├→ driver.h             (board_register_all_drivers)
              ├→ app_led_task.hpp
              ├→ app_spi_task.hpp
              ├→ freertos/Task
              └→ nvs_flash

drivers/ws2812/ws2812_drv.c
  ├→ device.h / driver.h / VFS.h
  ├→ hal_pulse_engine.h        (RMT HAL)
  ├→ osal.h                    (mutex / pool)
  └→ system_log.h              (DRV_LOG*)

vfs/spi/spi_bus.c
  ├→ hal_spi_bus_host.h        (bus host 全局)
  ├→ hal_spi_bus.h             (vtable)
  ├→ device.h / driver.h / bus.h
  └→ osal.h

vfs/spi/spi_client.c
  ├→ hal_spi.h                 (hal_spi_ctx)
  ├→ hal_spi_bus_host.h
  ├→ hal_spi_bus.h
  ├→ spi_vfs.h                 (ioctl 命令字)
  ├→ dev_lifecycle.h
  └→ osal.h

hal_inst/src/hal_spi.c
  ├→ driver/spi_slave.h        (ESP-IDF)
  ├→ stdatomic.h               (trans_queued 原子标志)
  └→ osal.h
```

### 5.2 ESP-IDF 依赖（`REQUIRES`）/ ESP-IDF Dependencies (`REQUIRES`)

```cmake
# components/mini_tree/CMakeLists.txt
idf_component_register(... REQUIRES
    esp_driver_rmt       # RMT (WS2812)
    esp_driver_gpio      # GPIO
    esp_driver_spi       # SPI slave
)
```

### 5.3 DTS → C 生成依赖 / DTS → C Generation Dependencies

```
board/dts/esp32-s3-devkitc-1.dts
  + board/dtsi/esp32s3.dtsi
  + board/dtsi/esp32s3-ws2812.dtsi   ← #include <dt-bindings/led/ws2812-timing.h>
  + board/dtsi/esp32s3-spi.dtsi      ← #include <dt-bindings/spi/spi-parameter.h>
        │
        ▼  tools/dtc-lite.py
generated/board/mini_tree/
  ├─ board_nodes.h           (DEV_ID_* 枚举)
  ├─ board_devtable.h / .c   (s_devtable[], s_probe_table[], cascade[])
  ├─ board_handles.h
  ├─ board_probe.c           (board_driver_probe_all 实现)
  └─ dt_config_gen.h         (DTC_GEN_COUNT_* 宏)
```

### 5.4 Kconfig → config.h
```
components/mini_tree/Kconfig
  + components/mini_tree/.config
        │
        ▼  tools/genconfig.py
generated/kconfig/mini_tree/config.h
  → 影响 OSAL 后端选择 (CMAKE_OSAL_SRCS)
  → 影响 SYSTEM C/CPP 后端 (CMAKE_SYSTEM_SRCS)
```

---

## 6. 项目运行方式 / How to Run the Project

### 6.1 开发环境 / Development Environment
- **Windows** + ESP-IDF **v5.5.2**（路径在 `build.bat`: `C:\esp\v5.5.2\esp-idf`）
- Python 3.11 (`D:\Espressif_vscode\python_env\idf5.5_py3.11_env`)
- ESP32-S3 烧录走 **USB Serial/JTAG** 接口（`COM13`，921600 baud）/ flashing via the **USB Serial/JTAG** interface (`COM13`, 921600 baud)

### 6.2 构建流程 / Build Flow
1. **运行 `build.bat`** / run `build.bat`:
   - source `export.bat`
   - `idf.py build`
   - 关键步骤（CMake 自动）/ key steps (automatic in CMake):
     - `genconfig.py` 解析 `.config` → `config.h`
     - `dtc-lite.py` 解析 `board/dts/*.dts` → `board_devtable.c/.h`
     - `disasm.cmake` 在 `CONFIG_BUILD_DISASM=y` 时生成 `.lst` 反汇编 / generates `.lst` disassembly when `CONFIG_BUILD_DISASM=y`
     - 链接后由 `system_scrubber_crc_stub.h` 占位，build 阶段覆盖 / stub placeholder `system_scrubber_crc_stub.h` overwritten during build
2. **运行 `flash.bat`**（或 `idf.py -p COMxx flash monitor`）/ run `flash.bat` (or `idf.py -p COMxx flash monitor`):
   - 烧写 `bootloader / partition-table / app` 三段到 flash / flashes the three images `bootloader / partition-table / app`
3. **Monitor** 串口日志（默认 `printf` 后端）/ serial log monitor (default `printf` backend)。

> 平台集成：板级 DTS/dtsi 经 `BOARD_DTS` / `BOARD_DTSI_DIR`（或 `MINI_TREE_BOARD_PORT` 引入的 board_port.cmake）注入，替换本仓占位 `board.dts`。
> Platform integration: board DTS/dtsi is injected via `BOARD_DTS` / `BOARD_DTSI_DIR` (or a `MINI_TREE_BOARD_PORT`-imported board_port.cmake), replacing this repo's placeholder `board.dts`.

### 6.3 `.config` 当前关键开关 / Current Key `.config` Switches
```ini
CONFIG_PLATFORM_XTENSA=y        # Xtensa (ESP32)
CONFIG_OSAL_FREERTOS=y          # OSAL 后端 = FreeRTOS
CONFIG_CPU_CORES=1              # 单核
CONFIG_SYS_LOG_USE_PRINTF=y     # 日志 → my_printf_output
CONFIG_SYSTEM_CPP=y             # 系统运行时 = C++ 实现
CONFIG_ENABLE_WDT=y             # TWDT + RTC WDT
CONFIG_ENABLE_FLASH_SCRUBBER=y  # Flash bit-rot 后台扫描
CONFIG_BUILD_DISASM=y           # 生成 .lst 反汇编
CONFIG_OSAL_MUTEX_POOL_SIZE=24
CONFIG_EVENT_BUS_QUEUE_LEN=64
CONFIG_EVENT_BUS_MAX_SUBSCRIBERS=24
```
**未启用**：`PRODUCTION_LOG` / `SAFETY_SHUTDOWN`。/ **Not enabled**: `PRODUCTION_LOG` / `SAFETY_SHUTDOWN`.

### 6.4 运行时数据通路 / Runtime Data Paths
- **LED 演示 / LED demo**: `app_led_task`（prio=10, 2048B 栈）→ 查 `ws2812` → `ioctl(SET_COLOR)` → RMT bytes encoder → GPIO48 → 1 颗 WS2812 循环红/绿/蓝/白。/ `app_led_task` (prio=10, 2048B stack) → look up `ws2812` → `ioctl(SET_COLOR)` → RMT bytes encoder → GPIO48 → one WS2812 cycling red/green/blue/white.
- **SPI FFT 演示 / SPI FFT demo**: `app_spi_task`（prio=9, 3072B 栈）→ 查 `fft_slave` → `device_open` → 阻塞在 `device_read` 等主机数据（DTS 配置 CS=GPIO10，MOSI=11, MISO=13, SCLK=12, 64 字节 max）。/ `app_spi_task` (prio=9, 3072B stack) → look up `fft_slave` → `device_open` → block on `device_read` waiting for host data (DTS: CS=GPIO10, MOSI=11, MISO=13, SCLK=12, 64 bytes max).
- **虚拟网卡 (TODO/上游) / Virtual NIC (TODO/upstream)**: CDC-ECM 走 USB OTG，本仓库 main.cpp 当前未启用 USB 任务，依赖 i.MX6ULL 端 `cdc_ether` 驱动。/ CDC-ECM over USB OTG; this repo's `main.cpp` does not enable a USB task yet, relying on the `cdc_ether` driver on the i.MX6ULL side.

---

## 7. SPI 驱动书写顺序（重点总结）/ Writing an SPI Driver — Step by Step

本节是用户最关心的：**当你要添加一个 SPI 设备驱动时，按以下 7 步走**。每一步都附关键 API/文件位置。
This is the part users care about most: **to add an SPI device driver, follow these 7 steps**; each step lists the key APIs / file locations.

### Step 1. 准备 `dt-bindings` 常量（可选）/ Prepare `dt-bindings` Constants (Optional)
- 文件 / File：`components/mini_tree/board/dt-bindings/spi/spi-parameter.h`
- 若需要新的 SPI 默认参数（如不同 mode / 频率），按现有宏风格追加 `#define`。/ to add new SPI default params (e.g. different mode / frequency), append `#define`s in the existing macro style.
- **不要** 加 `#ifndef guard`（会破坏 dtc-lite 宏展开）。/ do **not** add `#ifndef` guards (they break dtc-lite macro expansion).

### Step 2. 写 IP 模板 `dtsi` / Write the IP Template `dtsi`
- 文件 / File：`components/mini_tree/board/dtsi/esp32s3-spi.dtsi`（**平台工程**中已存在，可仿写 / exists in the **platform project**; mimic it）
- 关键结构 / Key structure:
  ```dts
  #include "esp32s3.dtsi"
  #include <dt-bindings/spi/spi-parameter.h>
  &soc {
      spi1: spi@0 {
          compatible = "esp32,spi";
          reg = <0>;
          #address-cells = <1>;
          #size-cells = <0>;
          host-id = <SPI_DEFAULT_HOST_ID>;
          dma-tx-cfg = <DMA2_BASE LL_DMA_STREAM_3 LL_DMA_CHANNEL_3
                        LL_DMA_PRIORITY_HIGH LL_DMA_PDATAALIGN_BYTE 1>;
          dma-rx-cfg = <DMA2_BASE LL_DMA_STREAM_0 LL_DMA_CHANNEL_3
                        LL_DMA_PRIORITY_HIGH LL_DMA_PDATAALIGN_BYTE 1>;
          status = "disabled";
          my_dev: my_dev@1 {
              compatible = "myvendor,my-spi-slave";
              reg = <1>;
              spi-mode = <SPI_DEFAULT_MODE>;
              spi-max-frequency = <SPI_DEFAULT_MAX_FREQUENCY_HZ>;
              queue-size = <SPI_DEFAULT_QUEUE_SIZE>;
              status = "disabled";
          };
      };
  };
  ```
- 父节点用 `compatible = "esp32,spi"`（匹配 `vfs/spi/spi_bus.c`）。/ the parent uses `compatible = "esp32,spi"` (matching `vfs/spi/spi_bus.c`).
- 子节点 `compatible` 决定走哪个 probe（`heterogeneous,fft-spi-slave` / 自定义）。/ the child's `compatible` selects which probe runs (`heterogeneous,fft-spi-slave` / custom).

### Step 3. 板级实例化 `dts` / Board-Level Instantiation in `dts`
- 文件 / File：`components/mini_tree/board/dts/esp32-s3-devkitc-1.dts`（**平台工程** / **platform project**）
- 在文件尾加 / append at the end of the file:
  ```dts
  &spi1 {
      status = "okay";
      mosi-pin = <11>;
      misi-pin = <13>;
      sclk-pin = <12>;
      max-trans-buffer = <64>;
  };
  &my_dev {
      status = "okay";
      cs-pin = <10>;
  };
  ```
- 注意：`status = "okay"` 才会让 dtc-lite 在生成的 probe 表中收录此节点（必须能找到 `DRIVER_REGISTER(my_dev, ...)`，否则构建失败）。/ note: only `status = "okay"` makes dtc-lite include the node in the generated probe table (a matching `DRIVER_REGISTER(my_dev, ...)` must exist, otherwise the build fails).

### Step 4. 注册驱动 — `DRIVER_REGISTER` / Register the Driver — `DRIVER_REGISTER`
- 文件 / File：`components/mini_tree/drivers/my/my_spi_drv.c`（新建 / new file）
- 模板 / Template:
  ```c
  #include "spi_client.h"
  #include "device.h"
  #include "driver.h"
  #include "VFS.h"
  static int my_probe(struct device* dev) { return spi_client_probe(dev); }
  static int my_remove(struct device* dev) { return spi_client_remove(dev); }
  DRIVER_REGISTER(my_dev, "myvendor,my-spi-slave", my_probe, my_remove)
  ```
- 复用了 `spi_client` 通用路径：自动 attach / detach、自动持 io_lock、自动 bus 锁 + reconfigure。/ reuses the generic `spi_client` path: automatic attach/detach, automatic io_lock, automatic bus lock + reconfigure.
- 若需要自定义 `fops`（不直接复用 spi_client），可参考 `ws2812_drv.c` 写完整 `file_operations` + `device_lc_bind`。/ for a custom `fops` (not reusing spi_client), write a full `file_operations` + `device_lc_bind` modeled on `ws2812_drv.c`.

### Step 5. 添加文件到 CMake / Add Files to CMake
- 文件 / File：`components/mini_tree/CMakeLists.txt`
- 在 `DRIVER_SRCS` 列表追加 / append to the `DRIVER_SRCS` list:
  ```cmake
  set(DRIVER_SRCS
      "drivers/ws2812/ws2812_drv.c"
      "drivers/fft/fft_spi_drv.c"
      "drivers/my/my_spi_drv.c"
      "vfs/spi/spi_bus.c"
      "vfs/spi/spi_client.c"
  )
  ```
- 在 `INCLUDE_DIRS` 追加 `drivers/my`。/ add `drivers/my` to `INCLUDE_DIRS`.
- 在 `add_custom_command` 的 `DEPENDS` 中也加上这个 `.c`（dtc-lite 扫描 compat 字符串用）。/ also add this `.c` to `DEPENDS` of `add_custom_command` (dtc-lite scans it for compat strings).

### Step 6. 应用层调用 / Call from the App Layer
- 文件 / File：`components/app/src/app_my_task.cpp`（新建 FreeRTOS 任务 / new FreeRTOS task）
- 关键调用序列 / Key call sequence:
  ```c
  // 1) 查设备（按 DTS label 或 name）
  struct device* dev = device_find_by_label("my_dev");
  if (!pdev) { ESP_LOGE(TAG, "not found"); osal_task_self_delete(); }

  // 2) 打开（走 spi_client → hal_spi_interface_attach）
  if (device_open(dev, NULL) != 0) { ... }

  // 3) 同步读
  uint8_t buf[64];
  int n = device_read(dev, buf, sizeof(buf), 100);
  // 或带内 ioctl（用于半双工 split-phase）
  struct spi_read_arg ra = { .data = buf, .len = sizeof(buf) };
  device_ioctl(dev, SPI_CMD_READ, &ra, sizeof(ra), 100);

  // 4) 异步 (top-half) 路径
  struct spi_queue_arg qa = { .data = tx, .len = len };
  device_ioctl(dev, SPI_CMD_QUEUE_TX, &qa, sizeof(qa), 100);
  // 主机随后拉低 CS 触发后，下半部:
  struct spi_trans_result_arg tra = { .data = rx, .len = sizeof(rx), .trans_len = &got };
  device_ioctl(dev, SPI_CMD_GET_TRANS_RESULT, &tra, sizeof(tra), OSAL_WAIT_FOREVER);

  // 5) 关闭
  device_close(dev);
  ```

### Step 7. 验证 / 调试验证链 / Verify & Debug the Chain
1. 启动日志搜 `[spi_bus]` 出现 `controller probe OK: host=2 mosi=11 miso=13 sclk=12 children=1` → 总线 OK。/ search the boot log for `[spi_bus]` `controller probe OK: host=2 mosi=11 miso=13 sclk=12 children=1` → bus OK.
2. 搜 `[spi_client]` 出现 `client probe OK: cs=10 mode=0` → 客户端 OK。/ search for `[spi_client]` `client probe OK: cs=10 mode=0` → client OK.
3. `[fft_slave status=N (expect PROBED=3)]` 在 app 任务里打 `status`，确认 `DEVICE_STATUS_PROBED`。/ print `status` in the app task and confirm `DEVICE_STATUS_PROBED`.
4. `device_open failed` 时检查：DTS `status = "okay"` 是否生效、`&spi1` 引脚是否被 `&spi1` 覆盖、`cs-pin` 是否在 `&my_dev`。/ on `device_open failed`: check that DTS `status = "okay"` took effect, pins are overridden in `&spi1`, and `cs-pin` is in `&my_dev`.
5. 用逻辑分析仪抓 SCK/MOSI/CS，配合主控验证数据流。/ use a logic analyzer on SCK/MOSI/CS together with the master to verify the data flow.

### 7.1 SPI 调用栈速查（运行时）/ SPI Call Stack Quick Reference (Runtime)

```
device_write(dev, buf, len, tmo)                     [VFS wrapper, 持 dev->lock]
   └─ spi_operations_template.write = spi_write      [vfs/spi/spi_client.c]
        └─ dev_lc_io_begin (持 io_lock)              [dev_lifecycle.c]
            └─ hal_spi_xfer_begin(&priv->ctx, tmo)   [hal_inst/src/hal_spi.c]
                 ├─ osal_mutex_lock(host->bus_mutex)
                 └─ hal_spi_bus_reconfigure(host, cfg)
            └─ priv->ctx.host->bus.write(host, data, len)
                 └─ spi_bus_write_impl               [hal_inst/src/hal_spi.c]
                      ├─ spi_slave_setup_trans
                      └─ spi_slave_transmit          [ESP-IDF]
            └─ hal_spi_xfer_end
                 └─ osal_mutex_unlock
        └─ dev_lc_io_end
```

### 7.2 SPI 驱动文件清单 / SPI Driver File Checklist

| 关注点 / Focus | 文件 / File | 关键 API / Key API |
| ------------------- | ----------------------------------------------------- | --------------------------------------- |
| DTS 父节点模板 / DTS parent template | `board/dtsi/esp32s3-spi.dtsi` | `&soc { spi1: spi@0 { compatible = "esp32,spi" } }` |
| DTS 子节点模板 / DTS child template | `board/dtsi/esp32s3-spi.dtsi` | `fft_slave` 或自定义子节点 / or a custom child |
| 板级实例化 / board instantiation | `board/dts/esp32-s3-devkitc-1.dts` | `&spi1 / &fft_slave { status=okay }` |
| SPI 默认常量 / SPI default constants | `board/dt-bindings/spi/spi-parameter.h` | `SPI_DEFAULT_*` |
| 总线驱动 (VFS) / bus driver (VFS) | `vfs/spi/spi_bus.c` | `spi_controller_probe/remove` |
| 客户端驱动 (VFS) / client driver (VFS) | `vfs/spi/spi_client.c` | `spi_client_probe/remove` |
| HAL 总线 / HAL bus | `hal_inst/src/hal_spi.c` | `hal_spi_bus_host_init / xfer_begin/end` |
| HAL Bus vtable | `hal_bus/include/hal_spi_bus.h` | `struct hal_spi_bus { write/read/... }` |
| HAL Bus host 状态 / host state | `hal_inst/include/hal_spi_bus_host.h` | `struct hal_spi_bus_host` |
| HAL ctx (instance) | `hal_inst/include/hal_spi.h` | `struct hal_spi_ctx` |
| 业务驱动 (FFT) / business driver (FFT) | `drivers/fft/fft_spi_drv.c` | `DRIVER_REGISTER(fft_spi, ...)` |
| ioctl 命令字 / ioctl command words | `vfs/spi/spi_vfs.h` | `SPI_CMD_READ/QUEUE_TX/GET_TRANS_RESULT/DEINIT` |
| 应用任务 / app task | `components/app/src/app_spi_task.cpp` | `device_find_by_label("fft_slave")` |

---

## 8. 关键设计原则（速记）/ Key Design Principles (Cheat Sheet)

1. **SIOF（Static Initialization Order Fiasco）防御**：`g_system_os_initialized` 在 `mini_tree_pre_os_init` 后置 `true`，禁止 C++ 静态构造函数在 OS/EventBus 就绪前偷跑。/ **SIOF (Static Initialization Order Fiasco) defense**: `g_system_os_initialized` is set `true` after `mini_tree_pre_os_init`, preventing C++ static constructors from running before the OS/EventBus is ready.
2. **持锁 check-then-act**：`device_open/close/read/write/ioctl` 全部在 `device_lock` 保护下做状态检查 + ops 调用，阻断多线程重入与 TOCTOU。/ **lock-holding check-then-act**: `device_open/close/read/write/ioctl` all do state checks + ops calls under `device_lock`, blocking multithreaded reentry and TOCTOU.
3. **持锁返回契约（dev_lc_remove_drain）**：成功时调用方仍持有 `lc->io_lock`，必须与 `dev_lc_remove_finish` 严格配对；中间不允许 `dev_lc_io_begin` 等会抢同锁的 API。/ **lock-holding return contract (`dev_lc_remove_drain`)**: on success the caller still holds `lc->io_lock` and must strictly pair with `dev_lc_remove_finish`; APIs that would contend for the same lock (e.g. `dev_lc_io_begin`) are forbidden in between.
4. **DRIVER_REGISTER 编译期绑定**：dtc-lite 扫描 `.c` 源里 `DRIVER_REGISTER(name, compat, ...)` 把 `board_driver_probe_name` 收录入 `s_probe_table[]`，运行时直接按 id 索引，无 `strcmp`。/ **compile-time binding via DRIVER_REGISTER**: dtc-lite scans `DRIVER_REGISTER(name, compat, ...)` in `.c` sources, collects `board_driver_probe_name` into `s_probe_table[]`, indexed by id at runtime — no `strcmp`.
5. **DTS 无序全解耦**：多个 `/ { }` 任意顺序合并，`&label` 延迟合并或虚空创生，dtsi 中间可插 `&soc`。/ **order-independent, fully decoupled DTS**: multiple `/ { }` blocks merge in any order; `&label` merges lazily or creates empty shells; `&soc` may appear mid-dtsi.
6. **HAL Bus vs Instance 两层**：`hal_spi_bus_host` 全局常驻（bus mutex / ref_count / hw_inited），`hal_spi_ctx` 是 interface 实例（attach/detach 时增减 ref_count）。ESP32 slave 一 host 只能一 active_ctx（不同 CS/mode 拒绝 attach）。/ **two-layer HAL Bus vs Instance**: `hal_spi_bus_host` is a global resident (bus mutex / ref_count / hw_inited); `hal_spi_ctx` is an interface instance (ref_count bumps on attach/detach). An ESP32 slave host allows only one active_ctx (different CS/mode is refused on attach).
7. **OSAL 抽象**：mutex / spinlock / sem / queue / task 全部后端可选（FreeRTOS 当前选中）；ISR 检测用 `mrs ipsr`，所有 lock/unlock 在 ISR 中**直接拒绝**。/ **OSAL abstraction**: mutex / spinlock / sem / queue / task are all backend-selectable (FreeRTOS currently selected); ISR detection uses `mrs ipsr`; all lock/unlock are **rejected outright** in ISR context.
8. **SPSC FIFO 内存序**：xtensa/ARM Cortex-A 双核下用 acquire/release 协议；`w_ptr` / `r_ptr` 间 padding 防 false sharing（`m_buffer.h`）。/ **SPSC FIFO memory ordering**: acquire/release protocol on dual-core xtensa/ARM Cortex-A; padding between `w_ptr` / `r_ptr` prevents false sharing (`m_buffer.h`).
9. **命名与风格规范**：全仓遵循 `.clang-format` 与分层 `.clang-tidy`（内核区全小写、无 `s_/g_/k_` 前缀；`app/` 与 `system_cpp/` 为 Google 区，类型 PascalCase、保留用户前缀）；app 层建议、app 以下强规定。/ **naming & style rules**: repo-wide `.clang-format` plus layered `.clang-tidy` (kernel zone: all-lowercase, no `s_/g_/k_` prefixes; `app/` and `system_cpp/` are the Google zone: PascalCase types, user prefixes preserved); suggested at the app layer, enforced below it.

---

## 9. 常见坑 & 调试建议 / Common Pitfalls & Debugging Tips

1. **`status = "okay"` 不生效** → 检查 dtsi 是否 include，板级 dts 是否 include 该 dtsi，是否同时设置了 `compatible`。/ **`status = "okay"` not taking effect** → check that the dtsi is included, the board dts includes that dtsi, and `compatible` is set.
2. **`device_open failed`** → 99% 是设备树问题。先 `device_get_status(dev)` 看是不是 PROBED；否则查 dtc-lite 输出 `board_devtable.c` 中是否有此节点。/ **`device_open failed`** → 99% a device-tree issue. First `device_get_status(dev)` to see if PROBED; otherwise check whether the node is in dtc-lite's `board_devtable.c` output.
3. **SPI 主机收不到数据** → 确认 `max-trans-buffer` ≥ 实际负载；`cs-pin` 与 `spi-mode` 与主控一致；从机的 `clock_speed_hz` 仅作记录（实际由主控定钟）。/ **SPI master receives nothing** → ensure `max-trans-buffer` ≥ the actual payload; `cs-pin` and `spi-mode` match the master; the slave's `clock_speed_hz` is informational only (the master clocks the bus).
4. **WS2812 颜色错位** → 改 `color-order` (默认 `"grb"`)。/ **WS2812 colors shifted** → change `color-order` (default `"grb"`).
5. **链接器报 `system_safety_hardware_shutdown` undefined** → `CONFIG_SAFETY_SHUTDOWN=n` 时它是弱符号，链接期没问题；若 `=y` 则必须由 `board_driver.c` 提供强符号。/ **linker: `system_safety_hardware_shutdown` undefined** → with `CONFIG_SAFETY_SHUTDOWN=n` it is a weak symbol and links fine; with `=y` a strong symbol must come from `board_driver.c`.
6. **多 pass 探测超时** → `[board_drv] EPROBE_DEFER stall`：phandle 依赖未就绪，确认父节点 `compatible` 与驱动 `DRIVER_REGISTER` 名字完全一致。/ **multi-pass probe timeout** → `[board_drv] EPROBE_DEFER stall`: a phandle dependency is not ready; verify the parent's `compatible` exactly matches the driver's `DRIVER_REGISTER` name.
7. **反汇编生成 `.lst`** → `CONFIG_BUILD_DISASM=y`，由 `cmake/disasm.cmake` 触发 `objdump -d -S`。/ **generate `.lst` disassembly** → `CONFIG_BUILD_DISASM=y`, triggered by `cmake/disasm.cmake` via `objdump -d -S`.
8. **Flash scrubber CRC** → 链接后由 `post_build_crc.py` 用真实 CRC 覆盖 `system_scrubber_crc_stub.h` 占位（构建系统会拷贝 stub → 链接期先占，build 完再覆盖；运行时实际值需另行确认）。/ **Flash scrubber CRC** → post-link `post_build_crc.py` overwrites the `system_scrubber_crc_stub.h` placeholder with the real CRC (the build system copies the stub → links with the placeholder first, then overwrites after build; the actual runtime value needs separate confirmation).

---

## 10. 附录：仓库内外部参考 / Appendix: In-Repo & External References

- 设备树详细规范 / Device tree spec: [components/mini_tree/board/docs/devicetree.md](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/board/docs/devicetree.md)
- 项目总览与烧录 / Project overview & flashing: [README.md](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/README.md)
- SPI 客户端 VFS 实现 / SPI client VFS impl: [vfs/spi/spi_client.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/vfs/spi/spi_client.c)
- SPI 总线 VFS 实现 / SPI bus VFS impl: [vfs/spi/spi_bus.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/vfs/spi/spi_bus.c)
- HAL SPI 实现 / HAL SPI impl: [hal_inst/src/hal_spi.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/hal_inst/src/hal_spi.c)
- WS2812 驱动 / driver: [drivers/ws2812/ws2812_drv.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/drivers/ws2812/ws2812_drv.c)
- FFT SPI 驱动 / driver: [drivers/fft/fft_spi_drv.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/drivers/fft/fft_spi_drv.c)
- 板级 DTS / board DTS: [board/dts/esp32-s3-devkitc-1.dts](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/board/dts/esp32-s3-devkitc-1.dts)
- SPI dtsi: [board/dtsi/esp32s3-spi.dtsi](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/board/dtsi/esp32s3-spi.dtsi)
- dtc-lite 编译器 / compiler: [tools/dtc-lite.py](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/tools/dtc-lite.py)
