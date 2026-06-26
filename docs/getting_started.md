## 2. 快速开始

mini_tree 是设备树框架的源代码仓库（`mini_tree/`），不直接构建固件。请通过具体芯片项目（如异构多核项目 Heterogeneous-Multicore 下的 `STM32F407ZGT6/`、`CH32V307/`、`ESP32-S3/`）集成使用。

### 2.1 在芯片项目中构建

各节点项目已通过 `CMakePresets.json` 绑定正确的工具链文件，三端（Docker / Linux / Windows）路径由 `find_program` 自动探测：

```bash
# ARM (STM32F407)
cd STM32F407ZGT6 && cmake --preset Debug && cmake --build build/Debug

# RISC-V (CH32V307)
cd CH32V307 && cmake --preset Debug && cmake --build build/Debug

# Xtensa (ESP32-S3) — 需先 source ESP-IDF 环境
cd ESP32-S3 && idf.py build
```

### 2.2 作为子模块引入新工程

```cmake
# 项目根 CMakeLists.txt
add_subdirectory(mini_tree)

# 设置板级设备树入口
set(BOARD_DTS ${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/board/dts/my_board.dts)

# 提供具体芯片的 HAL 实现（覆盖 hal/ 下的弱符号）
target_sources(mini_tree PRIVATE
    hal/spi/spi_hal_mychip.c
    hal/uart/uart_hal_mychip.c
    # ...
)
target_link_libraries(my_app.elf PRIVATE mini_tree)
```

用户需提供：
- 具体芯片的 HAL 实现（覆盖 `hal/` 下各子目录的弱符号，文件名通过 `HAL_SRCS` 变量传入）
- OSAL 后端配置（`OSAL_FREERTOS` / `OSAL_RTTHREAD` / `OSAL_NULL`）：FreeRTOS 需提供 `FreeRTOSConfig.h`，RT-Thread 需提供 RT-Thread 内核配置
- 板级 DTS 文件（通过 `BOARD_DTS` 变量传入）

#### 厂商 HAL 集成

mini_tree 框架与厂商 HAL 解耦：厂商工具生成外设初始化代码（如 ST CubeMX `MX_SPI1_Init()`、CH32 `SPI_Init()`、ESP-IDF `spi_bus_initialize()`）在板级 `pre_execution` 钩子中调用；mini_tree 的 `hal/` 层只负责运行时配置（mode/分频/传输），不重复开 RCC 或配置引脚复用。具体集成方式由用户工程决定，框架不绑定特定厂商 SDK。

---

## 3. 配置系统

### 3.1 menuconfig 图形化配置

```bash
python tools/kconfig_gui.py
```

### 3.2 核心配置项

| 菜单 | 选项 | 说明 |
|------|------|------|
| **Platform** → Target MCU | `PLATFORM_ARM_CM3` | ARM Cortex-M3 |
| | `PLATFORM_ARM_CM4F` | ARM Cortex-M4F (FPU) |
| | `PLATFORM_ARM_CM7` | ARM Cortex-M7 |
| | `PLATFORM_RISCV` | RISC-V 32-bit |
| | `PLATFORM_POSIX` | POSIX (本地编译验证) |
| **Multi-core** | `CPU_CORES=1` | 单核模式（默认） |
| | `CPU_CORES=2` | 双核 AMP（Core 0 RTOS, Core 1 裸机） |
| **RTOS Backend** | `OSAL_FREERTOS` | FreeRTOS 后端 |
| | `OSAL_RTTHREAD` | RT-Thread 后端 |
| | `OSAL_NULL` | 裸机 (前后台系统) |
| **System Backend** | `SYSTEM_CPP` | C++ system (默认) |
| | `SYSTEM_C` | 纯 C system |
| **Board Features** | `SAFETY_SHUTDOWN` | IEC 61508 安全停机 |
| **Build Options** | `BUILD_DISASM` | 构建期自动生成反汇编 |

### 3.3 配置值来源优先级

1. `.config` 文件 (menuconfig 生成)
2. CMake 变量 `-DFREERTOS_PORT=GCC_ARM_CM3` (跳过 Kconfig)
3. 默认值 (无配置时)

### 3.4 手动配置 (无 menuconfig 环境)

无 menuconfig 环境时，可参考 `Kconfig` 文件中各选项定义，手动创建 `build/generated/kconfig/config.h`，按需 `#define` 所需宏（选项名与取值以 `Kconfig` 为准）：

---

## 4. 用户工程集成

### 4.1 完整工程结构

参考异构多核项目（Heterogeneous-Multicore）的节点布局：

```
my_project/
├── CMakeLists.txt              # 项目根：工具链、HAL_SRCS、BOARD_DTS
├── CMakePresets.json           # Debug/Release 预设
├── mini_tree/                  # 框架子目录（含 board/dts/、board/dtsi/、dt-bindings/）
├── app/                        # 应用层（业务任务、命令 handler）
│   ├── inc/
│   └── src/app_*_task.cpp
├── hal/                        # 具体芯片 HAL 实现（覆盖 hal/ 弱符号）
│   ├── spi/spi_hal_mychip.c
│   ├── uart/uart_hal_mychip.c
│   └── ...
└── drivers/                    # 板级驱动（DRIVER_REGISTER 所在目录）
    └── ...
```

### 4.2 用户 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app)

# 工具链
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

# 引入 mini_tree
add_subdirectory(mini_tree)

# 指定板级 DTS 入口（必须）
set(BOARD_DTS ${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/board/dts/my_board.dts)

# 指定具体芯片的 HAL 实现文件（覆盖 hal/ 弱符号）
set(HAL_SRCS
    hal/spi/spi_hal_mychip.c
    hal/uart/uart_hal_mychip.c
)
target_sources(mini_tree PRIVATE ${HAL_SRCS})

# 应用层
add_subdirectory(app)
target_link_libraries(my_app.elf PRIVATE mini_tree app)
```

### 4.3 HAL 实现约定

具体芯片的 HAL 实现放在项目 `hal/` 目录下，文件名遵循 `hal/<periph>/<periph>_hal_<chip>.c` 约定，通过 `HAL_SRCS` 变量传入 `mini_tree` 目标。框架层只依赖 `hal/<periph>/<periph>_hal.h` 声明的统一 API，业务层和 bus 层无需感知具体芯片：

```c
// hal/spi/spi_hal_mychip.c
#include "hal/spi/spi_hal.h"
#include "chip_sdk.h"  // 芯片 SDK，仅在 .c 内部包含

int spi_hal_host_init(struct spi_hal_host* host, ...)
{
    // 调用芯片 SDK 的 SPI 初始化
    spi_bus_initialize(host->hw_instance, &bus_config, SPI_DMA_CH_AUTO);
    return 0;
}

int spi_hal_transfer_poll(struct spi_hal_host* host,
                          const uint8_t* tx, uint8_t* rx, size_t len)
{
    spi_transaction_t t = { .tx_buffer = tx, .rx_buffer = rx, .length = len * 8 };
    spi_device_polling_transmit(host->dev, &t);
    return 0;
}
```

---

## 5. 点火时序

### 5.1 标准两段式点火

mini_tree 采用两段式点火：`mini_tree_pre_os_init()` (Phase 1, OS 启动前) + `mini_tree_start_tasks()` (Phase 2, OS 启动后)。两者之间用 `board_register_all_drivers()` 衔接（兼容入口，实际 probe 表由编译期 dtc-lite 收录）。

入口函数通过 `__attribute__((used, section(".entry")))` 放入 `.entry` 段，由芯片启动文件跳转。外设初始化用 `pre_execution(priority)` 宏注册钩子，在 `HAL_Init()` 后自动调用 `MX_*_Init()`。

**三种 OSAL 后端的实际入口模板**：

#### 5.1.1 裸机后端 (OSAL_NULL) — 以 STM32F407ZGT6 为例

```c
#include "system_init.h"
#include "driver.h"
#include "compiler_compat.h"

pre_execution(50)
static void board_periph_init(void) {
    MX_SPI1_Init();
    MX_UART4_Init();
}

extern "C" __attribute__((used, section(".entry"))) int stm32f407zgt6_node_main(void)
{
    HAL_Init();
    SystemClock_Config();

    mini_tree_pre_os_init();          // Phase 1
    board_register_all_drivers();
    mini_tree_start_tasks();          // Phase 2
    system_init_complete();           // 释放全局中断

    while (1) { mini_tree_system_loop(); }  // 裸机: 喂狗 + 轮询
}
```

#### 5.1.2 FreeRTOS 后端 — 以 CH32V307 为例

```c
#include "system_init.h"
#include "driver.h"
#include "compiler_compat.h"
#include "freeRTOS.hpp"

pre_execution(50)
static void board_periph_init(void) {
    MX_SPI1_Init();
    MX_USART1_Init();
}

extern "C" __attribute__((used, section(".entry"))) int ch307_node_main(void)
{
    mini_tree_pre_os_init();          // Phase 1
    board_register_all_drivers();
    mini_tree_start_tasks();          // Phase 2
    system_init_complete();           // 释放全局中断
    task_rtos_main();                 // 封装 vTaskStartScheduler()
    return 0;
}
```

`task_rtos_main()` 由用户工程提供，封装 `vTaskStartScheduler()` 并注册 `vApplicationStackOverflowHook` → `enter_safe_state()`。

#### 5.1.3 ESP-IDF 后端 — 以 ESP32-S3 为例

ESP32 入口为 `app_main()`，由 ESP-IDF 调度。业务命令注册在 `board_register_all_drivers()` 之后、`mini_tree_start_tasks()` 之前；业务任务在 `mini_tree_start_tasks()` 之后、`system_init_complete()` 之前创建：

```c
#include "app_rtos.hpp"
#include "system_init.h"
#include "driver.h"

extern "C" void app_main(void) { app_rtos_start(); }

int app_rtos_start(void)
{
    nvs_flash_init();                 // ESP-IDF 专属

    mini_tree_pre_os_init();          // Phase 1
    board_register_all_drivers();
    app_cmd_handlers_register();      // 业务命令注册 (start_tasks 前)
    mini_tree_start_tasks();          // Phase 2

    app_spi_task_start();             // 业务任务创建 (system_init_complete 前)
    app_led_task_start();
    app_flash_task_start();

    system_init_complete();           // 释放全局中断, app_main 返回后调度器接管
    return 0;
}
```

### 5.2 各阶段职责

| 阶段 | 函数 | 时机 | 做的事 |
|------|------|------|--------|
| Phase 1 | `mini_tree_pre_os_init()` | OS 启动前 | IRQ_DISABLE → Bootloop 防护 → RTC WDT → device_tree_init → event_bus_init → SIOF 标志就绪 |
| 衔接 | `board_register_all_drivers()` | Phase 1 后 | 兼容入口（probe 表已由 dtc-lite 编译期收录）|
| Phase 2 | `mini_tree_start_tasks()` | OS 启动后 | event_bus_start → board_driver_probe_all → TWDT → Flash Scrubber → clear_bootloop → EVENT_SYS_READY → event_bus_seal → [AMP] hal_cpu_secondary_startup |
| 完成 | `system_init_complete()` | Phase 2 后 | IRQ_ENABLE，释放全局中断 |
| 调度 | `task_rtos_main()` / `app_rtos_start()` / `while(mini_tree_system_loop())` | 完成后 | RTOS: vTaskStartScheduler / 裸机: 喂狗轮询 |

> **业务任务创建时机**：FreeRTOS 后端在 `system_init_complete()` 之后创建（如 CH32V307 的 `app_*_task`）；ESP-IDF 后端在 `mini_tree_start_tasks()` 之后、`system_init_complete()` 之前创建（如 ESP32-S3 的 `app_*_task_start()`）。两种顺序均可，取决于业务是否需要在调度器接管前完成任务创建。

### 5.3 业务任务模板

业务任务通过 `task_manager_create_task()` 创建，通过 `device_find_by_label()` 获取设备句柄，通过 `device_open/read/write/ioctl` 操作设备：

```c
#include "device.h"
#include "VFS.h"
#include "task_manager.h"
#include "system_wdt.h"
#include "osal.h"

static void my_task_entry(void* arg)
{
    struct device* dev = (struct device*)arg;
    for (;;) {
        system_wdt_feed();                      // 喂任务看门狗
        uint8_t buf[16];
        int n = device_read(dev, buf, sizeof(buf), 100);
        if (n > 0) {
            device_ioctl(dev, MY_CMD_PROCESS, buf, n, 100);
        }
    }
}

void my_task_start(void)
{
    struct device* dev = device_find_by_label("my_device");
    if (IS_ERR(dev)) return;
    if (device_open(dev, NULL) != VFS_OK) return;
    task_manager_create_task("my", 1024, 10, my_task_entry, dev, 0);
}
```

> 完整实际示例参考异构多核项目（Heterogeneous-Multicore）的 `app/src/app_led_task.cpp`、`app/src/app_spi_task.cpp` 等。
