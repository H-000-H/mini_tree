# 贡献指南

## 开发平台策略

项目现已全面转向 **Linux 环境**（原生 Linux / WSL / Docker）作为主开发与验证平台，以兼顾 Linux 服务端工具链与 MCU 交叉编译的平衡，并彻底解决 Windows 原生环境下多套工具链（ARM GCC / RISC-V GCC / Xtensa GCC / MinGW）的路径冲突与版本碎片化问题。

同时，由于 `mini_tree` 是**通用嵌入式中间件**，核心层与环境无关，仅依赖 CMake + GCC 工具链和标准 C/C++，**Windows 原生编译同样可用**。各平台的定位如下：

| 平台 | 状态 | 定位 |
|------|------|------|
| **Linux 原生 / WSL** | 主开发环境 | 作者日常开发与 CI 验证的基准环境，工具链通过系统包管理器统一安装，路径与版本完全可控 |
| **Docker** | 可选 | 通过 `build.sh -docker` 一键复现 Linux 环境，适合无 Linux/WSL 的贡献者 |
| **Windows 原生** | 可用 | 作为通用中间件，同样支持 Windows 原生编译（通过 `find_program` 三端自动探测工具链），可编译但非主开发路径 |
| **Keil (MDK / ARMCC)** | 已废弃 | **本中间件移除 Keil 支持，且不推荐使用该开发方式** |

### 关于 Keil 的说明

Keil（MDK-ARM / ARMCC / AC5 / AC6）**天生不属于本套系统**，原因如下：

- **构建系统不兼容**：本中间件统一采用 **CMake + Ninja + GCC 工具链** 的现代构建体系，通过 `CMakePresets.json` 与 `cmake/*.cmake` 工具链文件管理。Keil 的 `.uvprojx` 工程模型与此完全异构，无法纳入本构建体系。
- **C/C++ 标准受限**：本框架统一使用 **C17 / C++17** 标准（`-std=c17` / `-std=c++17`，GCC 扩展开启）。Keil ARMCC 5 对 C++17 支持不完整，AC6 虽基于 Clang 但工程配置与 GCC 差异显著，会引入额外的条件编译分支，污染核心层。
- **跨架构能力缺失**：本框架以 ARM Cortex-M 与 RISC-V RV32 为双通用基准，并接入 ESP32 (Xtensa)。Keil 仅支持 ARM 架构，无法覆盖 RISC-V 与 Xtensa 节点，与"通用基准"定位冲突。
- **南向隔离原则**：`hal/` 层强制要求芯片厂家的专用头文件只允许包含在对应的 `.c` 实现文件内部。Keil 工程的预编译头与分散加载机制会破坏这一闭包约束。

> 第三方库（CMSIS / ETL / RT-Thread 等）头文件中保留的 `__ARMCC_VERSION` / `ETL_COMPILER_KEIL` 等条件分支属于上游厂商的编译器兼容代码，不由本仓库维护，亦不代表本中间件支持 Keil 构建。贡献者请勿为本仓库新增任何 Keil 工程文件（`.uvprojx` / `.uvoptx`）或 ARMCC 专属适配代码。

## 环境搭建

### 推荐 Linux 发行版

> 主开发环境为 Linux，但并非所有 Linux 环境都适合嵌入式交叉编译与硬件烧录。以下为作者实际使用后的推荐与避坑：

| 环境 | 推荐度 | 说明 |
|------|--------|------|
| **LMDE** (Linux Mint Debian Edition) | ✅ 推荐 | 轻量、稳定、基于 Debian，资源占用低，虚拟机运行流畅不卡顿。APT 包管理器与 Debian 工具链无缝对接 |
| **Debian** (稳定版) | ✅ 推荐 | 与 LMDE 同源，适合纯命令行或轻量桌面 |
| **其他轻量级虚拟机** (Xubuntu / Lubuntu / Linux Lite) | ✅ 可以 | 资源占用小的发行版均可，虚拟机跑起来不卡即可 |
| **Ubuntu** (标准版) | ❌ 不推荐 | 体积大、GNOME 桌面重，虚拟机里又大又卡，影响开发体验 |
| **WSL** (Windows Subsystem for Linux) | ❌ 不推荐 | 嵌入式烧录需要直接操作 USB 串口、J-Link/ST-Link 等硬件调试器，WSL 对 USB 直通支持不稳定，烧录流程经常挂，折腾成本高 |

> **总结**：嵌入式开发建议直接上虚拟机安装轻量级 Linux 发行版（首选 **LMDE**），USB 直通稳定、工具链一条龙、不会卡。不建议在 WSL 里折腾烧录，也别在虚拟机里跑 Ubuntu——太重了。

### 工具链

| 目标                            | 工具链 (Linux 主环境)                      | Windows 也支持                                       | 安装指引                                                |
| ----------------------------- | --------------------------------------- | -------------------------------------------------- | --------------------------------------------------- |
| ARM Cortex-M3/4/7 (STM32F407) | Linux: ARM GCC 14.2.1 (系统包) / Docker: ARM GCC 14.2.1 | ARM GCC 13.3.1 (STM32CubeCLT 1.20.0)                | ST 官方 STM32CubeCLT 安装包                              |
| RISC-V RV32 (CH32V307)        | Linux: RISC-V GCC (WCH 工具链) / Docker: RISC-V GCC 8.2.0 | RISC-V GCC 15.2.0 (WCH MounRiver Studio GCC15)      | 沁恒 MounRiver Studio 2 自带                            |
| ESP32 (Xtensa LX)             | Xtensa GCC (随 ESP-IDF v5.5.2+)                  | Xtensa GCC (随 ESP-IDF v5.5.2+)                     | ESP-IDF 安装包 Linux/Windows 部署（不走 Docker） |
| Host 编译验证                     | GCC (Linux) / MinGW 8.1.0 (Windows)      | MinGW 8.1.0                                        | 系统包管理器                                              |

> 工具链路径由各节点的 `cmake/*.cmake` 文件通过 `find_program` 三端自动探测（Docker `/opt` → Linux 标准路径 → Windows 标准路径 → PATH），无需手动设置环境变量。

### 构建验证

通过项目根目录的 `build.sh` 统一入口（默认 Docker，`-native` 强制宿主原生，Linux/Windows 均可）：

```bash
# 三节点原生构建（Linux/Windows 均可）
./build.sh stm32 -native    # STM32F407 (ARM Cortex-M4F)
./build.sh ch32   -native    # CH32V307 (RISC-V RV32)
./build.sh esp32  -native    # ESP32-S3 (Xtensa LX7)

# 一键构建所有节点
./build.sh all -native

# 强制 Docker 构建（ch32/stm32 走 multi-arch-compiler:v2.0，esp32 走 espressif/idf:latest）
./build.sh all -docker
```

或直接在节点目录使用 CMake Presets：

```bash
cd STM32F407ZGT6 && cmake --preset Debug && cmake --build build/Debug
cd CH32V307       && cmake --preset Debug && cmake --build build/Debug
cd ESP32-S3       && cmake --preset esp-idf && cmake --build build/esp-idf
```

### 代码检查

```bash
# Debug + Release 双模式验证 (以 CH32V307 为例)
cmake --preset Debug -S CH32V307
cmake --build CH32V307/build/Debug

cmake --preset Release -S CH32V307
cmake --build CH32V307/build/Release
```

***

mini\_tree 仍处于早期阶段，以下列出几个已知有待完善的方向。如果你有兴趣参与，欢迎提交 Issue 或 PR 讨论。

***

## 1. 外设适配层移植 (hal 的南向隔离)

`hal/` 定义了硬件抽象接口与平台实现。具体的芯片适配代码必须遵循**南向物理隔离**原则：

- **代码位置**：具体芯片实现放入 `hal/soc/<chip_name>/` 目录，通过 CMake Kconfig 开关包裹。核心层（`core/`、`board/`、`osal/`）中不得出现与具体芯片相关的条件编译。
- **符号闭包**：芯片厂家的专用头文件（如 ST 的 `stm32f4xx_hal.h`、ESP-IDF 的 `driver/gpio.h`）只允许包含在对应的 `.c` 实现文件内部，不得泄露到 `hal/` 各子目录的公共头文件中。
- **巨型 SDK 的管理**：对于 NXP MCUXpresso、ESP-IDF 等体积庞大的厂商 SDK，建议建立独立的 Git 仓库，通过 **Git Submodule** 在 `hal/soc/` 下按需挂载，避免膨胀核心仓库。

## 2. 多核 SMP 下的 Cache 对齐 —— 一个理论风险点

框架中的 `circle_fifo_buffer` 在单核场景下工作正常。如果用到双核 MCU（ESP32-S3、Cortex-M7 双核等），存在一个理论上的性能风险：

两个核同时操作环形缓冲区的控制结构时，若结构体未对齐到 Cache Line 边界，可能会触发缓存一致性同步开销（Cache Thrashing）。

**这只是理论分析，尚未在实际硬件上验证过**。如果你有条件在双核平台上测试，欢迎：

- 在 Kconfig 中新增 `CONFIG_MINITREE_CACHE_LINE_SIZE` 选项
- 在 `circle_fifo_buffer.c` 的关键结构体上增加 `__attribute__((aligned(N)))`
- 提交测试前后的反汇编对比或性能数据

这个问题涉及 ARM 和 RISC-V 两种架构的缓存模型，不同平台的 Cache Line 大小不同（通常是 32 或 64 字节），需要具体平台实测。

## 3. 设备树的动态探测 —— 一个可选的扩展方向

当前框架通过 `dtc-lite.py` 在编译期完成设备拓扑排序和驱动 Probe。这对硬件固定的场景够用，但可热插拔或模块化的系统可能需要运行时动态枚举。

如果你有兴趣，可以考虑：

- 在 `board_device.c` 中增加一个可选的运行时 Bus Probe 状态机
- 当 I2C/SPI 总线上检测到新设备时，在 VFS 树上动态挂载设备节点
- 与现有编译期 DTS 共存，两者不冲突

**注意**：编译期静态 DTS 和运行时动态枚举各有适用场景，不是替代关系。静态方案零运行时开销、确定性高，适合硬件固定的产品；动态方案灵活但复杂。新增动态探测应作为独立模块，不破坏现有静态路径。

***

***

## PR 提交规约

### 构建系统与工具链的 PR 规约

1. **请勿提交 IDE 工程残渣**：提交前确认 `.gitignore` 生效。包含 `.crf`、`.d`、`.dep`、`Listings/`、`Objects/` 等 IDE 生成物的提交将被驳回。
2. **禁止引入 Keil 工程文件**：本中间件已移除 Keil 支持（详见上方"开发平台策略"）。任何 `.uvprojx` / `.uvoptx` / `.uvproj` / `.uvopt` 工程文件，或针对 ARMCC/AC5/AC6 的专属适配代码（如 `#ifdef __ARMCC_VERSION` 分支）将被驳回。

### PR 提交前检查

- 确认 Debug 和 Release 模式下 `-Wall -Wextra -Werror` 零警告通过
- 不引入 C++ 异常（`try/catch`）和 RTTI（`typeid`），保持 `-fno-exceptions -fno-rtti` 编译
- 不向 `core/`、`board/`、`osal/` 引入具体芯片 SDK 符号

***

## 联系方式

- 微信: wxid\_85v93xugqjvp12
- 电话: 15302303271

