# ESP32-S3 — Code Wiki

> 适用仓库: `Heterogeneous-Multicore-project/ESP32-S3`
> 平台: ESP32-S3-DevKitC-1 + ESP-IDF v5.x
> 框架: `mini_tree`（Linux 风格设备树 + 编译期 probe 表 + 平台 HAL）
> 应用域: **虚拟网卡（USB OTG CDC-ECM）** + **FFT 协处理器（SPI Slave）**

---

## 1. 项目总览

本工程是 `mini_tree` 设备树框架在 **ESP32-S3** 上的移植。ESP32-S3 板在异构多核系统中担任两块功能外设的桥接节点：

| 功能            | 总线       | 说明                                                                 |
| --------------- | ---------- | -------------------------------------------------------------------- |
| 虚拟网卡 (NIC)  | USB OTG    | CDC-ECM 模式，接 i.MX6ULL 主机，枚举为以太网设备 `usb0`             |
| FFT 协处理器    | SPI Slave  | 接收外部主控的时域数据，回频域结果                                   |
| 烧录 / 调试     | USB Serial/JTAG | 接 PC，`idf.py flash monitor` / OpenOCD                          |

> USB OTG 与 USB Serial/JTAG 是两路不同的物理 USB 接口，开发时**不可混用**：OTG 留给 i.MX6ULL 网卡，Serial/JTAG 接 PC。

### 1.1 顶层目录

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
│       ├── hal/                     # 硬件抽象实现 (cpu/gpio/spi/uart/... + hal_if_dummy 兜底)
│       ├── bus/                     # 总线层 (dma/spi/uart 主从总线抽象)
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

### 1.2 二进制构件

| 构件                        | 来源                                                  |
| --------------------------- | ----------------------------------------------------- |
| `app_main`                  | `main/main.cpp`                                       |
| 业务任务 (led / spi)        | `components/app/src/app_*.cpp`                        |
| 框架运行时 (init/wdt/...)   | `components/mini_tree/system_c*/src/*.c|cpp`         |
| OSAL 后端                   | `components/mini_tree/osal/src/osal_freertos.c` (当前) |
| HAL (CPU / GPIO / SPI / …)  | `components/mini_tree/hal/soc/esp32s3/hal_esp32s3.c` 等   |
| VFS (SPI bus/client)        | `components/mini_tree/vfs/spi/spi_*.c`               |
| 板级驱动 (ws2812 / fft)     | `components/mini_tree/drivers/...`                    |
| **DTS 编译期生成代码**      | `tools/dtc-lite.py` → `board_devtable.c/.h`           |
| **Kconfig 生成 config.h**   | `tools/genconfig.py` → `generated/kconfig/config.h`  |

---

## 2. 整体架构

### 2.1 分层模型

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
│  ──── 持 dev->lock, 调用 bus.c/HAL 接口 ────                        │
└────────────────┬───────────────────────────────────────────────────┘
                 │  hal_spi_xfer_begin / bus.write/read
                 ▼
┌────────────────────────────────────────────────────────────────────┐
│                          HAL 层 (hal + bus)                        │
│  hal/spi/hal_spi.c (ESP32 SPI slave) · bus/spi/spi_bus.h (vtable+host) │
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

### 2.2 启动时序

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
   │   ├─ system_wdt_init_rtc
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

## 3. 主要模块职责

### 3.1 `main/` — 入口
- `main.cpp`: `extern "C" void app_main(void) { app_rtos_start(); }`，唯一职责是把控制权交给 `app_rtos_start`。

### 3.2 `components/app/` — 应用层 RTOS 任务
- **`app_rtos.hpp / app_freertos.cpp`**: 启动两段式初始化（pre-OS + start-tasks），并启动 LED / SPI 测试任务。
- **`app_led_task`**: 通过 `device_find("ws2812")` 拿到设备，按 500ms 周期用 4 种颜色（红/绿/蓝/白）循环刷新。调用 `device_ioctl(WS2812_CMD_SET_COLOR)`。
- **`app_spi_task`**: 找到 DTS label 为 `fft_slave` 的设备（`device_find_by_label`），`device_open` 之后在循环里 `system_wdt_feed()` + `osal_delay_ms(2000)`。当前作为**最小烟雾测试**，验证 SPI slave 通路从 dtsi→probe→open 全链是否 OK。

### 3.3 `components/mini_tree/board/` — 板级核心
| 文件                       | 职责                                                                        |
| -------------------------- | --------------------------------------------------------------------------- |
| `board_device.c`           | `struct device` 实例表、设备查找、属性读取、状态机、VFS 转发层（持锁）       |
| `board_driver.c`           | `board_driver_probe_all` / `board_driver_remove_all`（3-pass 依赖解析）      |
| `bus.c`                    | `bus_controller` / `bus_client` 绑定表（SPI 父子节点）                       |
| `dev_lifecycle.c`          | 设备生命周期（open/close/io/remove 的引用计数 + 持锁契约）                   |
| `config_store.c`           | JSON 配置存储 (Kconfig 可选)                                                |
| `task_config.c` / `task_utils.c` | 任务优先级 / 栈监测辅助                                                  |
| `dts/esp32-s3-devkitc-1.dts` | 板级 DTS 入口：model / aliases / `&ws2812` / `&spi1` / `&fft_slave` 实例化  |
| `dtsi/esp32s3.dtsi`        | SoC 根节点：cpus、soc label、compatible                                     |
| `dtsi/esp32s3-spi.dtsi`    | SPI 总线 + fft_slave 子节点模板（`status = "disabled"`）                    |
| `dtsi/esp32s3-ws2812.dtsi` | WS2812 节点模板（`status = "disabled"`）                                    |
| `dt-bindings/spi/spi-parameter.h` | SPI 默认参数宏（host_id / mode / freq / queue）                     |
| `dt-bindings/led/ws2812-timing.h` | WS2812 时序宏（t0h/t0l/t1h/t1l/reset ticks）                        |
| `docs/devicetree.md`       | dts-lite 解析规则、节点约定、属性契约                                       |
| `tools/dtc-lite.py`        | **编译期 DTS → C**：输出 `board_devtable.c/.h`、`board_probe.c`、`board_nodes.h` |

### 3.4 `components/mini_tree/osal/` — OS 抽象层
提供统一 API：mutex / recursive_mutex / spinlock / sem / queue / task / 时间 / 内存。当前 `.config` 选 **FreeRTOS**（`osal_freertos.c`）。
- ISR 检测：`mrs ipsr` (ARMv7-M/Cortex-M) 或 `csrr mcause` (RISC-V)。
- 池分配：`osal_pool_claim/release` 走 `taskENTER_CRITICAL`，ISR 安全。
- 强约束：所有 `lock/unlock/create/destroy` 在 ISR 中**直接拒绝**（返回 `-1`）。

### 3.5 `components/mini_tree/hal/`, `bus/` — HAL 与总线层
- **`hal/` (硬件抽象)**：平台相关 **API 接口** + **平台实现** + `hal_if_dummy.c` 兜底。例如 `hal/soc/esp32s3/` 下 ESP32 平台实现（RMT 编码器等）。
- **`bus/` (总线抽象)**：总线型设备抽象（`bus/spi/spi_bus.h` 定义 `struct hal_spi_bus { write, read, write_top_half }`），平台无关的 vtable；含 `bus/dma/`、`bus/uart/` 子目录。
- **实例实现**：具体芯片实例（`hal/spi/` 把 ESP-IDF `spi_slave_*` 包成 HAL 语义）。
- **总线 ↔ 实例 关系**：`hal_spi_bus_host` (全局每 host_id 一份) 由 `esp32,spi` probe 创建并常驻；`hal_spi_ctx` (interface) 由 `heterogeneous,fft-spi-slave` probe 创建，可 attach/detach。

### 3.6 `components/mini_tree/core/` — 核心公共设施
- **`event_bus.{c,h}`**: 事件总线 (C API)。`event_bus_post / subscribe / post_from_isr / seal / drop_count`。框架事件 ID：`EVENT_SYS_BOOT / READY / FAULT / DEVICE_REMOVED`。
- **`event_bus.cpp`**: C++ 包装（订阅者模式）。
- **`buffer_pool.{c,h}`**: 预分配定长缓冲池 (`bp_pool`)，支持 `BP_ALIGN_DMA` 32 字节对齐、位图分配、ISR 安全 (`bp_alloc_isr`)、峰值追踪。
- **`production_log.{c,h}`**: 生产级黑匣子日志 (Ring buffer + CRC)。
- **`printf_output.{c,h}`**: `my_printf_output()`，Kconfig 选 `SYS_LOG_USE_PRINTF` 时使用。
- **`system_log.h`**: `SYS_LOGI/W/E` 宏分发到所选后端（OSAL / ESP / printf），同时提供 `DRV_LOGI/W/E` (推到 production log)。
- **`safe_state.h`**: `enter_safe_state / safe_state_check_bootloop / safe_state_nmi_emergency_stamp`。
- **`bh/`**: bottom-half 队列（ISR → 任务上下文搬运）。

### 3.7 `components/mini_tree/system_c{,pp}/` — 系统运行时
由 `.config` 的 `CONFIG_SYSTEM_C / CONFIG_SYSTEM_CPP` 决定选 C 还是 C++。当前是 **C++**。
- `system_init.c/cpp`: `mini_tree_pre_os_init / start_tasks / system_init_complete`，实现 IEC 61508 §7.4.3 二段式点火。
- `system_wdt.cpp`: TWDT（任务看门狗）+ RTC WDT。
- `system_scrubber.cpp`: Flash bit-rot 后台扫描 + CRC 基线（占位 stub：`system_scrubber_crc_stub.h` → 链接后由 `post_build_crc.py` 覆盖）。
- `task_manager.cpp`: `task_manager_create_task`，自动 `system_wdt_subscribe`。
- `safe_state.c` / `safe_state.hpp`: bootloop 防烧穿。

### 3.8 `components/mini_tree/vfs/spi/` — VFS 桥接
- `spi_bus.c`: 注册 `DRIVER_REGISTER(spi_bus, "esp32,spi", ...)`。职责：解析 host-id / mosi / miso / sclk / dma-chan → `hal_spi_bus_host_init` → 绑定 `bus_controller_bind` → 自动 enumerate 挂在本 host 下的 child (cascade)。
- `spi_client.c`: 注册 `DRIVER_REGISTER(spi_client, "heterogeneous,fft-spi-slave", ...)`，由 `fft_spi_drv.c` 调用。提供 `file_operations` (open/close/read/write/ioctl)，每次 I/O 走 `dev_lc_io_begin → hal_spi_xfer_begin → bus.* → hal_spi_xfer_end`。
- `ex.c`: 当前空文件，保留给扩展示例。

### 3.9 `components/mini_tree/drivers/` — 板级具体驱动
- **`ws2812_drv.c`**: `DRIVER_REGISTER(ws2812, "esp32,ws2812", ...)`。DTS 解析所有 timing / color / brightness / num-leds / RMT 参数 → `hal_pulse_ws2812_open` 初始化 RMT channel → `device_write` / `WS2812_CMD_SET_COLOR` 走 RMT bytes encoder。
- **`fft_spi_drv.c`**: 极薄包装，直接调用 `spi_client_probe/remove`，让 `heterogeneous,fft-spi-slave` 节点复用 `spi_client` 通用路径。

### 3.10 `components/mini_tree/algorithm/` — 算法
- `circle_fifo_buffer.c / m_buffer.h`: SPSC（单生产者单消费者）无锁环形 FIFO，acquire/release 内存序，cache line 隔离 `w_ptr` / `r_ptr` 防 false sharing。可用于双核 SPSC 音频通路。

### 3.11 `components/mini_tree/tools/` — 构建工具
- `dtc-lite.py`: **核心工具**。无序全解耦版 DTS 编译器（向 Linux 看齐）。`#include` 展开后多个 `/ { }` 自动合并，`&label` 延迟合并或虚空创生。输出：
  - `board_nodes.h`: 节点枚举
  - `board_devtable.c/.h`: 设备表、probe/remove 函数指针表、cascade 表
  - `board_probe.c`: probe 调用分发
  - `board_handles.h`: 句柄
  - `dt_config_gen.h`: `DTC_GEN_COUNT_*` 等宏
- `genconfig.py`: 把 Kconfig → C 头文件 `config.h`。
- `menuconfig.py`: 文本菜单配置器。
- `post_build_crc.py`: 链接后计算 scrubber CRC 基线。
- `convert_struct_typedef.py`: 辅助脚本。

---

## 4. 关键数据结构与 API

### 4.1 `struct device_node`（编译期只读）
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

### 4.2 `struct device`（运行时实例）
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

### 4.3 `struct file_operations`（VFS 入口）
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

### 4.4 设备状态机
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

### 4.5 关键全局 API（`device.h`）
- 查找: `device_find / find_by_label / find_by_id / find_by_path / find_by_compatible / get_phandle_dev`
- 属性: `device_get_prop_int / str / bool / int_array / get_reg / get_irq`
- 运行时: `device_set_status / set_priv / get_priv`
- 遍历: `device_get_first / get_next / get_count`
- 锁: `device_lock / unlock`（递归锁）
- 卸载: `device_ops_unregister`（持锁斩断 ops + priv_data）
- 生命周期: `device_lc / device_lc_bind`
- VFS 包装: `device_open / close / read / write / ioctl / suspend / resume`（**全部在持锁下做 check-then-act**，IEC 61508 §7.4.3.1）

### 4.6 `dev_lifecycle`（驱动 I/O 生命周期）
| 状态              | 含义                            |
| ----------------- | ------------------------------- |
| `DEV_LC_UNINITIALIZED` | 初始（device_tree_init 中） |
| `DEV_LC_LIVE`     | probe 完成，接收 open/io        |
| `DEV_LC_REMOVING` | remove 已开始，拒绝新 open/io   |
| `DEV_LC_DEAD`     | 已卸载                         |

`dev_lc_open_begin` 返回 1=首次、0=非首次；`io_begin` 返回 `VFS_OK/ERR`；`remove_drain` 持锁返回（持锁契约）。

### 4.7 `DRIVER_REGISTER` 宏
```c
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn) \
    int board_driver_probe_##name(struct device* dev)  { return probe_fn(dev); }  \
    int board_driver_remove_##name(struct device* dev) { return remove_fn(dev); }
```
被 dtc-lite 扫描收录入 `board_probe.c` 的静态表。运行期无 `strcmp` 匹配，开销恒定。

### 4.8 OSAL 主要 API
| 分类      | 接口                                                                       |
| --------- | -------------------------------------------------------------------------- |
| 时间      | `osal_time_ms / delay_ms / ticks_from_ms / timeout_to_ticks`               |
| ISR 检测  | `osal_in_isr`                                                              |
| 调度/中断 | `osal_sched_suspend / int_disable`                                         |
| 自旋锁    | `osal_spinlock_init/lock/unlock`（关中断临界区）                           |
| 互斥锁    | `osal_mutex_create{,_static}{,_plain,_recursive,_typed}`、`lock/unlock/destroy` |
| 信号量    | `osal_sem_create_binary{,_static} / wait / post / post_from_isr`           |
| 队列      | `osal_queue_create / send / receive (+_from_isr)`                          |
| 任务      | `osal_task_create / create_handle / self_delete / get_stack_watermark`     |
| 池分配    | `osal_pool_claim / release`                                                |
| Panic     | `OSAL_PANIC(fmt, ...)`、`CRITICAL_ASSERT(cond, fmt, ...)`                  |

---

## 5. 依赖关系

### 5.1 内部模块依赖（按 `#include`）

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
  ├→ bus/spi/spi_bus.h         (vtable + bus host 全局)
  ├→ device.h / driver.h / bus.h
  └→ osal.h

vfs/spi/spi_client.c
  ├→ hal/spi/hal_spi.h         (hal_spi_ctx)
  ├→ bus/spi/spi_bus.h         (vtable + host)
  ├→ spi_vfs.h                 (ioctl 命令字)
  ├→ dev_lifecycle.h
  └→ osal.h

hal/spi/hal_spi.c
  ├→ driver/spi_slave.h        (ESP-IDF)
  ├→ stdatomic.h               (trans_queued 原子标志)
  └→ osal.h
```

### 5.2 ESP-IDF 依赖（`REQUIRES`）

```cmake
# components/mini_tree/CMakeLists.txt
idf_component_register(... REQUIRES
    esp_driver_rmt       # RMT (WS2812)
    esp_driver_gpio      # GPIO
    esp_driver_spi       # SPI slave
)
```

### 5.3 DTS → C 生成依赖

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

## 6. 项目运行方式

### 6.1 开发环境
- **Windows** + ESP-IDF **v5.5.2**（路径在 `build.bat`: `C:\esp\v5.5.2\esp-idf`）
- Python 3.11 (`D:\Espressif_vscode\python_env\idf5.5_py3.11_env`)
- ESP32-S3 烧录走 **USB Serial/JTAG** 接口（`COM13`，921600 baud）

### 6.2 构建流程
1. **运行 `build.bat`**：
   - source `export.bat`
   - `idf.py build`
   - 关键步骤（CMake 自动）：
     - `genconfig.py` 解析 `.config` → `config.h`
     - `dtc-lite.py` 解析 `board/dts/*.dts` → `board_devtable.c/.h`
     - `disasm.cmake` 在 `CONFIG_BUILD_DISASM=y` 时生成 `.lst` 反汇编
     - 链接后由 `system_scrubber_crc_stub.h` 占位，build 阶段覆盖
2. **运行 `flash.bat`**（或 `idf.py -p COMxx flash monitor`）：
   - 烧写 `bootloader / partition-table / app` 三段到 flash
3. **Monitor** 串口日志（默认 `printf` 后端）。

### 6.3 `.config` 当前关键开关
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
**未启用**：`PRODUCTION_LOG` / `SAFETY_SHUTDOWN`。

### 6.4 运行时数据通路
- **LED 演示**: `app_led_task`（prio=10, 2048B 栈）→ 查 `ws2812` → `ioctl(SET_COLOR)` → RMT bytes encoder → GPIO48 → 1 颗 WS2812 循环红/绿/蓝/白。
- **SPI FFT 演示**: `app_spi_task`（prio=9, 3072B 栈）→ 查 `fft_slave` → `device_open` → 阻塞在 `device_read` 等主机数据（DTS 配置 CS=GPIO10，MOSI=11, MISO=13, SCLK=12, 64 字节 max）。
- **虚拟网卡 (TODO/上游)**: CDC-ECM 走 USB OTG，本仓库 main.cpp 当前未启用 USB 任务，依赖 i.MX6ULL 端 `cdc_ether` 驱动。

---

## 7. SPI 驱动书写顺序（重点总结）

本节是用户最关心的：**当你要添加一个 SPI 设备驱动时，按以下 7 步走**。每一步都附关键 API/文件位置。

### Step 1. 准备 `dt-bindings` 常量（可选）
- 文件：`components/mini_tree/board/dt-bindings/spi/spi-parameter.h`
- 若需要新的 SPI 默认参数（如不同 mode / 频率），按现有宏风格追加 `#define`。
- **不要** 加 `#ifndef guard`（会破坏 dtc-lite 宏展开）。

### Step 2. 写 IP 模板 `dtsi`
- 文件：`components/mini_tree/board/dtsi/esp32s3-spi.dtsi`（已存在，可仿写）
- 关键结构：
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
          dma-chan = <SPI_DEFAULT_DMA_CHAN>;
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
- 父节点用 `compatible = "esp32,spi"`（匹配 `vfs/spi/spi_bus.c`）。
- 子节点 `compatible` 决定走哪个 probe（`heterogeneous,fft-spi-slave` / 自定义）。

### Step 3. 板级实例化 `dts`
- 文件：`components/mini_tree/board/dts/esp32-s3-devkitc-1.dts`
- 在文件尾加：
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
- 注意：`status = "okay"` 才会让 dtc-lite 在生成的 probe 表中收录此节点（必须能找到 `DRIVER_REGISTER(my_dev, ...)`，否则构建失败）。

### Step 4. 注册驱动 — `DRIVER_REGISTER`
- 文件：`components/mini_tree/drivers/my/my_spi_drv.c`（新建）
- 模板：
  ```c
  #include "spi_client.h"
  #include "device.h"
  #include "driver.h"
  #include "VFS.h"
  static int my_probe(struct device* dev) { return spi_client_probe(dev); }
  static int my_remove(struct device* dev) { return spi_client_remove(dev); }
  DRIVER_REGISTER(my_dev, "myvendor,my-spi-slave", my_probe, my_remove)
  ```
- 复用了 `spi_client` 通用路径：自动 attach / detach、自动持 io_lock、自动 bus 锁 + reconfigure。
- 若需要自定义 `fops`（不直接复用 spi_client），可参考 `ws2812_drv.c` 写完整 `file_operations` + `device_lc_bind`。

### Step 5. 添加文件到 CMake
- 文件：`components/mini_tree/CMakeLists.txt`
- 在 `DRIVER_SRCS` 列表追加：
  ```cmake
  set(DRIVER_SRCS
      "drivers/ws2812/ws2812_drv.c"
      "drivers/fft/fft_spi_drv.c"
      "drivers/my/my_spi_drv.c"        # 新增
      "vfs/spi/spi_bus.c"
      "vfs/spi/spi_client.c"
  )
  ```
- 在 `INCLUDE_DIRS` 追加 `drivers/my`。
- 在 `add_custom_command` 的 `DEPENDS` 中也加上这个 `.c`（dtc-lite 扫描 compat 字符串用）。

### Step 6. 应用层调用
- 文件：`components/app/src/app_my_task.cpp`（新建 FreeRTOS 任务）
- 关键调用序列：
  ```c
  // 1) 查设备（按 DTS label 或 name）
  struct device* dev = device_find_by_label("my_dev");
  if (!dev) { ESP_LOGE(TAG, "not found"); osal_task_self_delete(); }

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

### Step 7. 验证 / 调试验证链
1. 启动日志搜 `[spi_bus]` 出现 `controller probe OK: host=2 mosi=11 miso=13 sclk=12 children=1` → 总线 OK。
2. 搜 `[spi_client]` 出现 `client probe OK: cs=10 mode=0` → 客户端 OK。
3. `[fft_slave status=N (expect PROBED=3)]` 在 app 任务里打 `status`，确认 `DEVICE_STATUS_PROBED`。
4. `device_open failed` 时检查：DTS `status = "okay"` 是否生效、`&spi1` 引脚是否被 `&spi1` 覆盖、`cs-pin` 是否在 `&my_dev`。
5. 用逻辑分析仪抓 SCK/MOSI/CS，配合主控验证数据流。

### 7.1 SPI 调用栈速查（运行时）

```
device_write(dev, buf, len, tmo)                     [VFS wrapper, 持 dev->lock]
   └─ spi_operations_template.write = spi_write      [vfs/spi/spi_client.c]
        └─ dev_lc_io_begin (持 io_lock)              [dev_lifecycle.c]
            └─ hal_spi_xfer_begin(&priv->ctx, tmo)   [hal/spi/hal_spi.c]
                 ├─ osal_mutex_lock(host->bus_mutex)
                 └─ hal_spi_bus_reconfigure(host, cfg)
            └─ priv->ctx.host->bus.write(host, data, len)
                 └─ spi_bus_write_impl               [hal/spi/hal_spi.c]
                      ├─ spi_slave_setup_trans
                      └─ spi_slave_transmit          [ESP-IDF]
            └─ hal_spi_xfer_end
                 └─ osal_mutex_unlock
        └─ dev_lc_io_end
```

### 7.2 SPI 驱动文件清单

| 关注点              | 文件                                                  | 关键 API                                |
| ------------------- | ----------------------------------------------------- | --------------------------------------- |
| DTS 父节点模板      | `board/dtsi/esp32s3-spi.dtsi`                         | `&soc { spi1: spi@0 { compatible = "esp32,spi" } }` |
| DTS 子节点模板      | `board/dtsi/esp32s3-spi.dtsi`                         | `fft_slave` 或自定义子节点              |
| 板级实例化          | `board/dts/esp32-s3-devkitc-1.dts`                    | `&spi1 / &fft_slave { status=okay }`    |
| SPI 默认常量        | `board/dt-bindings/spi/spi-parameter.h`               | `SPI_DEFAULT_*`                         |
| 总线驱动 (VFS)      | `vfs/spi/spi_bus.c`                                   | `spi_controller_probe/remove`           |
| 客户端驱动 (VFS)    | `vfs/spi/spi_client.c`                                | `spi_client_probe/remove`               |
| HAL 总线            | `hal/spi/hal_spi.c`                              | `hal_spi_bus_host_init / xfer_begin/end` |
| HAL Bus vtable      | `bus/spi/spi_bus.h`                                   | `struct hal_spi_bus { write/read/... }` |
| HAL Bus host 状态   | `bus/spi/spi_bus.h`                                   | `struct hal_spi_bus_host`               |
| HAL ctx (instance)  | `hal/spi/hal_spi.h`                                   | `struct hal_spi_ctx`                    |
| 业务驱动 (FFT)      | `drivers/fft/fft_spi_drv.c`                           | `DRIVER_REGISTER(fft_spi, ...)`         |
| ioctl 命令字        | `vfs/spi/spi_vfs.h`                                   | `SPI_CMD_READ/QUEUE_TX/GET_TRANS_RESULT/DEINIT` |
| 应用任务            | `components/app/src/app_spi_task.cpp`                 | `device_find_by_label("fft_slave")`     |

---

## 8. 关键设计原则（速记）

1. **SIOF（Static Initialization Order Fiasco）防御**：`g_system_os_initialized` 在 `mini_tree_pre_os_init` 后置 `true`，禁止 C++ 静态构造函数在 OS/EventBus 就绪前偷跑。
2. **IEC 61508 §7.4.3.1 持锁 check-then-act**：`device_open/close/read/write/ioctl` 全部在 `device_lock` 保护下做状态检查 + ops 调用，阻断多线程重入与 TOCTOU。
3. **持锁返回契约（dev_lc_remove_drain）**：成功时调用方仍持有 `lc->io_lock`，必须与 `dev_lc_remove_finish` 严格配对；中间不允许 `dev_lc_io_begin` 等会抢同锁的 API。
4. **DRIVER_REGISTER 编译期绑定**：dtc-lite 扫描 `.c` 源里 `DRIVER_REGISTER(name, compat, ...)` 把 `board_driver_probe_name` 收录入 `s_probe_table[]`，运行时直接按 id 索引，无 `strcmp`。
5. **DTS 无序全解耦**：多个 `/ { }` 任意顺序合并，`&label` 延迟合并或虚空创生，dtsi 中间可插 `&soc`。
6. **HAL Bus vs Instance 两层**：`hal_spi_bus_host` 全局常驻（bus mutex / ref_count / hw_inited），`hal_spi_ctx` 是 interface 实例（attach/detach 时增减 ref_count）。ESP32 slave 一 host 只能一 active_ctx（不同 CS/mode 拒绝 attach）。
7. **OSAL 抽象**：mutex / spinlock / sem / queue / task 全部后端可选（FreeRTOS 当前选中）；ISR 检测用 `mrs ipsr`，所有 lock/unlock 在 ISR 中**直接拒绝**。
8. **SPSC FIFO 内存序**：xtensa/ARM Cortex-A 双核下用 acquire/release 协议；`w_ptr` / `r_ptr` 间 padding 防 false sharing（`m_buffer.h`）。

---

## 9. 常见坑 & 调试建议

1. **`status = "okay"` 不生效** → 检查 dtsi 是否 include，板级 dts 是否 include 该 dtsi，是否同时设置了 `compatible`。
2. **`device_open failed`** → 99% 是设备树问题。先 `device_get_status(dev)` 看是不是 PROBED；否则查 dtc-lite 输出 `board_devtable.c` 中是否有此节点。
3. **SPI 主机收不到数据** → 确认 `max-trans-buffer` ≥ 实际负载；`cs-pin` 与 `spi-mode` 与主控一致；从机的 `clock_speed_hz` 仅作记录（实际由主控定钟）。
4. **WS2812 颜色错位** → 改 `color-order` (默认 `"grb"`)。
5. **链接器报 `system_safety_hardware_shutdown` undefined** → `CONFIG_SAFETY_SHUTDOWN=n` 时它是弱符号，链接期没问题；若 `=y` 则必须由 `board_driver.c` 提供强符号。
6. **多 pass 探测超时** → `[board_drv] EPROBE_DEFER stall`：phandle 依赖未就绪，确认父节点 `compatible` 与驱动 `DRIVER_REGISTER` 名字完全一致。
7. **反汇编生成 `.lst`** → `CONFIG_BUILD_DISASM=y`，由 `cmake/disasm.cmake` 触发 `objdump -d -S`。
8. **Flash scrubber CRC** → 链接后由 `post_build_crc.py` 用真实 CRC 覆盖 `system_scrubber_crc_stub.h` 占位（构建系统会拷贝 stub → 链接期先占，build 完再覆盖；运行时实际值需另行确认）。

---

## 10. 附录：仓库内外部参考

- 设备树详细规范: [components/mini_tree/board/docs/devicetree.md](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/board/docs/devicetree.md)
- 项目总览与烧录: [README.md](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/README.md)
- SPI 客户端 VFS 实现: [vfs/spi/spi_client.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/vfs/spi/spi_client.c)
- SPI 总线 VFS 实现: [vfs/spi/spi_bus.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/vfs/spi/spi_bus.c)
- HAL SPI 实现: [hal/spi/hal_spi.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/hal/spi/hal_spi.c)
- WS2812 驱动: [drivers/ws2812/ws2812_drv.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/drivers/ws2812/ws2812_drv.c)
- FFT SPI 驱动: [drivers/fft/fft_spi_drv.c](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/drivers/fft/fft_spi_drv.c)
- 板级 DTS: [board/dts/esp32-s3-devkitc-1.dts](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/board/dts/esp32-s3-devkitc-1.dts)
- SPI dtsi: [board/dtsi/esp32s3-spi.dtsi](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/board/dtsi/esp32s3-spi.dtsi)
- dtc-lite 编译器: [tools/dtc-lite.py](file:///d:/Heterogeneous-Multicore-project/ESP32-S3/components/mini_tree/tools/dtc-lite.py)
