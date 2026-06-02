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

> **完整使用指南请移步 [USAGE.md](USAGE.md)** — 快速开始、服务编写、硬件移植、设备树驱动开发、调试监控等。
> **架构总览请移步 [ARCHITECTURE.md](ARCHITECTURE.md)** — 层次划分、核心数据流、启动时序、安全架构。
> **架构演进记录请移步 [NOTICE.md](NOTICE.md)** — 8 轮重构全记录、核心架构决策、安全防御层次、完整文件索引。

---

## 快速开始

```bash
# 一键构建 (推荐)
python tools/p2s.py -p arm_cm3 -t gcc -o freertos
python tools/p2s.py -p arm_cm4f -t keil5 -o rtthread
python tools/p2s.py --menuconfig          # 先配置再构建
python tools/p2s.py -l                     # 列出所有可用组合

# 或直接使用 Makefile
make PLATFORM=arm_cm4f TOOLCHAIN=gcc
make PLATFORM=arm_cm4f TOOLCHAIN=clang
make PLATFORM=arm_cm4f TOOLCHAIN=keil6

# 或 CMake
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
cmake --build build

# Clang 构建 (需要 LLVM 18+)
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_clang_arm_cm4f.cmake
cmake --build build

# 用户工程集成
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

详细说明见 [USAGE.md](USAGE.md) — 配置、集成、服务编写、硬件移植、调试监控全流程指南。

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

### 纯净的南向物理隔离 (Subsystem Ops)

摒弃通过 `#ifdef` 在代码各处散落芯片适配的常见做法。借鉴 Linux 内核 Kbuild 的物理隔离思想：

- **无状态操作表**：核心层定义严格的 `hal_ops` 纯虚接口表，彻底斩断对芯片厂 SDK 的符号依赖。
- **物理平行隔离**：具体芯片驱动（如 ESP32、STM32、GD32）平行放置于 `hal_if/soc/` 目录下。
- **构建期裁剪**：通过 Kconfig 选择目标平台，CMake/Makefile 根据开关动态打包对应物理目录。核心大件（`core/`、`board/`）实现真正的"一次编译，处处链接"。

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
│   ├── menuconfig.py         # 图形化配置工具
│   └── p2s.py                # 一键构建脚本
├── core/                     # 核心服务
│   ├── include/              # EventBus, BufferPool, 日志, 关键数据
│   └── src/                  # CAS 无锁缓冲池与总线分发
├── board/                    # 板级设备树与驱动
│   ├── include/              # VFS 抽象, Class 定义
│   └── src/                  # DTS 生成容器
├── hal_if/                   # 硬件抽象层接口
│   ├── include/              # hal_gpio, hal_spi, hal_i2c, hal_pwm 等
│   └── soc/                  # 南向芯片适配 (按芯片平行隔离)
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

### 工具链文件

| 工具链 | ARM CM3 | ARM CM4F | ARM CM7 | RISC-V |
|--------|---------|----------|---------|--------|
| GCC | `toolchain_arm_cm3.cmake` | `toolchain_arm_cm4f.cmake` | `toolchain_arm_cm7.cmake` | `toolchain_riscv.cmake` |
| Clang/LLVM | `toolchain_clang_arm_cm3.cmake` | `toolchain_clang_arm_cm4f.cmake` | `toolchain_clang_arm_cm7.cmake` | `toolchain_clang_riscv.cmake` |
| Keil 6 (ARMCLANG) | `toolchain_keil6_arm_cm3.cmake` | `toolchain_keil6_arm_cm4f.cmake` | `toolchain_keil6_arm_cm7.cmake` | — |
| Keil 5 MDK + ARMCLANG AC6 | `toolchain_keil5_arm_cm3.cmake` | `toolchain_keil5_arm_cm4f.cmake` | `toolchain_keil5_arm_cm7.cmake` | — |

> Keil MDK 5 与 Keil MDK 6 均使用 ARMCLANG (AC6) 编译器，两者共享同一 LLVM/Clang 后端。`keil5` 工具链指向 `C:/Keil_v5/ARM/ARMCLANG/bin/armclang.exe`，配置的是 AC6 而非已废弃的 ARMCC v5。

### 工具链与 IDE 推荐矩阵

现代嵌入式开发推荐采用**构建系统与代码实现分离**的工作流。以下工具链按推荐程度排列：

| 工具链 / IDE | 推荐度 | 说明 |
| :--- | :---: | :--- |
| **Clang / LLVM** | ★★★★★ | 优秀的静态分析与错误诊断能力，清晰的报错信息有助于复杂代码调试与重构。 |
| **GNU Make / Makefile** | ★★★★★ | 简洁、零运行时依赖。配合 Kconfig 实现构建期物理裁剪。 |
| **ARM GCC** | ★★★★★  | 久经检验的开源工业标准工具链，支持 C23/C++23。 |
| **VS Code / Cursor / Trae** | ★★★★★  | 现代编辑器生态，配合 CMake 和 `dtc-lite.py` 可完成一键配置与构建。 |
| **ARMCLANG (STM32CubeCLT)** | ★★★★☆ | 免费获取，基于 LLVM/Clang 后端，与 GCC 生态互补。 |
| **Ninja** | ★★★★☆ | 加速 CMake 增量构建的轻量构建工具。 |
| **MinGW** | ★★★★☆ | Windows 平台本地 POSIX 测试环境。 |
| **Keil MDK (AC6)** | ★★☆☆☆ | 作为工程管理和烧录辅助工具保留。详见下方维护政策。 |
| **Keil MDK (ARMCC v5)** | 不支持 | 编译器过旧，不支持 C23/C++23 及 GNU 扩展，已被项目淘汰。 |

#### Keil MDK 的客观定位与使用规约

本框架定位为现代化微内核架构，全面拥抱 C++23/C23 标准及 Linux-like 的代码解耦构建系统。针对工业界广泛使用的 Keil MDK，以下从客观角度评估其适用场景并明确使用边界。

**Keil 的核心优势（适用于调试与发布阶段）：**
1. **硬件仿真生态**：提供完善的底层寄存器查看、逻辑分析以及 ULINK/J-Link 深度集成能力。
2. **代码密度优化**：MicroLIB 和底层链接器在代码尺寸优化上表现突出。
3. **行业习惯兼容**：满足传统企业"双击即编译"的交付与验证需求。

**Keil 的局限性（不适用于架构设计与开发阶段）：**
1. **语言标准支持不足**：内建编辑器对 C++23/C23 的高级特性（静态生命周期、模板元编程）解析能力有限，不适合作为代码编写和重构的主力环境。
2. **依赖管理黑盒化**：通过 GUI 手动管理源文件的方式，与本框架 Kconfig + CMake 数据驱动裁剪的微内核理念不兼容。
3. **版本控制负担**：编译过程产生大量中间文件（`.crf`、`.d`、`.axf`），需要严格的 `.gitignore` 策略。

**使用规约：**
- **ARMCC v5 不支持**：项目永久拒绝兼容 ARMCC v5.06。请在 Keil 的 `Options → Target` 中切换为 `Use default compiler version 6` (ARMCLANG/AC6)。欢迎有能力将本架构降级兼容 ARMCC v5 的开发者通过社区 PR 弥补这一空缺，作者不会主动完成此工作。
- **影子工程模式**：架构设计、代码编写和重构在 VS Code / Cursor（配合 Clangd）中完成。Keil 仅作为工程管理和烧录/调试的辅助工具，不建议在其中编辑代码或管理文件依赖。

Clang 构建示例 (需 18+，且 `clang`、`llvm-ar` 在 PATH 中)：

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_clang_arm_cm4f.cmake \
  -DFREERTOS_PORT=GCC_ARM_CM4F
ninja -C build
```

Keil 6 (ARMCLANG) 构建示例 (需 Keil MDK v6, `armclang`/`armar` 在 PATH 中)：

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_keil6_arm_cm4f.cmake \
  -DFREERTOS_PORT=GCC_ARM_CM4F
ninja -C build
```

Keil 5 (ARMCLANG) 构建示例 (需 Keil MDK v5，使用 AC6 编译器)：

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_keil5_arm_cm3.cmake \
  -DFREERTOS_PORT=GCC_ARM_CM3
ninja -C build
```

### Makefile 构建

无需 CMake/Python，直接使用 GNU Make（或使用 `python tools/p2s.py` 一键构建）：

```bash
# GCC ARM Cortex-M4F
make PLATFORM=arm_cm4f TOOLCHAIN=gcc

# Clang ARM Cortex-M4F
make PLATFORM=arm_cm4f TOOLCHAIN=clang

# Keil 6 ARMCLANG Cortex-M4F (FreeRTOS 可用)
make PLATFORM=arm_cm4f TOOLCHAIN=keil6

# Keil MDK 5 + ARMCLANG AC6 (Cortex-M3)
make PLATFORM=arm_cm3 TOOLCHAIN=keil5 OSAL_BACKEND=RTTHREAD

# RISC-V
make PLATFORM=riscv TOOLCHAIN=gcc

# 裸机本地测试
make PLATFORM=posix OSAL_BACKEND=NULL

# 指定 FreeRTOS 堆分配器
make PLATFORM=arm_cm4f FREERTOS_HEAP=4
```

输出 `build_make/lib/*.a` 静态库。完整可用参数：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `PLATFORM` | `posix` | `arm_cm3` / `arm_cm4f` / `arm_cm7` / `riscv` / `posix` |
| `TOOLCHAIN` | `gcc` | `gcc` / `clang` / `keil6` / `keil5` (Keil MDK 5 + ARMCLANG AC6) |
| `OSAL_BACKEND` | `FREERTOS` | `FREERTOS` / `RTTHREAD` / `NULL` |
| `FREERTOS_HEAP` | `4` | FreeRTOS 堆分配器编号 1-5 |
| `BUILD_DIR` | `build_make` | 输出目录 |
| `CC` / `CXX` / `AS` / `AR` | — | 覆盖编译器/汇编器/归档器 |

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

CMake 工具链：ARM GCC 13.3.1 / ARM Clang 18+ / RISC-V GCC 15.2.0 / MinGW 8.1.0 / ARMCLANG 6+
Makefile 工具链：ARM GCC / ARM Clang / RISC-V GCC / ARMCLANG (Keil 5/6)

---
## License

本项目基于 [MIT License](LICENSE) 开源。欢迎任何形式的商业与非商业使用、修改及分发。
