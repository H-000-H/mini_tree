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

## 1. 外设适配层移植 (hal_if)

`hal_if/` 定义了硬件抽象接口，但具体的芯片适配代码需要为各平台分别实现。目前已有接口定义，缺的是各 MCU 的原生适配：

- **STM32 系列** — 基于 HAL 库或 LL 库实现 `hal_gpio`、`hal_spi`、`hal_i2c` 等
- **ESP32-S3 / C3** — 将 RMT、SPI 等驱动封装到 `hal_if` 接口下
- **RP2040** — PIO 状态机与通用脉冲引擎的抽象对接

**实现约定**：
- 芯片适配代码放在 `examples/porting_template/` 下，不侵入 `core/`、`board/`、`osal/`
- 通过链接符号向 `hal_if` 接口注册，而非直接修改框架代码

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

## PR 提交前检查

- 确认 Debug 和 Release 模式下 `-Wall -Wextra -Werror` 零警告通过
- 不引入 C++ 异常（`try/catch`）和 RTTI（`typeid`），保持 `-fno-exceptions -fno-rtti` 编译
- 不向 `core/`、`board/`、`osal/` 引入具体芯片 SDK 符号
