## 2. 快速开始

### 2.1 一键构建 (推荐)

```bash
python tools/p2s.py -p arm_cm3 -t gcc -o freertos
python tools/p2s.py -p arm_cm4f -t keil5 -o rtthread
python tools/p2s.py -p arm_cm7 -t keil5 -o null
python tools/p2s.py -l             # 列出可用组合
python tools/p2s.py --menuconfig   # 配置后构建
python tools/p2s.py --clean        # 清理
```

### 2.2 Makefile 构建

```bash
make PLATFORM=arm_cm4f TOOLCHAIN=gcc OSAL_BACKEND=FREERTOS
make PLATFORM=arm_cm4f TOOLCHAIN=clang
make PLATFORM=riscv TOOLCHAIN=gcc OSAL_BACKEND=NULL
make PLATFORM=posix TOOLCHAIN=gcc OSAL_BACKEND=NULL  # 本地测试
```

### 2.3 CMake 构建

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm4f.cmake
cmake --build build
```

### 2.4 作为静态库引入用户工程

```cmake
add_subdirectory(path/to/mini_tree)
target_link_libraries(my_app PRIVATE mini_tree)
```

用户需提供：
- `hal_*` 符号（通过 `soc_port_` 实现）
- FreeRTOS 后端：`FreeRTOSConfig.h`
- RT-Thread 后端：RT-Thread 内核配置

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

```bash
cp config.example.h build/generated/kconfig/config.h
# 编辑 config.h，取消注释所需选项
```

---

## 4. 用户工程集成

### 4.1 完整工程结构

```
my_project/
├── CMakeLists.txt
├── my_board.dts                # 板级设备树
├── main.c
├── soc_port_mychip/            # HAL 实现 (需用户编写)
│   ├── CMakeLists.txt
│   ├── hal_gpio.c
│   ├── hal_uart.c
│   ├── hal_spi.c
│   ├── hal_i2c.c
│   ├── hal_wdt.c
│   ├── hal_flash.c
│   ├── hal_cpu.c
│   └── hal_pwm.c
└── mini_tree/                  # 子模块或子目录
    ├── CMakeLists.txt
    └── ...
```

### 4.2 用户 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app)

# 引入 mini_tree
add_subdirectory(components/mini_tree)

# 注册 soc_port (HAL 实现)
add_subdirectory(components/soc_port_mychip)

# 链接
target_link_libraries(my_app.elf PRIVATE mini_tree soc_port_mychip)
```

### 4.3 硬件初始化

用户需实现 `hal_*` 接口。每个 `hal_if/include/` 中的接口在 `soc_port_` 中实现：

```c
// soc_port_mychip/hal_gpio.c
#include "hal_gpio.h"
#include "chip_sdk.h"  // 芯片 SDK，仅在 .c 内部包含

void hal_gpio_init(hal_gpio_t* obj, uint32_t pin)
{
    // 调用芯片 SDK 的 GPIO 初始化
    gpio_config_t cfg = {
        .pin = pin,
        .mode = GPIO_MODE_OUTPUT,
    };
    chip_gpio_config(&cfg);
}

void hal_gpio_set_level(hal_gpio_t* obj, uint32_t pin, uint32_t level)
{
    chip_gpio_write(pin, level);
}
```

---

## 5. 点火时序

### 5.1 标准两段式点火 (推荐)

```c
#include "system_init.h"     // C 版本
// 或
#include "system_init.hpp"   // C++ 版本

int main(void)
{
    /* ─── Step 1: 用户硬件初始化 ─── */
    platform_init();                // 时钟、GPIO、外设电源
    board_hal_init();               // soc_port HAL 驱动安装

    /* ─── Step 2: Phase 1 (RTOS 启动前) ─── */
    mini_tree_pre_os_init();
    // 或 C++:  MiniTree::System_Pre_OS_Init();
    //
    // 做的事:
    //   - Bootloop 防护检查
    //   - RTC 硬件看门狗初始化
    //   - 设备树数据结构 init
    //   - EventBus init

    /* ─── Step 3: 用户注册自有驱动 ─── */
    my_driver_register_all();

    /* ─── Step 4: Phase 2 (RTOS 启动后) ─── */
    mini_tree_start_tasks();
    // 或 C++:  MiniTree::System_Start_Tasks();
    //
    // 做的事:
    //   - EventBus 启动 (创建分发任务)
    //   - 设备树 Driver Probe (拓扑排序)
    //   - TWDT 初始化
    //   - Flash Scrubber 启动
    //   - Bootloop 计数器清零
    //   - EventBus seal 封表
    //   - [AMP] hal_cpu_secondary_startup (CPU_CORES>1 时启动副核)

    /* ─── Step 5: 用户创建自有业务任务 ─── */
    osal_task_create("app", 4096, 5, my_app_task, NULL);

    /* ─── Step 6: 启动调度器 ─── */
#if CONFIG_OSAL_NULL
    while (1) { mini_tree_system_loop(); }  // 裸机轮询
#else
    vTaskStartScheduler();                  // RTOS 接管
#endif
}
```
