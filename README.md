# mini_tree v2.0

> 平台无关的嵌入式中间件框架 — Linux 风格设备树 + 编译期 probe + 平台 HAL + 虚拟中断

## 项目简介

`mini_tree` 是异构多核项目的中间件主仓库，提供一套与具体 MCU 解耦的板级框架，借鉴 Linux 设备模型与设备树思想，把"硬件描述、驱动绑定、设备生命周期、操作系统抽象"四件事在编译期与运行期清晰分层。

仓库自身**不包含任何具体平台的 HAL 实现**：所有 `hal_*.c` 仅保留接口签名与参数校验（空函数化），由各平台工程在自己的目录下提供 `hal_gpio_stm32.c` / `hal_uart_stm32.c` 等具体实现并链接。各平台的 DTS（`board/dts/`）与 DTSI（`board/dtsi/`）同样不在本仓库中，由平台工程自带，本仓库仅保留 `board/dt-bindings/` 通用宏常量。

### 支持的目标平台

| 平台 | HAL 后端 | OSAL 后端 | 备注 |
|------|----------|-----------|------|
| STM32F407ZGT6 | `hal_*_stm32.c` | FreeRTOS / RT-Thread / NULL | 当前主用平台 |
| CH32 | `hal_gpio_ch32.c` 等 | 同上 | Kconfig 已支持 |
| ESP32 | `hal_gpio_esp32.c` 等 | FreeRTOS（ESP-IDF） | Kconfig 已支持 |

## 核心设计理念

### 1. DTS 硬件描述

板级硬件以 Linux 风格的 DeviceTree Source（DTS / DTSI）描述，由 `tools/dtc-lite.py` 在编译期解析为 C 代码：

- 节点 (`/soc/spi@1 { ... }`)、`compatible` 属性、`reg` / `interrupts` / `status` 全部沿用 Linux 语义
- `&label` 引用、`/ { }` 无序合并、`/delete-node/` 等覆盖式语法支持
- 厂商 dtsi 中的 `#include <xxx_ll_*.h>` 通过系统 `cpp` 预处理，**DTSI 厂商宏值零翻译直投**给底层 LL 库

### 2. 编译期 probe

`DRIVER_REGISTER(name, compat, probe_fn, remove_fn)` 宏在驱动 `.c` 文件中展开为 `board_driver_probe_<name>` / `board_driver_remove_<name>` 函数，由 `dtc-lite.py` 在编译期扫描所有驱动源码目录并生成静态 `s_probe_table[]`。运行时按 id 索引调用，无 `strcmp` 匹配开销。

### 3. 平台 HAL

HAL 层头文件平台无关，`.c` 文件空函数化（仅参数校验与状态机骨架），由各平台工程提供具体实现。`bus`、`vfs` 层**不直接 include 厂商头**，仅通过 HAL 抽象接口访问硬件，从而保证中间件可以在 STM32 / CH32 / ESP32 之间无修改迁移。

### 4. 虚拟中断框架

`interrupt/interrupt.{c,h}` 提供虚拟中断号（VIRQ）+ 上半部 / 下半部分离机制：

- 虚拟中断按 block 划分（system / tim / gpio / adc / uart / spi / i2c / user），每块 8 个 slot
- 上半部回调在 ISR 内执行，返回值决定是否自动 submit 下半部
- 下半部通过 SPSC 无锁 FIFO 队列在主循环（裸机）或专用任务（RTOS）中执行
- 平台无关：硬件中断使能 `interrupt_hw_enable` 由各平台 HAL 实现

## 架构分层图

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

## 目录结构

```
mini_tree/
├── algorithm/buffer/          # SPSC 无锁环形 FIFO + 双缓冲
├── board/                     # 板级框架核心
│   ├── docs/                  # 文档 (Code_Wiki / devicetree / linux-vs-mini_tree)
│   ├── dt-bindings/           # DTS 宏常量 (spi, uart, tim)
│   ├── include/               # device.h, driver.h, bus.h, VFS.h, dev_lifecycle.h
│   └── src/                   # board_device.c, board_driver.c, bus.c 等
├── bus/                       # 总线抽象
│   ├── spi/                   # spi_bus.c/h
│   └── uart/                  # uart_bus.c/h
├── cmake/                     # CMake 模块 (etl.cmake, rust.cmake, disasm.cmake)
├── core/                      # 核心服务
│   ├── include/               # event_bus.h, buffer_pool.h, production_log.h 等
│   └── src/                   # event_bus.c, buffer_pool.c 等
├── drivers/flash/             # W25Q64 SPI Flash 驱动 (板级驱动示例)
├── hal/                       # 硬件抽象层 (头平台无关, .c 空函数)
│   ├── adc/                   # hal_adc.c (空函数)
│   ├── amp/                   # hal_amp.c (AMP 多核)
│   ├── dac/                   # hal_dac.c (空函数)
│   ├── gpio/                  # hal_gpio.h, hal_gpio_stm32.c (空函数)
│   ├── i2c/                   # hal_i2c.c
│   ├── spi/                   # hal_spi.h, hal_spi_stm32.c (空函数)
│   ├── storage/               # hal_flash.h
│   ├── system/                # hal_stm32f407.c (空函数), hal_wdt.h, hal_rtc.h
│   ├── tim/                   # hal_tim.c (空函数)
│   └── uart/                  # hal_uart.h, hal_uart_stm32.c (空函数)
├── interrupt/                  # 虚拟中断框架 (interrupt.c/h)
├── osal/                      # OS 抽象层
│   ├── include/               # osal.h
│   └── src/                   # osal_null.c, osal_freertos.c, osal_rtthread.c
├── system_c/                  # C 版系统运行时 (init, wdt, scrubber, task_manager)
├── system_cpp/                # C++ 版系统运行时 (+ system_cmd, system_runtime)
├── time_slice/                # 时间片调度 (xtask)
├── tools/                     # 构建工具
│   ├── dtc_lite/              # Lark 文法 DTS 编译器 (含 platform.py)
│   ├── dtc-lite.py            # CLI 入口
│   ├── genconfig.py           # Kconfig → config.h
│   ├── menuconfig.py          # 菜单配置器
│   └── post_build_crc.py      # 链接后 CRC 计算
├── vfs/                       # VFS 桥接 (节点 → file_operations)
│   ├── adc/, can/, dac/       # 各外设 VFS
│   ├── gpio/                  # vfs-gpio.c
│   ├── i2c/, i2s/             # I2C/I2S VFS
│   ├── rtc/, tim/, usb/       # RTC/定时器/USB VFS
│   ├── spi/                   # vfs-spi.c
│   └── uart/                  # vfs-uart.c
├── CMakeLists.txt             # 主构建文件
├── Kconfig                    # 配置选项
├── .config                    # 当前配置
└── .clangd                    # clangd 配置 (相对路径)
```

## 工具链建议

中间件的编译期流水线（DTS 编译、Kconfig 生成、链接后 CRC）依赖 CMake + Python 工具链驱动，**推荐使用以下任意 CMake 原生支持的工具链**：

- GCC ARM None-EABI
- armclang（Keil MDK 命令行版 `armclang`，非 uVision IDE）
- IAR EWARM with CMake
- 任意支持 `cmake --build` 的命令行工具链

搭配的 IDE / 编辑器推荐：VSCode + clangd（仓库自带 `.clangd` 配置）、CLion、Eclipse CDT。

### 关于 Keil MDK（uVision IDE）

Keil MDK 的闭源 uVision IDE 与本组件的编译期 DTS / Kconfig / post-build 流水线集成成本较高：设备树编译器、Kconfig 菜单与 CRC 后处理脚本都需要外部 Python 工具驱动，而 uVision 的工程文件格式（`.uvprojx`）不直接表达这类自定义构建步骤，往往需要额外的脚本胶水层维护，长期演进负担较重。

因此 **uVision IDE 不作为推荐路径**；若团队仍希望沿用 MDK 工具链，建议改用 `armclang` 命令行 + CMake 的方式接入，保留 MDK 编译器的同时获得完整的构建期脚本支持。

## 集成方式

平台工程通过 CMake `add_subdirectory(mini_tree)` 引入，并通过以下变量向中间件注入平台特定的库、宏、头搜索路径与 DTS 入口。

### CMake 变量

| 变量 | 类型 | 作用 |
|------|------|------|
| `PLATFORM_VENDOR_LIB` | STRING | 厂商 SDK 链接库（如 STM32 HAL/LL 库、ESP-IDF 组件），由中间件 `target_link_libraries` 透传 |
| `PLATFORM_VENDOR_DEFINE` | STRING | 厂商设备选择宏（如 `STM32F407xx`、`USE_FULL_LL_DRIVER`），由中间件 `target_compile_definitions` 透传 |
| `VENDOR_INC_DIRS` | PATH | 厂商 HAL 头搜索路径，传给 `dtc-lite.py -I`，让 dtsi 中 `#include <xxx_ll_*.h>` 能被 cpp 解析 |
| `VENDOR_DEFINES` | STRING | 厂商预定义宏，传给 `dtc-lite.py -D`，如 `STM32F407xx` |
| `BOARD_DTS` | FILEPATH | 板级 DTS 入口，默认 `board/dts/board.dts`，各平台覆盖为自己的 `.dts` |
| `BOARD_DTSI_DIR` | PATH | 板级 dtsi 目录，默认 `board/dtsi`，各平台覆盖为自己的 dtsi 目录 |
| `BOARD_DT_BINDINGS_DIR` | PATH | dt-bindings 目录，默认 `board/dt-bindings`（中间件自带通用宏常量） |

### 集成示例

```cmake
# 平台工程 CMakeLists.txt
set(PLATFORM_VENDOR_LIB   "stm32f4xx_ll"   CACHE STRING "" FORCE)
set(PLATFORM_VENDOR_DEFINE "STM32F407xx,USE_FULL_LL_DRIVER" CACHE STRING "" FORCE)
set(VENDOR_INC_DIRS       "/path/to/STM32F4xx/Include" CACHE PATH "" FORCE)
set(VENDOR_DEFINES        "STM32F407xx,USE_FULL_LL_DRIVER" CACHE STRING "" FORCE)
set(BOARD_DTS             "${CMAKE_CURRENT_SOURCE_DIR}/board/dts/stm32f407zgt6.dts"
                           CACHE FILEPATH "" FORCE)
set(BOARD_DTSI_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/board/dtsi" CACHE PATH "" FORCE)

add_subdirectory(mini_tree)
target_link_libraries(my_app PRIVATE mini_tree)
```

### 平台工程需要提供的文件

1. `board/dts/*.dts` — 板级实例化（覆写 `&label { status = "okay"; ... }`）
2. `board/dtsi/*.dtsi` — SoC 根节点 + IP 模板（含 `compatible`、引脚模板）
3. `hal/*/*_<platform>.c` — 各 HAL 接口的具体实现（覆盖中间件空函数）
4. 应用层 `main.c` / `main.cpp` — 调用 `mini_tree_pre_os_init / start_tasks / system_init_complete`

## 构建配置

### Kconfig 关键配置项

`Kconfig` 与 `.config` 控制中间件行为，由 `tools/genconfig.py` 在构建期生成 `generated/kconfig/mini_tree/config.h`。常用配置项：

| 配置项 | 默认值 | 作用 |
|--------|--------|------|
| `PLATFORM_ARM_CM4F` / `CM7` / `CM3` / `RISCV` | `CM4F` | 目标处理器架构选择 |
| `CPU_CORES` | `1` | CPU 核心数（1=单核, 2=AMP 双核） |
| `AMP_MODE` | `y`（多核时） | AMP 模式（osal_null 用原子 CAS 实现锁） |
| `OSAL_NULL` / `FREERTOS` / `RTTHREAD` | `NULL` | OSAL 后端 |
| `OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC` | `IRQ_DISABLE` | 自旋锁实现 |
| `SYS_LOG_USE_PRINTF` / `OSAL` | `PRINTF` | 系统日志后端 |
| `SYSTEM_C` / `SYSTEM_CPP` | `CPP` | 系统运行时实现语言 |
| `HAL_GPIO_STM32` / `CH32` / `ESP32` | `STM32` | HAL GPIO 后端 |
| `PRODUCTION_LOG` | `n` | 生产级黑匣子日志 |
| `SAFETY_SHUTDOWN` | `n` | 硬件级安全停机 |
| `ENABLE_WDT` | `y` | 看门狗 (IWDG + TWDT) |
| `ENABLE_FLASH_SCRUBBER` | `y` | Flash bit-rot 后台扫描 |
| `EVENT_BUS_QUEUE_LEN` | `64` | 事件总线队列深度 |
| `EVENT_BUS_MAX_SUBSCRIBERS` | `24` | 事件总线最大订阅者 |
| `OSAL_MUTEX_POOL_SIZE` | `24` | OSAL 静态互斥锁池大小 |
| `BOARD_STACK_MONITOR_MAX_TASKS` | `8` | 栈监测最大任务数 |
| `COMPILER_GNU_EXTENSIONS` | `y` | GNU 扩展（container_of, likely/unlikely） |
| `BUILD_DISASM` | `y` | 生成 .lst 反汇编 |

### 构建流程

```
1. CMake configure
   ├─ genconfig.py 解析 Kconfig + .config → generated/kconfig/mini_tree/config.h
   ├─ dtc-lite.py 解析 BOARD_DTS → generated/board/mini_tree/
   │   ├─ board_nodes.h           (DEV_ID_* 枚举)
   │   ├─ board_devtable.h/.c     (s_devtable[], s_probe_table[], cascade[])
   │   ├─ board_handles.h
   │   ├─ board_probe.c           (board_driver_probe_all 实现)
   │   └─ dt_config_gen.h        (DTC_GEN_COUNT_* 宏)
   └─ system_scrubber_crc_stub.h 占位 → 链接后 post_build_crc.py 覆盖

2. CMake build
   ├─ 编译 mini_tree 静态库 (含 OSAL / HAL / board / VFS / bus / drivers / core / system / algorithm / interrupt)
   └─ 链接平台 HAL 实现库 (PLATFORM_VENDOR_LIB)

3. Post-build (可选)
   ├─ post_build_crc.py 计算 CRC 覆盖 scrubber stub
   └─ disasm.cmake 生成 .lst 反汇编 (CONFIG_BUILD_DISASM=y)
```

### 配置工具

```bash
# 文本菜单配置器（依赖 kconfiglib）
python tools/menuconfig.py

# 直接生成 config.h
python tools/genconfig.py Kconfig generated/kconfig/mini_tree --config .config
```

## 启动时序

中间件采用 IEC 61508 §7.4.3 二段式点火：

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

## 进一步阅读

- [Code_Wiki.md](board/docs/Code_Wiki.md) — 框架代码 Wiki（分层架构、启动时序、关键数据结构、状态机、中断框架）
- [devicetree.md](board/docs/devicetree.md) — dtc-lite 设备树规范、语法、compatible 属性契约
- [linux-vs-mini_tree-device-model.md](board/docs/linux-vs-mini_tree-device-model.md) — Linux 设备模型 vs mini_tree 对照
- [tools/README.md](tools/README.md) — 构建工具（dtc-lite, genconfig, menuconfig, post_build_crc）说明
