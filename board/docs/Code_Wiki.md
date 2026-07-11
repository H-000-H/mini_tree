# mini_tree v2.0 — Code Wiki

> 适用仓库: `Heterogeneous-Multicore/mini_tree`
> 平台: 平台无关中间件（支持 STM32 / CH32 / ESP32 等多平台移植）
> 框架: Linux 风格设备树 + 编译期 probe 表 + 平台 HAL + 虚拟中断

---

## 1. 项目总览

`mini_tree` 是异构多核项目的中间件主仓库，提供一套与具体 MCU 解耦的板级框架，借鉴 Linux 设备模型与设备树思想。本仓库**不包含任何具体平台的 HAL 实现**：所有 `hal_*.c` 仅保留接口签名与参数校验（空函数化），由各平台工程提供具体实现并链接。各平台的 DTS（`board/dts/`）与 DTSI（`board/dtsi/`）同样不在本仓库中，由平台工程自带，本仓库仅保留 `board/dt-bindings/` 通用宏常量。

### 1.1 顶层目录结构

```
mini_tree/
├── algorithm/buffer/          # SPSC 无锁环形 FIFO + 双缓冲
├── board/                     # 板级框架核心
│   ├── docs/                  # 框架文档 (Code_Wiki / devicetree / linux-vs-mini_tree)
│   ├── dt-bindings/           # DTS 宏常量 (spi, uart, tim)
│   ├── include/               # device.h, driver.h, bus.h, VFS.h, dev_lifecycle.h
│   └── src/                   # board_device.c, board_driver.c, bus.c, dev_lifecycle.c 等
├── bus/                       # 总线抽象 (零翻译直通 HAL)
│   ├── spi/                   # spi_bus.c/h
│   └── uart/                  # uart_bus.c/h
├── cmake/                     # CMake 模块 (etl.cmake, rust.cmake, disasm.cmake)
├── core/                      # 核心服务 (event_bus, buffer_pool, production_log, system_log)
├── drivers/flash/             # W25Q64 SPI Flash 驱动 (板级驱动示例)
├── hal/                       # 硬件抽象层 (头平台无关, .c 空函数)
│   ├── adc/, amp/, dac/       # 各外设 HAL
│   ├── gpio/                  # hal_gpio.h + hal_gpio_stm32.c (空函数)
│   ├── i2c/, spi/, uart/      # 总线型 HAL
│   ├── storage/               # hal_flash.h
│   ├── system/                # hal_stm32f407.c (空函数), hal_wdt.h, hal_rtc.h
│   └── tim/                   # hal_tim.c (空函数)
├── interrupt/                 # 虚拟中断框架 (interrupt.c/h)
├── osal/                      # OS 抽象层 (FreeRTOS / RT-Thread / NULL 后端)
├── system_c/                  # C 版系统运行时
├── system_cpp/                # C++ 版系统运行时
├── time_slice/                # 时间片调度 (xtask)
├── tools/                     # dtc-lite / genconfig / menuconfig / post_build_crc
├── vfs/                       # VFS 桥接 (adc/can/dac/gpio/i2c/i2s/rtc/spi/tim/uart/usb)
├── CMakeLists.txt             # 主构建文件
├── Kconfig                    # 配置选项
├── .config                    # 当前配置
└── .clangd                    # clangd 配置 (相对路径)
```

### 1.2 二进制构件

| 构件 | 来源 |
|------|------|
| 框架运行时 (init/wdt/scrubber) | `system_c*/src/*.c` 或 `system_cpp*/src/*.cpp` |
| OSAL 后端 | `osal/src/osal_{null,freertos,rtthread}.c`（由 Kconfig 选择） |
| HAL (CPU/GPIO/SPI/UART/...) | `hal/*/*.c`（空函数）+ 各平台工程提供具体实现 |
| VFS (SPI/UART/GPIO/...) | `vfs/*/*.c`（如 `vfs-spi.c`, `vfs-uart.c`, `vfs-gpio.c`） |
| 板级驱动 | `drivers/flash/w25q64_spi_drv.c`（示例：W25Q64） |
| DTS 编译期生成代码 | `tools/dtc-lite.py` → `board_devtable.c/.h`, `board_probe.c`, `board_nodes.h` |
| Kconfig 生成 config.h | `tools/genconfig.py` → `generated/kconfig/mini_tree/config.h` |
| 链接后 CRC | `tools/post_build_crc.py` → 覆盖 `system_scrubber_crc_stub.h` 占位 |

---

## 2. 整体架构

### 2.1 分层模型

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Application Layer                           │
│            用户 main + 业务任务 (调用 device_open/read/ioctl)         │
└────────────────────────────┬─────────────────────────────────────────┘
                             │  device_open / device_read / device_ioctl
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│                  board 层 (board/include, board/src)                  │
│   device.h · driver.h · bus.h · dev_lifecycle.h · VFS.h              │
│   device_tree_init → board_driver_probe_all → board_driver_remove    │
│   device_find / device_get_prop_* / device_lock                       │
└────────────────────────────┬─────────────────────────────────────────┘
                             │  board_devtable.c (dtc-lite 编译期生成)
                             │  DRIVER_REGISTER(...) 编译期绑定
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│                       VFS 层 (vfs/*)                                 │
│   vfs-spi.c · vfs-uart.c · vfs-gpio.c · vfs-adc.c · vfs-tim.c · ...  │
│   节点 → file_operations 入口桥接, 持 dev->lock                        │
└────────────────────────────┬─────────────────────────────────────────┘
                             │  bus.write / bus.read
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│                       bus 层 (bus/spi, bus/uart)                     │
│   spi_bus.c · uart_bus.c — 零翻译直通 HAL, 平台中立共享代码            │
└────────────────────────────┬─────────────────────────────────────────┘
                             │  hal_spi_* / hal_uart_* / hal_gpio_*
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│                  HAL 层 (hal/*, .c 空函数, 各平台提供实现)             │
│   hal_spi.h + hal_spi_stm32.c · hal_uart.h + hal_uart_stm32.c · ...  │
│   头文件平台无关, .c 仅签名 + 参数校验                                  │
└────────────────────────────┬─────────────────────────────────────────┘
                             │  LL 库 / ESP-IDF / CH32 库
                             ▼
┌──────────────────────────────────────────────────────────────────────┐
│                        厂商 SDK / 寄存器                              │
└──────────────────────────────────────────────────────────────────────┘

横向支撑层：
  core/        — event_bus, buffer_pool, production_log, system_log
  osal/        — mutex/spinlock/sem/queue/task (FreeRTOS / RT-Thread / NULL)
  system_c|cpp/— 系统运行时 (init, wdt, scrubber, task_manager, safe_state)
  interrupt/   — VIRQ 虚拟中断号 + 上半部/下半部分离
  algorithm/   — SPSC 无锁环形 FIFO, 双缓冲
  tools/       — dtc-lite, genconfig, menuconfig, post_build_crc
```

### 2.2 启动时序

中间件采用 IEC 61508 §7.4.3 二段式点火。各平台 `main()`（C）或入口函数需按以下顺序调用：

```
平台 main() (各平台入口)
   │
   ▼ mini_tree_pre_os_init()                 [system_c/src/system_init.c]
   ├─ IRQ_DISABLE
   ├─ safe_state_check_bootloop              (bootloop 防烧穿)
   ├─ system_wdt_init_rtc                    (RTC WDT)
   ├─ device_tree_init()                     [board_device.c, 全量静态分配 + 互斥锁池]
   ├─ event_bus_init()                       [core/event_bus.c]
   └─ g_system_os_initialized = true
   │
   ▼ board_register_all_drivers()           [编译期 DRIVER_REGISTER, 当前为空]
   │
   ▼ mini_tree_start_tasks()                 [system_init.c]
   ├─ event_bus_start
   ├─ board_driver_probe_all()               ◀── 核心: 3-pass 依赖解析 + 状态机
   ├─ system_wdt_init (TWDT)
   ├─ system_scrubber_init/start             (Flash bit-rot 后台扫描)
   └─ event_bus_seal
   │
   ▼ 用户业务任务启动 (task_manager_create_task)
   │
   ▼ system_init_complete() → IRQ_ENABLE
   │
   ▼ CONFIG_OSAL_NULL ?  while(1) { mini_tree_system_loop(); }  :  vTaskStartScheduler();
```

参考 `system_c/include/system_init.h` 文件头注释中的最小 main 模板。

---

## 3. 主要模块职责

### 3.1 `board/` — 板级核心

| 文件 | 职责 |
|------|------|
| `board_device.c` | `struct device` 实例表、设备查找、属性读取、状态机、VFS 转发层（持锁） |
| `board_driver.c` | `board_driver_probe_all` / `board_driver_remove_all`（3-pass 依赖解析）+ IEC 61508 安全停机子系统 |
| `bus.c` | `bus_controller` / `bus_client` 绑定表（如 SPI 父子节点） |
| `dev_lifecycle.c` | 设备生命周期（open/close/io/remove 的引用计数 + 持锁契约，CAS 哨兵版） |
| `config_store.c` | JSON 配置存储（Kconfig 可选） |
| `task_config.c` / `task_utils.c` | 任务优先级 / 栈监测辅助 |
| `dt-bindings/spi/spi-parameter.h` | SPI 默认参数宏（host_id / mode / freq / queue） |
| `dt-bindings/uart/uart-parameter.h` | UART 默认参数宏 |
| `dt-bindings/tim/tim-parameter.h` | 定时器默认参数宏 |
| `docs/devicetree.md` | dtc-lite 解析规则、节点约定、属性契约 |
| `docs/Code_Wiki.md` | 本文档 |
| `docs/linux-vs-mini_tree-device-model.md` | Linux 设备模型对照学习指南 |

> 注意：`board/dts/*.dts` 与 `board/dtsi/*.dtsi` 不在本仓库中，由各平台工程提供；`board/dt-bindings/` 仅含平台无关的通用宏常量。

### 3.2 `hal/` — HAL 层（空函数化设计）

HAL 层头文件平台无关，定义了所有外设的抽象接口；`.c` 文件空函数化（仅参数校验与状态机骨架）。各平台工程通过 `hal_*_<platform>.c`（如 `hal_gpio_stm32.c`、`hal_spi_stm32.c`、`hal_uart_stm32.c`）覆盖空函数提供具体实现，由 CMake 根据 Kconfig 选择链接。

| 子目录 | 头文件 | .c 文件 | 说明 |
|--------|--------|---------|------|
| `hal/gpio/` | `hal_gpio.h` | `hal_gpio_stm32.c`（空函数） | GPIO 配置/读写/toggle，含 fast path |
| `hal/spi/` | `hal_spi.h` | `hal_spi_stm32.c`（空函数） | SPI Host/Device, sync/DMA transfer, slave 不支持返回 NOTSUPP |
| `hal/uart/` | `hal_uart.h` | `hal_uart_stm32.c`（空函数） | UART 收发 |
| `hal/adc/` | `hal_adc.h` | `hal_adc.c` | ADC 采样 + DMA |
| `hal/dac/` | `hal_dac.h` | `hal_dac.c` | DAC 输出 |
| `hal/i2c/` | `hal_i2c.h` | `hal_i2c.c` | I2C 主从 |
| `hal/tim/` | `hal_tim.h` | `hal_tim.c` | 定时器/PWM |
| `hal/amp/` | `hal_amp.h` | `hal_amp.c` | AMP 多核（核间通信、副核启动） |
| `hal/storage/` | `hal_flash.h`, `hal_storage.h` | — | Flash 抽象（W25Q64 等驱动基于此） |
| `hal/system/` | `hal_platform_safety.h`, `hal_rtc.h`, `hal_sdio.h`, `hal_wdt.h` | `hal_stm32f407.c`（空函数） | 平台级（看门狗/RTC/安全停机） |

设计原则：
- 头文件不引入任何厂商头（不 `#include "stm32f4xx_ll.h"` 等）
- 返回值统一用 `int`，错误码使用 `VFS.h` 中的 `VFS_ERR_*`
- 不使用 `enum`，模式/配置由 DTS 宏值直接传递（厂商宏值零翻译直投）
- bus 层与 vfs 层不直接 `#include` 厂商头，仅通过 HAL 抽象接口访问硬件

### 3.3 `bus/` — 总线抽象

| 文件 | 职责 |
|------|------|
| `bus/spi/spi_bus.{c,h}` | SPI 总线子系统 bus 层：静态池（host/client/bridge pool）、引用计数、零翻译直通 HAL |
| `bus/uart/uart_bus.{c,h}` | UART 总线抽象 |

bus 层是平台中立共享代码，**不做任何 `#ifdef` 平台区分**：直接转发到 HAL 函数，由各平台 HAL `.c` 决定是否支持（不支持则返回 `VFS_ERR_NOTSUPP`）。

### 3.4 `vfs/` — VFS 桥接

每个外设目录提供一个或多个 `DRIVER_REGISTER` 注册点，把 DTS 节点桥接到 `file_operations` 入口：

| 目录 | 文件 | 注册的 compatible |
|------|------|--------------------|
| `vfs/spi/` | `vfs-spi.c` | SPI Host controller + bus client（Linux 风格三层嵌套） |
| `vfs/uart/` | `vfs-uart.c` | UART 设备 |
| `vfs/gpio/` | `vfs-gpio.c` | GPIO 设备 |
| `vfs/adc/` | `vfs-adc.c` | ADC 设备 |
| `vfs/dac/` | `vfs-dac.c` | DAC 设备 |
| `vfs/tim/` | `vfs-tim.c` | 定时器/PWM 设备 |
| `vfs/i2c/` | `vfs-i2c.c` | I2C 设备 |
| `vfs/i2s/` | `vfs-i2s.c` | I2S 设备 |
| `vfs/rtc/` | `vfs-rtc.c` | RTC 设备 |
| `vfs/can/` | `vfs-can.c` | CAN 设备 |
| `vfs/usb/` | `vfs-usb.c` | USB 设备 |

### 3.5 `core/` — 核心公共设施

| 文件 | 职责 |
|------|------|
| `event_bus.{c,h}` | 事件总线 (C API)：`event_bus_post / subscribe / post_from_isr / seal / drop_count`。框架事件 ID：`EVENT_SYS_BOOT / READY / FAULT / DEVICE_REMOVED` |
| `event_bus.hpp` | C++ 包装（订阅者模式） |
| `buffer_pool.{c,h}` | 预分配定长缓冲池 (`bp_pool`)，支持 `BP_ALIGN_DMA` 32 字节对齐、位图分配、ISR 安全 (`bp_alloc_isr`)、峰值追踪 |
| `production_log.{c,h}` | 生产级黑匣子日志 (Ring buffer + CRC) |
| `printf_output.{c,h}` | `my_printf_output()`，Kconfig 选 `SYS_LOG_USE_PRINTF` 时使用 |
| `system_log.h` | `SYS_LOGI/W/E` 宏分发到所选后端（OSAL / printf），同时提供 `DRV_LOGI/W/E`（推到 production log） |
| `safe_state.h` | `enter_safe_state / safe_state_check_bootloop / safe_state_nmi_emergency_stamp` |
| `compiler_compat.h` | GNU 扩展（container_of, likely/unlikely, COMPAT_ATOMIC_*） |
| `critical_data.h` | 关键数据 CRC 保护 |
| `bh/` | bottom-half 队列辅助（ISR → 任务上下文搬运，interrupt.h 中也提供新版接口） |

### 3.6 `system_c/`, `system_cpp/` — 系统运行时

由 `.config` 的 `CONFIG_SYSTEM_C / CONFIG_SYSTEM_CPP` 决定选 C 还是 C++（默认 C++）。

| 文件 | 职责 |
|------|------|
| `system_init.{c,cpp}` | `mini_tree_pre_os_init / start_tasks / system_init_complete / system_loop`，实现 IEC 61508 §7.4.3 二段式点火 |
| `system_wdt.{c,cpp}` | TWDT（任务看门狗）+ RTC WDT |
| `system_scrubber.{c,cpp}` | Flash bit-rot 后台扫描 + CRC 基线（占位 stub：`system_scrubber_crc_stub.h` → 链接后由 `post_build_crc.py` 覆盖） |
| `task_manager.{c,cpp}` | `task_manager_create_task`，自动 `system_wdt_subscribe` |
| `safe_state.c` / `safe_state.h` | bootloop 防烧穿（仅 system_cpp 提供；`core/include/safe_state.h` 提供公共接口） |
| `system_cmd.cpp`（仅 C++） | 系统命令行（shell 风格） |
| `system_runtime.cpp`（仅 C++） | 运行时统计/诊断 |

### 3.7 `osal/` — OS 抽象层

提供统一 API：mutex / recursive_mutex / spinlock / sem / queue / task / 时间 / 内存。由 Kconfig 选择后端：
- `osal_null.c`（默认，裸机 + 原子 CAS 锁）
- `osal_freertos.c`（FreeRTOS）
- `osal_rtthread.c`（RT-Thread）

特性：
- ISR 检测：`mrs ipsr` (ARMv7-M/Cortex-M) 或 `csrr mcause` (RISC-V)
- 池分配：`osal_pool_claim/release`，ISR 安全
- 强约束：所有 `lock/unlock/create/destroy` 在 ISR 中**直接拒绝**（返回 `-1`）
- AMP 模式：`CONFIG_AMP_MODE=y` 时 osal_null 的互斥锁等同步原语使用原子 CAS 实现；单核退化为关中断

### 3.8 `interrupt/` — 虚拟中断框架

`interrupt/interrupt.{c,h}` 提供平台无关的虚拟中断号（VIRQ）+ 上半部 / 下半部分离机制。详见第 8 节。

### 3.9 `algorithm/` — 算法

| 文件 | 职责 |
|------|------|
| `buffer/circle_fifo_buffer.c` / `buffer.h` | SPSC（单生产者单消费者）无锁环形 FIFO，acquire/release 内存序，cache line 隔离 `w_ptr` / `r_ptr` 防 false sharing。可用于双核 SPSC 通路（如 ADC DMA → 处理任务） |
| `buffer/double_buffer.c` | 双缓冲（ping-pong） |

### 3.10 `tools/` — 构建工具

| 文件 | 职责 |
|------|------|
| `dtc-lite.py` | **核心工具**。无序全解耦版 DTS 编译器（向 Linux 看齐）。CLI 入口，调用 `dtc_lite/` 包 |
| `dtc_lite/grammar.py` | Lark 文法（Earley 算法） |
| `dtc_lite/parser.py` | Transformer 把 parse tree 转 AST |
| `dtc_lite/dts_ast.py` | `DtsNode` / `DtsProperty` 数据结构 |
| `dtc_lite/compiler.py` | `#include` 预处理、overlay 合并、驱动扫描、cpp 提取厂商头宏 |
| `dtc_lite/generator.py` | C 代码生成 |
| `dtc_lite/platform.py` | 设备树平台节点判定（识别 `simple-bus`、`gpio-controller`、`#*-cells`、`device_type=cpu` 等基础设施节点，跳过 VFS 驱动绑定） |
| `dtc_lite/main.py` | argparse 入口，支持 `-I` / `-D` 透传给 cpp |
| `genconfig.py` | Kconfig → C 头文件 `config.h` |
| `menuconfig.py` | 文本菜单配置器 |
| `post_build_crc.py` | 链接后计算 scrubber CRC 基线 |

---

## 4. 关键数据结构与 API

### 4.1 `struct device_node`（编译期只读，dtc-lite 生成）

```c
struct device_node {
    const char*         name;
    const char*         label;          /* DTS label (如 spi_dev0) */
    const char*         compatible;     /* "vendor,spi-master" 等 */
    const char*         path;           /* DTS 全路径 (如 /soc/spi@1) */
    const struct device_property* props;
    const device_id_t*  deps;           /* cascade child 列表 */
    const struct device_reg* regs;     /* reg 条目（按 #address-cells / #size-cells 分组） */
    const struct device_irq* irqs;      /* interrupt 表（按 #interrupt-cells 分组） */
    uint8_t             status;         /* 编译期默认状态 */
    uint8_t             criticality;   /* DEVICE_CRIT_IGNORE/WARNING/FATAL */
    uint8_t             flags;         /* DEVICE_FLAG_DIRECT 等 */
    uint8_t             prop_count, dep_count, reg_count, irq_count;
};
```

### 4.2 `struct device`（运行时实例）

```c
struct device {
    const struct device_node* node;       /* 指向 dtc-lite 生成的只读节点 */
    enum device_status        status;
    void*                     priv_data;  /* 驱动私有 (VFS 层) */
    const struct file_operations* ops;    /* 由 vfs 层注入 */
    struct osal_mutex*        lock;       /* 递归锁, device_lock 用 */
    struct dev_lifecycle      lc;        /* open/io/close/remove 状态机 */
    void*                     platform_data; /* board 层注入的静态数据 */
};
```

### 4.3 `struct file_operations`（VFS 入口）

```c
struct file_operations {
    int (*init)   (struct device* dev);
    int (*open)   (struct device* dev, void* arg);
    int (*close)  (struct device* dev);
    int (*write)  (struct device* dev, const void* buffer, size_t len, uint32_t timeout_ms);
    int (*read)   (struct device* dev, void* buffer, size_t len, uint32_t timeout_ms);
    int (*ioctl)  (struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms);
    int (*suspend)(struct device* dev);
    int (*resume) (struct device* dev);
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

转换由 `device_status_can_transit` 在 `s_status_lock` 自旋锁保护下校验。状态枚举：

| 状态 | 含义 |
|------|------|
| `DEVICE_STATUS_DISABLED` | 编译期 DTS 标记 `status = "disabled"`，或运行期依赖永久不可用 |
| `DEVICE_STATUS_UNINIT` | 初始（`device_tree_init` 中） |
| `DEVICE_STATUS_READY` | 已就绪，等待 probe |
| `DEVICE_STATUS_PROBED` | probe 完成，ops 已绑定，可被 open |
| `DEVICE_STATUS_RUNNING` | 已 open（有活跃引用） |
| `DEVICE_STATUS_SUSPENDED` | 暂停（PM） |
| `DEVICE_STATUS_ERROR` | probe 失败或运行时故障 |
| `DEVICE_STATUS_REMOVED` | 已卸载 |

### 4.5 关键全局 API（`device.h`）

- 查找：`device_find / find_by_label / find_by_id / find_by_path / find_by_compatible / get_phandle_dev`
- 属性：`device_get_prop_int / str / bool / int_array / get_reg / get_irq`
- 运行时：`device_set_status / set_priv / get_priv`
- 遍历：`device_get_first / get_next / get_count`
- 锁：`device_lock / unlock`（递归锁）
- 卸载：`device_ops_unregister`（持锁斩断 ops + priv_data）
- 生命周期：`device_lc / device_lc_bind`
- VFS 包装：`device_open / close / read / write / ioctl / suspend / resume`（**全部在持锁下做 check-then-act**，IEC 61508 §7.4.3.1）

### 4.6 `dev_lifecycle`（驱动 I/O 生命周期）

CAS 哨兵版：`opens` / `io_active` 使用 `COMPAT_ATOMIC_INT`，`-1` 哨兵表示 "teardown 已锁定"。

| 状态 | 含义 |
|------|------|
| `DEV_LC_UNINITIALIZED` | 初始（`device_tree_init` 中） |
| `DEV_LC_LIVE` | probe 完成，接收 open/io |
| `DEV_LC_REMOVING` | remove 已开始，拒绝新 open/io |
| `DEV_LC_DEAD` | 已卸载 |

关键 API：
- `dev_lc_open_begin` 返回 1=首次、0=非首次
- `dev_lc_io_begin` 返回 `VFS_OK/ERR`
- `dev_lc_remove_drain` 持锁返回（持锁契约，调用方仍持有 `lc->io_lock`，必须与 `dev_lc_remove_finish` 严格配对）

### 4.7 `DRIVER_REGISTER` 宏

```c
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn)        \
    int board_driver_probe_##name(struct device* dev)             \
    {                                                             \
        return probe_fn(dev);                                     \
    }                                                             \
    int board_driver_remove_##name(struct device* dev)            \
    {                                                             \
        return remove_fn(dev);                                    \
    }
```

被 `dtc-lite.py` 在编译期扫描所有驱动源码目录，把 `board_driver_probe_<name>` 收录入 `s_probe_table[]`。运行期按 id 索引调用，无 `strcmp` 匹配，开销恒定。

### 4.8 OSAL 主要 API

| 分类 | 接口 |
|------|------|
| 时间 | `osal_time_ms / delay_ms / ticks_from_ms / timeout_to_ticks` |
| ISR 检测 | `osal_in_isr` |
| 调度/中断 | `osal_sched_freeze / int_freeze`（单向不可恢复） |
| 自旋锁 | `osal_spinlock_init/lock/unlock`（关中断临界区或原子 CAS） |
| 互斥锁 | `osal_mutex_create{,_static}{,_plain,_recursive,_typed}`、`lock/unlock/destroy` |
| 信号量 | `osal_sem_create_binary{,_static} / wait / post / post_from_isr` |
| 队列 | `osal_queue_create / send / receive (+_from_isr)` |
| 任务 | `osal_task_create / create_handle / self_delete / get_stack_watermark` |
| 池分配 | `osal_pool_claim / release` |
| Panic | `OSAL_PANIC(fmt, ...)`、`CRITICAL_ASSERT(cond, fmt, ...)` |

---

## 5. 中断框架

### 5.1 虚拟中断号（VIRQ）

`interrupt/interrupt.h` 按 block 划分虚拟中断号，每块 `VIRTUAL_IRQ_BLOCK_SIZE = 8` 个 slot：

```c
#define VIRTUAL_IRQ_BLOCK_TABLE(X) \
    X(system) X(tim) X(gpio) X(adc) \
    X(uart) X(spi) X(i2c) X(user)
```

通过 `VIRQ(name, idx)` 宏计算得到虚拟中断号（如 `VIRQ(spi, 0)`、`VIRQ(gpio, 3)`）。各平台 HAL 在 ISR 中调用 `interrupt_virtual_dispatch(virq_num)` 即可触发上半部 + 下半部链路。

### 5.2 上半部 / 下半部分离

```
硬件中断触发
   │
   ▼ 平台 ISR 入口 (如 SPI DMA TC 中断)
   ├─ interrupt_virtual_dispatch(virq_num)             [interrupt.c]
   │   ├─ 读 interrupt_virtual_top_half[virq_num]      上半部回调
   │   │   └─ 上半部清硬件标志 + 决定是否需要下半部 (返回非零 = 需要)
   │   └─ 若需要 → bottom_half_submit_from_isr(fifo, work)
   │       ├─ COMPAT_ATOMIC_CAS work->pending 0→1
   │       ├─ fifo_write_data (SPSC 无锁队列)
   │       └─ 裸机: 置 pending_drain 标志
   │          RTOS: osal_sem_post_from_isr 唤醒 bottom_half_task
   └─ return from ISR
       │
       ▼ 消费端 (主循环 / 专用任务)
       ├─ 裸机 (CONFIG_OSAL_NULL):  mini_tree_system_loop() → bottom_half_poller_run()
       └─ RTOS:                    bottom_half_task_entry() → bottom_half_run_pending()
           └─ work->fn(work->arg)  执行下半部回调
               └─ 执行期间再触发 ISR → 置 rerun 标志, run_pending 结束后补跑
```

### 5.3 关键 API

| API | 作用 |
|-----|------|
| `interrupt_virtual_register(virq_num, top_half, work, arg)` | 注册虚拟中断的上半部回调与下半部工作 |
| `interrupt_virtual_dispatch(virq_num)` | ISR 内调用，自动衔接上半部与下半部 |
| `interrupt_hw_enable(irqn, priority)` | 使能硬件中断（平台 HAL 实现） |
| `interrupt_hw_disable(irqn)` | 关闭硬件中断 |
| `interrupt_bottom_half_submit(work)` | ISR 入队接口 |
| `interrupt_bottom_half_poll()` | 主循环执行下半部队列（裸机） |

### 5.4 设计要点

- 上半部在 ISR 内执行，必须轻量（清硬件标志、决定是否需要下半部）；**禁止** printf / 上锁 / 长时间阻塞
- 下半部在主循环或专用任务上下文执行，可调用任意 OSAL API
- SPSC 无锁 FIFO 队列，无锁无等待，多核安全
- `pending/executing/rerun` 三原子位实现合并与补跑，执行期间再触发不丢失
- 队列深度 `BOTTOM_HALF_QUEUE_DEPTH` 必须是 2 的幂

---

## 6. HAL 空函数化设计

### 6.1 中间件只保留签名

`hal/` 下的所有 `.c` 文件（如 `hal_gpio_stm32.c`、`hal_spi_stm32.c`、`hal_uart_stm32.c`、`hal_stm32f407.c`）默认是空函数：仅做参数校验（`if (!pdev) return VFS_ERR_INVAL;`）与状态机骨架，**不调用任何厂商 LL 库**。

```c
/* 示例: hal/gpio/hal_gpio_stm32.c 中的空函数 */
int hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level)
{
    if (!pdev)
        return VFS_ERR_INVAL;

    /**<零分支映射：通过位移运算同时兼容高电平(BSRR低16位)与低电平(BSRR高16位)>*/

    return VFS_OK;
}
```

### 6.2 各平台提供具体实现

各平台工程在自己的目录下提供同名文件的实现版本（或新建 `hal_gpio_<vendor>.c`），由 CMake 根据 Kconfig 选择链接：

```cmake
# CMakeLists.txt 节选
if(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_STM32=y")
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_stm32.c")
    set(HAL_PLATFORM_SRCS "hal/system/hal_stm32f407.c")
    set(NEW_ARCH_SRCS
        "hal/spi/hal_spi_stm32.c"
        "hal/uart/hal_uart_stm32.c"
    )
elseif(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_CH32=y")
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_ch32.c")
else()
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_esp32.c")
endif()
```

平台工程可通过 `target_sources(mini_tree PRIVATE ...)` 或在 `mini_tree` 库构建后通过自己的库覆盖链接（弱符号或同文件覆盖）。

### 6.3 头文件设计原则

- **不引入厂商头**：`hal_*.h` 仅 `#include <stdint.h>`、`VFS.h`、`compiler_compat.h` 等平台无关头
- **不用 enum**：模式/配置由 DTS 宏值直接传递（DTSI 厂商宏值零翻译直投），避免 HAL 层重复定义 enum 去映射厂商值
- **返回值统一用 int**：错误码使用 `VFS.h` 中的 `VFS_ERR_*`
- **bus/vfs 层不直接 include 厂商头**：仅通过 HAL 抽象接口访问硬件

---

## 7. 依赖关系

### 7.1 内部模块依赖（按 `#include`）

```
应用 main.c
  └→ system_init.h        (mini_tree_pre_os_init / start_tasks)
        ├→ device.h       (device_tree_init)
        ├→ event_bus.h    (event_bus_init)
        └→ driver.h       (board_register_all_drivers)

vfs/spi/vfs-spi.c
  ├→ hal_spi.h            (HAL 接口)
  ├→ device.h / driver.h / VFS.h
  └→ osal.h

bus/spi/spi_bus.c
  ├→ hal_spi.h            (HAL 接口)
  ├→ device.h / bus.h
  └→ osal.h

hal/spi/hal_spi_stm32.c  (平台实现, 各平台工程覆盖)
  ├→ hal_spi.h           (接口)
  ├→ interrupt.h         (虚拟中断注册)
  ├→ osal.h
  └→ 厂商 LL 库 (STM32: stm32f4xx_ll_spi.h 等)
```

### 7.2 DTS → C 生成依赖

```
<platform>/board/dts/board.dts
  + <platform>/board/dtsi/*.dtsi        ← #include <dt-bindings/...>
        │
        ▼  tools/dtc-lite.py
        │   (-I <VENDOR_INC_DIRS> -D <VENDOR_DEFINES> 透传给 cpp)
generated/board/mini_tree/
  ├─ board_nodes.h           (DEV_ID_* 枚举)
  ├─ board_devtable.h / .c   (s_devtable[], s_probe_table[], cascade[])
  ├─ board_handles.h
  ├─ board_probe.c           (board_driver_probe_all 实现)
  └─ dt_config_gen.h         (DTC_GEN_COUNT_* 宏)
```

### 7.3 Kconfig → config.h

```
mini_tree/Kconfig
  + mini_tree/.config
        │
        ▼  tools/genconfig.py
generated/kconfig/mini_tree/config.h
  → 影响 OSAL 后端选择 (osal_null / freertos / rtthread)
  → 影响 SYSTEM C/CPP 后端
  → 影响 HAL GPIO 后端 (stm32 / ch32 / esp32)
  → 影响各类 feature flag (WDT / SCRUBBER / SAFETY_SHUTDOWN / ...)
```

---

## 8. 关键设计原则（速记）

1. **SIOF（Static Initialization Order Fiasco）防御**：`g_system_os_initialized` 在 `mini_tree_pre_os_init` 后置 `true`，禁止 C++ 静态构造函数在 OS/EventBus 就绪前偷跑。

2. **IEC 61508 §7.4.3.1 持锁 check-then-act**：`device_open/close/read/write/ioctl` 全部在 `device_lock` 保护下做状态检查 + ops 调用，阻断多线程重入与 TOCTOU。

3. **持锁返回契约（dev_lc_remove_drain）**：成功时调用方仍持有 `lc->io_lock`，必须与 `dev_lc_remove_finish` 严格配对；中间不允许 `dev_lc_io_begin` 等会抢同锁的 API。

4. **DRIVER_REGISTER 编译期绑定**：dtc-lite 扫描 `.c` 源里 `DRIVER_REGISTER(name, compat, ...)` 把 `board_driver_probe_name` 收录入 `s_probe_table[]`，运行时直接按 id 索引，无 `strcmp`。

5. **DTS 无序全解耦**：多个 `/ { }` 任意顺序合并，`&label` 延迟合并或虚空创生，dtsi 中间可插 `&soc`。

6. **HAL 空函数化**：中间件 `hal/*.c` 仅保留签名 + 参数校验，各平台工程提供 `hal_*_<platform>.c` 具体实现并链接。bus/vfs 层不直接 include 厂商头，仅通过 HAL 抽象接口访问硬件。

7. **DTSI 厂商宏值零翻译直投**：dtsi 中 `#include <xxx_ll_*.h>` 通过系统 cpp 预处理，厂商 `#define` 宏值直接灌入 DTS 属性，再由 dtc-lite 输出到 C 代码，HAL 层不再写映射代码。

8. **HAL Bus vs Instance 两层**（总线型外设）：`hal_spi_bus_host` 全局常驻（bus mutex / ref_count / hw_inited），`hal_spi_dev` 是 instance（open/close 时增减 ref_count）。

9. **OSAL 抽象**：mutex / spinlock / sem / queue / task 全部后端可选（NULL / FreeRTOS / RT-Thread）；ISR 检测用 `mrs ipsr` 或 `csrr mcause`，所有 lock/unlock 在 ISR 中**直接拒绝**。

10. **SPSC FIFO 内存序**：双核下用 acquire/release 协议；`w_ptr` / `r_ptr` 间对齐到 cache line 防 false sharing（`buffer.h`）。

11. **虚拟中断上半部 / 下半部分离**：上半部 ISR 内清硬件标志 + 决定是否需要下半部；下半部在主循环或专用任务执行，可调用任意 OSAL API。`pending/executing/rerun` 三原子位实现合并与补跑，执行期间再触发不丢失。

---

## 9. 常见坑 & 调试建议

1. **`status = "okay"` 不生效** → 检查 dtsi 是否 include，板级 dts 是否 include 该 dtsi，是否同时设置了 `compatible`。

2. **`device_open failed`** → 99% 是设备树问题。先 `device_get_status(dev)` 看是不是 PROBED；否则查 dtc-lite 输出 `board_devtable.c` 中是否有此节点。

3. **多 pass 探测超时** → `[board_drv] EPROBE_DEFER stall`：phandle 依赖未就绪，确认父节点 `compatible` 与驱动 `DRIVER_REGISTER` 名字完全一致。

4. **链接器报 `system_safety_hardware_shutdown` undefined** → `CONFIG_SAFETY_SHUTDOWN=n` 时它是弱符号，链接期没问题；若 `=y` 则必须由 `board_driver.c` 提供强符号。

5. **反汇编生成 `.lst`** → `CONFIG_BUILD_DISASM=y`，由 `cmake/disasm.cmake` 触发 `objdump -d -S`。

6. **Flash scrubber CRC** → 链接后由 `post_build_crc.py` 用真实 CRC 覆盖 `system_scrubber_crc_stub.h` 占位（构建系统会拷贝 stub → 链接期先占，build 完再覆盖；运行时实际值需另行确认）。

7. **dtc-lite 找不到厂商宏** → 通过 CMake 变量 `VENDOR_INC_DIRS` 与 `VENDOR_DEFINES` 透传厂商头搜索路径与预定义宏，dtc-lite 会自动用 cpp 提取该头全部 `#define`。

8. **ISR 内调用 lock/unlock 失败** → OSAL 强约束：所有 `lock/unlock/create/destroy` 在 ISR 中直接拒绝（返回 `-1`）。需要同步请用 `osal_sem_post_from_isr` 或下半部。

---

## 10. 附录：相关文档

- 设备树详细规范：[devicetree.md](devicetree.md)
- Linux 设备模型对照学习：[linux-vs-mini_tree-device-model.md](linux-vs-mini_tree-device-model.md)
- 构建工具说明：[../../tools/README.md](../../tools/README.md)
- 项目总览与集成方式：[../../README.md](../../README.md)
