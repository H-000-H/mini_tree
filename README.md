# mini_tree: Pure Generic & Architecture-Isolated Embedded Middleware

![Version](https://img.shields.io/badge/version-v1.0.0-blue.svg)
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

> **完整使用指南请移步 [USAGE.md](USAGE.md)** — 快速开始、服务编写、硬件移植、设备树驱动开发、调试监控等。
> **架构总览请移步 [ARCHITECTURE.md](ARCHITECTURE.md)** — 层次划分、核心数据流、启动时序、安全架构。
> **架构演进记录请移步 [NOTICE.md](NOTICE.md)** — 8 轮重构全记录、核心架构决策、安全防御层次、完整文件索引。

---

## 快速开始

```bash
# 1. 配置 (选择目标平台和 OSAL 后端)
python tools/menuconfig.py

# 2. 构建
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
cmake --build build

# 3. 用户工程集成
# CMakeLists.txt 中添加:
#   add_subdirectory(path/to/mini_tree)
#   target_link_libraries(my_app PRIVATE mini_tree)
```

```c
// 4. 你的 main.c:
#include "system_init.h"

int main(void) {
    platform_init();               // 时钟 / HAL
    mini_tree_pre_os_init();       // Phase 1: 看门狗 + EventBus
    platform_register_drivers();   // 注册驱动
    mini_tree_start_tasks();       // Phase 2: Probe + 巡检
    vTaskStartScheduler();         // RTOS 接管
}
```

详细说明见 [USAGE.md](USAGE.md) -- 配置、集成、服务编写、硬件移植、调试监控全流程指南。

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

## 核心设计

### OSAL 三栖抽象层

OSAL 统一封装了 FreeRTOS / RT-Thread / NULL (裸机) 三种后端，对外暴露完全一致的 C 接口。上层无感知后端切换：

- **FreeRTOS** — 完整多任务调度，支持 SMP 多核
- **RT-Thread** — 通过 OSAL 代理接管线程与 IPC，FinSH 终端作为选配外挂
- **NULL (裸机)** — 无 RTOS，退化为 1ms SysTick 状态机，使用原子操作 + 位掩码无锁环形队列平替 RTOS IPC。当上层调用 `osal_queue_receive` 传入 `OSAL_WAIT_FOREVER` 时，裸机下退化为轻量自旋等待。对应用层和 EventBus 透明。

由 Kconfig `OSAL_BACKEND` 选择，编译期确定。

### 双系统编译期隔离

通过 Kconfig `SYSTEM_BACKEND` 在编译期选择 system 实现：

| 后端 | 语言 | 适用场景 |
|------|------|---------|
| `SYSTEM_CPP` (默认) | C++23 | 现代特性、Meyers Singleton、静态生命周期防线 |
| `SYSTEM_C` | C23 | 纯 C 交付环境 |

二者输出同名 `system` 库，用户工程无需修改链接配置。

### 轻量级设备树 (dtc-lite)

抛弃传统硬编码硬件注册，采用类似 Linux 的设备树描述硬件拓扑：

- **拓扑排序依赖解析** — 使用 Kahn 算法解析 `depends-on` 属性，生成确定性 Probe 顺序
- **零开销静态链接** — 编译期 DTS → `.rodata` 结构数组，构造函数强制链接匹配驱动
- **级联初始化** — BFS 算法生成设备依赖图，支持延迟加载和联动卸载

### 安全回路

7 层纵深防御体系，详见 [ARCHITECTURE.md §6](ARCHITECTURE.md#6-安全架构-7-层防御)：

- **L1 Bootloop 防护** — 连续崩溃 ≥ 5 次永久锁 Flash
- **L2 RTC 硬件看门狗** — 独立 32kHz 时钟物理电源复位
- **L3 Task WDT** — 3 秒未喂狗触发 Core Dump + 复位
- **L4 栈水位监控** — 剩余 < 512 字节中断闭锁
- **L5 Flash Scrubber** — CRC 逐页巡检检测 Bit-Rot
- **L6 Safe State** — OSAL_PANIC 关中断死循环
- **L7 反汇编审查** — 构建期指令级验证

### 无锁 BufferPool

基于位图和 `__builtin_ctz` 的 O(1) 分配器，支持 ISR 安全操作，零碎片。用于 EventBus 零拷贝消息传递。

---

## 目录结构

```
mini_tree/
├── Kconfig                   # 全局 menuconfig 配置树
├── CMakeLists.txt            # 顶层构建脚本
├── tools/
│   ├── genconfig.py          # Kconfig → config.h 转化
│   ├── dtc-lite.py           # 编译期微型设备树编译器
│   └── menuconfig.py         # 图形化配置工具
├── core/                     # 核心服务
│   ├── include/              # EventBus, BufferPool, 日志, 关键数据
│   └── src/                  # CAS 无锁缓冲池与总线分发
├── board/                    # 板级设备树与驱动
│   ├── include/              # VFS 抽象, Class 定义
│   └── src/                  # DTS 生成容器
├── hal_if/                   # 硬件抽象层接口
│   └── include/              # hal_gpio, hal_spi, hal_i2c, hal_pwm 等
├── osal/                     # 操作系统抽象层
│   ├── include/              # osal.h 统一接口
│   └── src/                  # 三后端: freertos, rtthread, null
├── algorithm/                # 通用算法（环形 FIFO 等）
├── display_if/               # 显示抽象接口
├── lib/                      # 第三方库内核（FreeRTOS, RT-Thread）
├── system_cpp/               # C++ 两段式安全点火与监控
├── system_c/                 # 纯 C 过程式点火平替
└── examples/porting_template/# 移植参考模板
```

---

## 构建与配置

### 配置

使用 Kconfig 图形化配置：

```bash
python tools/menuconfig.py
```

关键配置项：

| 菜单 | 选项 | 说明 |
|------|------|------|
| Platform | `PLATFORM_ARM_CM3/CM4F/CM7/RISCV/POSIX` | 目标 MCU |
| RTOS Backend | `OSAL_FREERTOS/RTTHREAD/NULL` | OSAL 后端 |
| System Backend | `SYSTEM_CPP/SYSTEM_C` | system 实现语言 |
| System Log | `SYS_LOG_USE_OSAL/ESP/PRINTF` | 日志后端 |
| Build Options | `BUILD_DISASM` | 生成反汇编 .lst |

配置产出于 `.config`，编译期由 `genconfig.py` 转化为 `build/generated/kconfig/config.h`。

### 用户工程集成

```cmake
add_subdirectory(path/to/mini_tree)
target_link_libraries(my_app PRIVATE mini_tree)
```

### 点火时序

**C++ (SYSTEM_CPP=y)：**

```cpp
#include "system_init.hpp"

int main(void) {
    platform_init();                // 芯片级硬件初始化
    platform_register_drivers();    // 驱动向 VFS 树绑定

    MiniTree::System_Pre_OS_Init(); // Phase 1: 无锁内存、看门狗、EventBus 铺设
    MiniTree::System_Start_Tasks(); // Phase 2: DTS Probe、Scrubber、监控任务

#ifdef CONFIG_OSAL_NULL
    while (1) { MiniTree::System_Loop(); }
#else
    vTaskStartScheduler();
#endif
}
```

**C (SYSTEM_C=y)：**

```c
#include "system_init.h"

int main(void) {
    platform_init();
    platform_register_drivers();

    mini_tree_pre_os_init();        // Phase 1
    mini_tree_start_tasks();        // Phase 2

#ifdef CONFIG_OSAL_NULL
    while (1) { mini_tree_system_loop(); }
#else
    vTaskStartScheduler();
#endif
}
```

### 反汇编审查

开启 `CONFIG_BUILD_DISASM=y` 后，构建期自动调用 `CMAKE_OBJDUMP` 在 `build/disasm/*.lst` 输出各模块机器码。可用于验证无锁操作的原子指令（`lock cmpxchg`、`__builtin_ctz` 的 `bsf` 等）及死代码消除。

---

## 跨平台验证

已通过 10 种组合验证，详见 [ARCHITECTURE.md §7](ARCHITECTURE.md#7-跨平台验证矩阵)：

工具链：ARM GCC 13.3.1 (STM32CubeCLT) / RISC-V GCC 15.2.0 (xPack) / MinGW 8.1.0 (host)

---
## License

本项目基于 [MIT License](LICENSE) 开源。欢迎任何形式的商业与非商业使用、修改及分发。
