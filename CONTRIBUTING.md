# 贡献指南

## 环境搭建

### 工具链

| 目标 | 工具链 | 安装指引 |
|------|--------|----------|
| ARM Cortex-M3/4/7 | ARM GCC 13.3.1 | STM32CubeCLT 或 xPack ARM GCC |
| RISC-V RV32 | RISC-V GCC 15.2.0 | xPack RISC-V GCC |
| Host 编译验证 | MinGW 8.1.0 (Windows) / GCC (Linux) | 系统包管理器 |

### 构建验证

```bash
# ARM Cortex-M3 + FreeRTOS + system_cpp
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake -DCONFIG_OSAL_FREERTOS=y -DCONFIG_SYSTEM_CPP=y
cmake --build build

# Host 本机 + OSAL_NULL + system_cpp (无需硬件)
cmake -B build_host -DPLATFORM_POSIX=ON -DCONFIG_OSAL_NULL=y -DCONFIG_SYSTEM_CPP=y
cmake --build build_host
```

### 代码检查

```bash
# Debug + Release 双模式验证
cmake -B build_debug   -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
cmake --build build_debug

cmake -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
cmake --build build_release
```

---

mini_tree 仍处于早期阶段，以下列出几个已知有待完善的方向。如果你有兴趣参与，欢迎提交 Issue 或 PR 讨论。

---

## 1. 外设适配层移植 (hal_if 的南向隔离)

`hal_if/` 定义了硬件抽象接口。具体的芯片适配代码必须遵循**南向物理隔离**原则：

- **代码位置**：具体芯片实现放入 `hal_if/soc/<chip_name>/` 目录，通过 CMake/Makefile Kconfig 开关包裹。核心层（`core/`、`board/`、`osal/`）中不得出现与具体芯片相关的条件编译。
- **符号闭包**：芯片厂家的专用头文件（如 ST 的 `stm32f4xx_hal.h`、ESP-IDF 的 `driver/gpio.h`）只允许包含在对应的 `.c` 实现文件内部，不得泄露到 `hal_if/include/` 的公共头文件中。
- **巨型 SDK 的管理**：对于 NXP MCUXpresso、ESP-IDF 等体积庞大的厂商 SDK，建议建立独立的 Git 仓库，通过 **Git Submodule** 在 `hal_if/soc/` 下按需挂载，避免膨胀核心仓库。

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

---


---

## PR 提交规约

### 构建系统与工具链的 PR 规约

1. **不为 IDE 兼容性牺牲架构**：主分支不接受为兼容 ARMCC v5 而修改核心代码逻辑的 PR（如去掉 `const` 导致安全降级、移除 C++23 特性改用宏替代）。欢迎有志者 fork 仓库自行降级适配，作者不会阻拦但也不会合入主干。
2. **禁止提交 IDE 工程残渣**：提交前确认 `.gitignore` 生效。任何包含 `.crf`、`.d`、`.dep`、`Listings/`、`Objects/` 等 IDE 生成物的提交将被驳回。
3. **影子工程修改需审核**：若优化了附带的 `.uvprojx` 影子工程（如调整 MicroLIB 选项或底层链接脚本 `.sct`），欢迎提交。但需确保修改不破坏 CMake/Makefile 的核心依赖关系。

### PR 提交前检查

- 确认 Debug 和 Release 模式下 `-Wall -Wextra -Werror` 零警告通过
- 不引入 C++ 异常（`try/catch`）和 RTTI（`typeid`），保持 `-fno-exceptions -fno-rtti` 编译
- 不向 `core/`、`board/`、`osal/` 引入具体芯片 SDK 符号

---

## 联系方式

- 微信: a1474295026
- 电话: 15302303271
