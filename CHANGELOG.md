# Changelog

## [Unreleased]

### 统一中间件架构

mini_tree/ 收敛为纯中间件, 完成 VFS / Bus / HAL 三层解耦:

- **VFS 层** (`vfs/`) — `file_operations` + `dev_lifecycle` + DTS, 注册 `spi-master` / `uart` / `heterogeneous,gpios` 等 compatible
- **Bus 层** (`bus/` + `board/include/bus.h`) — host/client 池 + atomic `ref_count` + `controller_ops`
- **HAL 层** (`hal/**/*.h` 平台中立头) — 平台 `.c` 实现由各项目目录通过 `HAL_SRCS` 变量集成

三层隔离由 `core/include/compiler_compat_poison.h` 的 `#pragma GCC poison` 在编译期强制 (bus 外禁调 hal 符号, vfs 外禁调 bus 符号), 新增 **L10 防御层**.

**硬件直投模式**: 移除 `hal_gpio_ops_t` / `hal_spi_ops_t` 等 vtable/ops 间接层, DTSI 厂商宏值直投, HAL 零翻译透传给 LL 库 / ESP-IDF driver.

**统一 compatible strings**: 去除 `stm32,` / `ch32,` / `esp32,` 平台前缀, 三平台 IP dtsi 中 `compatible` 统一为 `spi-master` / `spi-slave` / `uart` / `uart-client` / `heterogeneous,gpios` / `heterogeneous,spi-master-client` / `heterogeneous,fft-spi-slave` / `*-platform-cap`. dtc-lite `_validate_compatibles()` 在编译期校验驱动匹配.

**统一 HAL 头结构体** (平台中立):

- `hal_spi_pin_cfg  { uintptr_t port; uint16_t pin; uint32_t clk_periph; uint32_t af; }`
- `hal_uart_pin_cfg { uintptr_t port; uint16_t pin; uint32_t clk_periph; uint32_t af; }`
- `hal_gpio_obj_t   { uintptr_t port; uint16_t pin; uint32_t clk_periph; bool is_used; }` (嵌入 VFS priv, HAL 无池管理)

涉及文件: `hal/spi/hal_spi.h`, `hal/uart/hal_uart.h`, `hal/gpio/hal_gpio.h`, `board/include/bus.h`, `vfs/spi/spi_vfs.{c,h}`, `vfs/uart/uart_vfs.{c,h}`, `vfs/gpio/vfs-gpio.{c,h}`, `core/include/compiler_compat_poison.h`.

### hal_pin_t 完全删除

删除 `hal_pin_t` 复合引脚 32-bit port+pin 编码及其所有辅助函数, 改为 `hal_spi_pin_cfg` / `hal_uart_pin_cfg` / `hal_gpio_obj_t` 三组结构体, 各自携带 `port` / `pin` / `clk_periph` / `af` (`is_used`) 字段. 同步删除 `hal_gpio_ops_t`, `hal_spi_ops_t` 等 vtable/ops 表, HAL 接口改为函数原型直投. 涉及 `hal/**/*.h` 头文件全部重写.

### Apache-2.0 license 统一

所有源文件 license 统一为 Apache-2.0, 头部添加 `/* SPDX-License-Identifier: Apache-2.0 */`. 此前混合的 MIT / Apache-2.0 / 无 license 头状态全部收敛. `tools/genconfig.py` 生成头模板同步注入 SPDX 标识.

### ESP32 适配统一 HAL 模式

ESP32 平台适配迁移到统一 HAL 头结构体 (`hal_spi_pin_cfg` / `hal_uart_pin_cfg` / `hal_gpio_obj_t`), 不再为 ESP32 单独维护 ops/vtable 路径. ESP32 DMA 在无硬件支持的场景返回 `-ENOTSUP` stub, 与 `hal_if_dummy.c` 兜底实现一致, 避免 `#ifdef ESP_PLATFORM` 散落各处.

### 三平台 DTSI compatible 统一

STM32 / CH32 / ESP32 三平台 IP dtsi 中 `compatible` 字段去除平台前缀:

| 之前 | 之后 |
|------|------|
| `stm32,spi-master` / `ch32,spi-master` / `esp32,spi-master` | `spi-master` |
| `stm32,uart` / `ch32,uart` / `esp32,uart` | `uart` |
| `stm32,gpios` / ... | `heterogeneous,gpios` |
| `stm32,spi-master-client` / ... | `heterogeneous,spi-master-client` |
| `stm32,spi-platform-cap` / `ch32,spi-platform-cap` / `esp32,spi-platform-cap` | `*-platform-cap` (平台能力声明, dtc-lite 编译期识别) |

业务侧 `device_find_by_label()` 通过节点 `label` 获取设备, 不直接依赖 compatible string, 因此 compatible 调整对业务 API 兼容性无影响.

### Windows 三平台编译验证

在 Windows 原生环境完成 ST/ESP/CH 三节点编译验证，工具链版本区分平台标注：

- **STM32F407ZGT6** — ARM GCC 13.3.1 (STM32CubeCLT 1.20.0)，FLASH 2.80%, RAM 6.71% ✓
- **CH32V307** — RISC-V GCC 15.2.0 (MounRiver Studio GCC15)，FLASH 15.55%, RAM 33.63% ✓
- **ESP32-S3** — Xtensa GCC (ESP-IDF v5.5.2)，bin 304KB, 70% free ✓

文档修正：将原标注的单一工具链版本（Docker: ARM GCC 14.2.1 / RISC-V GCC 8.2.0）改为区分 Docker 与 Windows 原生版本。涉及 `ARCHITECTURE.md` §7 验证矩阵、`NOTICE.md`、`CONTRIBUTING.md`、`README.md`、`docs/faq.md`。

工具链 cmake 修复：`gcc-arm-none-eabi.cmake` 补充 `STM32CubeCLT_1.20.0` 版本化路径；`riscv32-wch-elf-ch32v307.cmake` 补充 `RISC-V Embedded GCC12`/`GCC` 路径作为回退。

### Linux (WSL) 三平台编译验证

在 WSL 纯 Linux 环境完成 ST/ESP/CH 三节点编译验证（主开发环境）：

- **STM32F407ZGT6** — ARM GCC 14.2.1 (系统包)，FLASH 3.12%, RAM 6.70% ✓
- **CH32V307** — RISC-V GCC (WCH 工具链)，FLASH 16.05%, RAM 33.26% ✓
- **ESP32-S3** — Xtensa GCC 16.1.0 (ESP-IDF v6.2)，bin 319KB, 70% free ✓

工具修复：`tools/genconfig.py` 新增 `esp_kconfiglib.core` 作为第三回退导入路径，兼容 ESP-IDF v6.2 的 esp_kconfiglib 分支（`kconfiglib` 包仅 re-export `Kconfig` 类，不含 `BOOL`/`HEX`/`INT`/`STRING` 常量）。

### 开发平台策略与 Keil 支持移除

- **`CONTRIBUTING.md`** — 新增"开发平台策略"章节：明确作者已将主开发环境迁移至 WSL（纯 Linux 环境）以兼顾 Linux 服务端工具链与 MCU 交叉编译的平衡，并解决 Windows 原生环境下多套工具链的路径冲突与版本碎片化问题。Windows 原生降级为半支持（仅跨平台兼容性验证），Docker 为可选复现路径。
- **Keil 支持正式移除**：新增"关于 Keil 的说明"章节，从构建系统不兼容（CMake+Ninja+GCC 体系与 `.uvprojx` 异构）、C/C++ 标准受限（ARMCC 对 C++17 支持不完整）、跨架构能力缺失（Keil 仅支持 ARM，无法覆盖 RISC-V/Xtensa）、南向隔离原则破坏四个维度说明 Keil 天生不属于本套系统。PR 规约新增禁止引入 Keil 工程文件与 ARMCC 专属适配代码的条款。

### 文档全面对齐当前架构

- **`docs/service_spec.md`** — 重写"服务编写规范"与"应用层解耦规范"。删除过时的 `vfs_open`/`vfs_write` POSIX 风格 API 描述，统一为 `device_find_by_label`/`device_open`/`device_read`/`device_write`/`device_ioctl`/`task_manager_create_task`。新增"异步邮局模式"章节，说明 `SystemCmd` 单例 + 领域任务专属 `osal_queue` 的解耦机制。
- **`docs/debug_monitor.md`** — 修正反汇编路径为 `build/<preset>/disasm/`，移除 `hal_if.lst` 残留改为 `hal.lst`。补充 RISC-V (CH32V307) 异常寄存器 (`mepc`/`mcause`/`mtval`) 说明，新增 `wch-openocd` + `riscv32-wch-elf-gdb` CLI 路线，新增业务任务调试技巧表。
- **`docs/osal_switching.md`** — 工具链命令更新为 `cmake --preset Debug`（由 `CMakePresets.json` 自动选择 `gcc-arm-none-eabi.cmake` / `riscv32-wch-elf-ch32v307.cmake`），补充三端路径探测说明。
- **`NOTICE.md`** — 修正工具链版本：ARM GCC 13.3.1 → 14.2.1 (STM32CubeCLT)，RISC-V GCC 15.2.0 → 8.2.0 (WCH MounRiver Studio)。C/C++ 标准从 C23/C++23 降为 C17/C++17（与实际 `CMakeLists.txt` 一致）。将 `hal_if` 术语统一为 `hal/`。业务任务创建 API 从 `osal_task_create_handle` 改为 `task_manager_create_task`。
- **`USAGE.md`** — 术语表新增 `hal/`、`bus/`、`vfs/`、`DRIVER_REGISTER`、`device_find_by_label`、`task_manager_create_task`、`SystemCmd` 等当前架构实际术语；删除过时的 `hal_if`、`soc_port_`。
- **`API_COMPATIBILITY.md`** — 稳定接口列表新增 `device.h` 的 `device_*` 系列、`task_manager.h` 的 `task_manager_create_task`、`system_init.h` 的两段式点火 API。实验性接口新增 `SystemCmd`、`bus/**/*.h`、`vfs/**/*.h`。
- **`FILE_INDEX.md`** — 新增 `bus/`、`vfs/` 子系统索引；`board/src/` 补充 `dev_lifecycle.c`/`bus.c`/`config_store.c`/`task_config.c`/`task_utils.c`；`system_cpp/` 补充 `system_cmd.hpp`/`system_cmd.cpp`/`task_manager.cpp`。
- **`README.md`** — 修正 badge 为 C++17/C17，工具链版本对齐实际（ARM GCC 14.2.1 / RISC-V GCC 8.2.0 / Xtensa GCC / MinGW 8.1.0）。

### Windows 三平台编译验证

- 在 Windows 原生 cmd/PowerShell 下成功编译验证 STM32F407ZGT6 / CH32V307 / ESP32-S3 三节点
- **`STM32F407ZGT6/cmake/gcc-arm-none-eabi.cmake`** — 修复 Windows 工具链路径拼接缺失 `.exe` 后缀问题，改用 `find_program` 自动处理。补充 STM32CubeCLT 安装路径到 HINTS
- **`CH32V307/cmake/riscv32-wch-elf-ch32v307.cmake`** — 修复正则表达式匹配和工具链路径问题，补充 MounRiver_Studio2 安装路径到 HINTS
- **`CH32V307/CMakeLists.txt`** — `python3` 替换为 `${Python3_EXECUTABLE}`，避免 Windows Store stub 干扰

### 架构基准统一

- 删除所有 STM32 专属示例和 CubeMX 集成章节，替换为厂商无关的 HAL 集成指南
- 明确 ARM Cortex-M 与 RISC-V RV32 为通用基准（支持 Linux/Windows/Docker 三平台）
- ESP32 (Xtensa) 作为异构架构，走原生 Linux/Windows 工具链（ESP-IDF 官方双端，不走 Docker）

### 子系统文档与顶层文档对齐

- **`board/docs/devicetree.md`** — 标题从"ESP32-S3 设备树说明"改为通用标题，文件布局从 ESP32 专属改为通用 `<project>/mini_tree/board/` 路径模板，新增 `BOARD_DTS` 变量传入说明，CMake 集成章节补充 `${Python3_EXECUTABLE}` 调用与三节点集成方式差异。
- **`bus/spi/SPI.md`** — 完整重写为多架构通用文档。明确 HAL 实现由项目通过 `HAL_SRCS` 变量提供；`compatible` 的 `<vendor>` 段因芯片而异（STM32/CH32/ESP32）；引脚映射区分多端口 MCU（`port<<4|pin`）与单端口 SoC（裸 GPIO）；HAL 实现文件命名约定为 `hal/spi/spi_hal_<chip>.c`。
- **`tools/README.md`** — 补充 dtc-lite 生成的完整文件列表（`board_nodes.h` / `board_handles.h`），新增 `add_custom_command` CMake 集成示例，新增 `genconfig.py` / `kconfig_gui.py` / `post_build_crc.py` / `firmware_size_report.py` 等工具说明。
- **`docs/getting_started.md`** — 删除 `toolchain_arm_cm4f.cmake` 过时引用，改为 `cmake --preset Debug`；删除 `soc_port_` / `hal_if` 过时术语，统一为 `HAL_SRCS` / `hal/`；用户工程结构示例对齐异构多核项目布局；HAL 实现约定改为通过 `HAL_SRCS` 变量传入。
- **`docs/porting_guide.md`** — 已对齐多架构基准（含 ARM Cortex-M7+M4 / GD32 同构双核 / RISC-V 双核示例），无需修改。
- **`ARCHITECTURE.md`** — 修正配置菜单中 `C++23/C23` 为 `C++17/C17`，工具链版本表统一为 C17/C++17 标准。
- **`CONTRIBUTING.md`** — 工具链版本对齐实际（ARM GCC 14.2.1 / RISC-V GCC 8.2.0 / Xtensa GCC / MinGW 8.1.0），构建验证改为 `build.sh` 统一入口 + CMake Presets，补充 `find_program` 三端自动探测说明。
- **`docs/faq.md`** — 删除 `soc_port_` / `osal_task_create_handle` 过时引用，新增 `task_manager_create_task` 与 `device_find_by_label` 排查章节，新增"业务任务与硬件解耦"异步邮局模式说明，C23 错误改为 C17 历史说明。

---

## [Historical]



### 新项目规划

- **`ROADMAP.md`** — **新建**，新增项目路线图，以异构多核项目（Heterogeneous-Multicore: STM32F407ZGT6 / CH32V307 / ESP32-S3 / i.MX6ULL）为参考实现，验证框架在 ARM Cortex-M / RISC-V RV32 / Xtensa LX 三类架构、Linux/Windows/Docker 三平台原生编译下的适配与跨平台一致性
- **`FILE_INDEX.md`** — 新增 `ROADMAP.md` 索引项

### Linux DTS 源级兼容

- **`tools/dtc-lite.py`** — 修复 `&label` overlay 合并（`_merge_overlays()`）、支持 `/include/` 指令、支持 `()` 和宏标识符在 `<>` 内的解析、`reg` 按 `#address-cells/#size-cells` 分组生成
- **`board/include/device.h`** — 新增 `device_reg_t` 结构体和 `device_get_reg()` API，支持多地址单元 reg 读取
- **`board/include/freertos_compat.h`** — **新建**，MinGW POSIX 信号兼容层（`sigaction`、`SIGALRM`、`sigset_t`）
- **`board/include/sys/times.h`** — **新建**，MinGW `sys/times.h` 兼容头
- **`lib/rtthread/libcpu/x86/atomic.c`** — **新建**，RT-Thread x86 原子操作实现
- **`lib/freeRTOS/portable/ThirdParty/GCC/Posix/utils/wait_for_event.h`** — **新建**，FreeRTOS POSIX port 缺失头文件
- **`lib/freeRTOS/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c`** — 修复 MinGW 缺少 `pthread_mutexattr_setrobust`/`pthread_mutex_consistent`
- **`Makefile`** — FreeRTOS port.c 注入 `freertos_compat.h`；RT-Thread 原子操作按平台选择（ARM→atomic_arm.c，x86→atomic.c）
- **`osal/src/osal_freertos.c`** — `vApplicationGetIdleTaskMemory` 第三参数类型更正为 `configSTACK_DEPTH_TYPE*`
- **三后端 POSIX 构建验证通过** — NULL / FreeRTOS / RT-Thread 在 MinGW-w64 下全部编译成功
- **MCU 交叉编译验证通过** — `arm_cm4f + NULL` 和 `arm_cm4f + FreeRTOS` 均通过

### 中断框架基础支持

- **`board/include/device.h`** — 新增 `device_irq_t` 结构体（irq/type/flags），`device_node_t` 新增 `irqs`/`irq_count` 字段
- **`board/src/board_device.c`** — 新增 `device_get_irq()` API，与 `device_get_reg()` 同模式
- **`tools/dtc-lite.py`** — 中断三件套：
  - `_scan_interrupt_controllers()` — DFS 扫描所有含 `interrupt-controller` 的节点，构建 `#interrupt-cells` 映射
  - `_resolve_device_interrupts()` — 沿父链解析 `interrupt-parent`，按 `#interrupt-cells` 分组（1/2/3 cells），生成 `(irq, type, flags)` 元组
  - C 生成器 — 为每个有 `interrupts` 的设备生成 `DEV_xxx_IRQS[]` 数组
- **`tools/dtc-lite.py`** — 修复预处理器误吞 `#interrupt-cells` 等 DTS `#` 属性行（只跳过 `#define`/`#ifndef`/`#ifdef`/`#endif`，其余放行）

### 双核 AMP 支持 (Asymmetric Multi-Processing)

- **`Kconfig`** — 新增 `menu "Multi-core Configuration"` → `CPU_CORES`（默认 1，范围 1-2）。1 = 单核，2 = 双核 AMP 模式（Core 0 跑 RTOS，Core 1 跑裸机）
- **`hal/cpu/hal_cpu.h`** — 新增 3 个 AMP 函数声明：`hal_cpu_secondary_startup()`、`hal_cpu_baremetal_entry()`、`hal_cpu_get_id()`
- **`hal/cpu/hal_cpu_amp.c`** — **新建**，三个 weak 符号默认实现（副核入口死循环、启动空操作、核心 ID 返回 0）
- **`hal/paths.cmake`** — `CPU_CORES > 1` 时条件编译 `hal_cpu_amp.c`
- **`osal/src/osal_freertos.c`**、`osal/src/osal_rtthread.c`** — AMP 模式下 `core_id > 0` 自动回退到 Core 0 并打印警告
- **`system_cpp/src/system_init.cpp`**、**`system_c/src/system_init.c`** — Phase 2 末尾调用 `hal_cpu_secondary_startup()`

### C 修复

- **`core/src/buffer_pool.c`** — 移除重复定义的 `bp_alloc_isr`（第 219 行已定义，第 252 行重复），修复 ARM GCC 全架构编译错误。

### infrastructure 整理

- **`core/include/system_log.hpp` → `system_log.h`** — 文件内容为纯 C 兼容宏，改名 `.h` 避免 C 文件包含 `.hpp`；同步更新全部 8 处 `#include`。
- **`system_c/include/`** — `system_scrubber.h`、`system_wdt.h` 自声明函数，不再套壳包含 `.hpp`。
- **`tools/menuconfig.py` → `tools/kconfig_gui.py`** — 文件名与 kconfiglib 的 `menuconfig` 模块冲突导致自导入失败；重命名消除冲突，同步更新 5 处引用。
- **`cmake/toolchain_riscv.cmake`** — 工具链文件自动设置 `FREERTOS_PORT=GCC_RISC_V`，编译时无需额外传参。

### 文档新增

- **`README.md`** — 新增"致谢与设计参考"章节，列出 LVGL、Linux、RT-Thread、Zephyr 等设计来源。
- **`NOTICE.md` / `ARCHITECTURE.md`** — MPU/PMP、Power Management、64-bit 适配三条待优化项移至 `TODOLIST.md`。



### RT-Thread 端口修复

- **`lib/rtthread/libcpu/arm/cortex-m7/context_gcc.S`** — HardFault_Handler FPU 现场修复：原实现仅压入占位 flag，未保存实际 FPU 寄存器 (d8-d15 / s16-s31)。对齐 CM4F 实现，增加 `TST lr, #0x10` 检查 EXC_RETURN bit4，FPU 活跃时 `VSTMDBEQ` 保存 d8-d15，再按 flag → exec_return 顺序压栈。修复后 FPU 异常现场可被 `rt_hw_hard_fault_exception` 完整捕获。
- **`lib/rtthread/libcpu/arm/cortex-m4/cpuport.c`** — `rt_interrupt_enter/leave` 中断嵌套计数器修复：原实现为空桩（仅 hook + log），`rt_interrupt_nest` 全局计数器永不递增。补入 `extern volatile rt_atomic_t rt_interrupt_nest`，enter 做 `rt_atomic_add`，leave 做 `rt_atomic_sub`，与 `irq.c` 通用弱实现保持一致。CM7/CM3/RISC-V 端口无此问题（均走通用实现）。

### dtc-lite.py 优化

- **ROM 安全**: probe/remove 函数指针表添加 `const`，从 `.data` 移至 `.rodata`，防 RAM 篡改
- **增量构建**: 新增 `_write_if_changed()` 防抖写入，内容不变时不碰磁盘时间戳，消除雪崩重编
- **Kconfig 隔离**: extern 声明添加 `__attribute__((weak))`，被 Kconfig 裁剪的驱动不会导致链接器 Undefined Reference
- **扫描性能**: `_scan_drivers()` 跳过 >1MB 的大文件，避免 SDK 巨型文件被整吞

### OSAL & RT-Thread 修复

- **`osal_rtthread.c`**: 适配 RT-Thread v5.x 内核 API。修复信号量/互斥量 count 访问、时基单位转换（OS ticks → RT-Thread `RT_TICK_PER_SECOND`）、OP和MQ默认初始化宏
- **`osal_freertos.c` / `osal_null.c`**: 同步信号量 count 类型适配

### 构建脚本硬化 — 全量类型注解 + argparse + 原子写入

所有 `tools/` Python 脚本统一遵循两条安全基线：

**原子写入**：生成 `.h`/`.c` 文件采用先写 `.tmp` 再 `shutil.move()` 替换的模式，防止中断留下残缺文件。写入前通过 `_write_if_changed()`/`_needs_update()` 做内容比对，内容不变时不动磁盘，保护构建时间戳避免雪崩重编。

**显式类型注解**：每个函数/方法的参数、返回值、局部变量均显式标注类型，不依赖自动推导。

| 脚本 | 注解规模 |
|------|---------|
| `genconfig.py` | 76 处 |
| `post_build_crc.py` | 98 处 |
| `menuconfig.py` | 15 处 |
| `dtc-lite.py` | ~1200 处 |

可在 CI 中通过 `mypy tools/ --strict` 做静态类型检查。

各脚本具体变更：

- **`tools/genconfig.py`**: `argparse` 替换裸 `sys.argv`，`pathlib.Path` 替换 `os.path`，新增 `_atomic_write()` 和 `_needs_update()` 内容比对防抖
- **`tools/dtc-lite.py`**: `argparse` 替换裸 `sys.argv`，`pathlib.Path` 替换 `os.path`，新增 `_atomic_write()`，移除未使用 `tempfile`/`OrderedDict` 导入
- **`tools/post_build_crc.py`**: `pathlib.Path` 替换 `os.path`，新增 `_atomic_write()` + `_needs_update()`，提取 `_compute_crc()`/`_replace_in_file()`/`_write_new_header()` 辅助函数
- **`tools/menuconfig.py`**: 15 处显式类型注解（`main()` → `int` 等）

### C 修复

- **`core/include/system_log.hpp`**: 新增 `DRV_LOGV` 宏 — `osal_log(OSAL_LOG_DEBUG, tag, fmt, ...)`，修复 board_driver.c 编译错误
- **`core/include/compiler_compat.h`**: `COMPAT_WEAK` 从函数式宏 `#define COMPAT_WEAK(func) __attribute__((weak)) func` 重构为属性宏 `#define COMPAT_WEAK __attribute__((weak))`，消除 implicit-int 冲突；同步更新 `osal_freertos.c`、`osal_rtthread.c`、`osal_null.c` 共 6 处调用点

### 文档新增 (已合并至此文件，源文件已删除)

日志系统分为 **SYS_LOG（系统日志）** 和 **DRV_LOG（驱动日志）** 两层：

```
SYS_LOG — 应用/框架层，Kconfig 可选择后端
  ├── CONFIG_SYS_LOG_USE_OSAL   → osal_log()
  ├── CONFIG_SYS_LOG_USE_ESP    → ESP_LOGI/LOGW/LOGE
  └── CONFIG_SYS_LOG_USE_PRINTF → printf（默认，无外部依赖）

DRV_LOG — 驱动层，固定走 osal_log()
  ├── DRV_LOGE / DRV_LOGW       → Error/Warning 同时推入 production_log 黑匣子
  └── DRV_LOGI / DRV_LOGD / DRV_LOGV → Info/Debug/Verbose 仅 osal_log()
```

- SYS_LOG 后端由 Kconfig 编译期选择，运行时不可切换
- DRV_LOG 固定在 `osal_log()` 之上，`DRV_LOGE`/`DRV_LOGW` 额外推入黑匣子用于故障事后分析
- `DRV_LOGV` 可在 `NDEBUG` 时被编译器优化剔除
- 各模块统一包含 `system_log.hpp`，不直接调用 `printf` 或 `osal_log`

### genconfig.py 修复

- **magic number**: `_C_SPACES_INDENT` 改为 `self._indent` 单源，整树一致
- **STRING 引号**: `int` 类型值误加引号导致 `make` 比较失败，按 Kconfig 类型严格处理
- **copy2 时间戳**: `shutil.copy2` 保留 mtime 导致增量构建防抖误判，改用 `shutil.move` 避免时间戳传播
