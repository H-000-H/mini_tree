# Changelog / 变更记录

> 记录中间件 shelf 用户可见的变化。更细的设计动机见 [docs/design_decisions.md](docs/cn/design_decisions.md)。
> User-visible changes to the middleware shelf. Deeper design rationale: [docs/design_decisions.md](docs/cn/design_decisions.md).

---

## [Unreleased] / 未发布

### 配置系统 / Configuration

- **`CONFIG_SYSTEM`（默认自开）/ `CONFIG_EVENT_BUS` / `CONFIG_SYSTEM_CMD`（默认关闭）总开关**：System 模块、EventBus、命令系统均可整体裁剪，CMake 按 `.config` 裁剪源文件；另有 `CONFIG_BOTTOM_HALF_QUEUE_DEPTH`、`CONFIG_PRODUCTION_LOG_SLOT_COUNT`、`CONFIG_BOARD_MAX_SAFETY_PINS`、`CONFIG_BOARD_SAFETY_MAX_CALLBACKS`、`CONFIG_FREERTOS_USE_TIMERS`、`CONFIG_FREERTOS_HEAP_SIZE`、`CONFIG_RTT_HEAP_SIZE` 入库。
  **Master switches**: `CONFIG_SYSTEM` (default on) / `CONFIG_EVENT_BUS` / `CONFIG_SYSTEM_CMD` (off by default): System, EventBus and the command infra are fully trimmable; CMake trims sources per `.config`; more knobs moved into Kconfig (`CONFIG_BOTTOM_HALF_QUEUE_DEPTH`, `CONFIG_PRODUCTION_LOG_SLOT_COUNT`, `CONFIG_BOARD_MAX_SAFETY_PINS`, `CONFIG_BOARD_SAFETY_MAX_CALLBACKS`, `CONFIG_FREERTOS_USE_TIMERS`, `CONFIG_FREERTOS_HEAP_SIZE`, `CONFIG_RTT_HEAP_SIZE`).

### 内存与 DTS / Memory & DTS

- **静态内存多轮压缩**（arm-none-eabi / Cortex-M4F 实测）：`vfs-adc` 池 27.5→4.7 KB；VFS 池改由 `DTC_GEN_COUNT_*` 驱动；EventBus / SystemCmd / Flash-Scrubber 默认关闭；队列池 1×2048、config_store 8 项、mutex 24、下半部 16。全库 85.3 → **28.0 KB**；**默认最小（无外设）≈ 2.8 KB**，仍可再压（见 [memory_footprint.md](docs/cn/memory_footprint.md) §2.2）。
  **Multi-round static-RAM cuts** (measured, Cortex-M4F): `vfs-adc` pool 27.5→4.7 KB; VFS pools `DTC_GEN_COUNT_*` driven; EventBus / SystemCmd / Flash-Scrubber off by default; queue pool 1×2048, config_store 8, mutex 24, bottom-half 16. Whole library 85.3 → **28.0 KB**; **default minimum (no peripherals) ≈ 2.8 KB**, still trimmable (see [memory_footprint.md](docs/cn/memory_footprint.md) §2.2).
- **裸机队列池改为"基础数 + EventBus 自动 +1"**：`CONFIG_OSAL_NULL_MAX_QUEUES` 为基础数（默认 0，不占内存），开启 `CONFIG_EVENT_BUS` 时自动 +1；FreeRTOS/RTT 堆也 Kconfig 化（`CONFIG_FREERTOS_HEAP_SIZE` / `CONFIG_RTT_HEAP_SIZE`）。
  **Bare-metal queue pool is now "base + auto-1 for EventBus"**: `CONFIG_OSAL_NULL_MAX_QUEUES` is the base (default 0, no RAM); enabling `CONFIG_EVENT_BUS` auto-adds 1; FreeRTOS/RTT heaps are Kconfig-gated too (`CONFIG_FREERTOS_HEAP_SIZE` / `CONFIG_RTT_HEAP_SIZE`).
- **字段宽度/池宏移入 `board/define/` 配置头体系**：每 VFS 一个 `board/define/vfs/board_define_<name>.h`（普通 C 宏，板级改头或 `-D` 覆盖）；池数量仍由 DTS 节点数自动生成（`DTC_GEN_COUNT_*`）；不再走 DTS `#define` 透传。
  **Field-width & pool macros moved into `board/define/` config headers**: one `board/define/vfs/board_define_<name>.h` per VFS (plain C macros, board override via header or `-D`); pool counts still auto-generated from DTS node counts (`DTC_GEN_COUNT_*`); the DTS `#define` pass-through was removed.
- **DTS 节点模板库**：`board/dtsi/vfs/`（11）+ `board/dtsi/drivers/`（37），参数全 0 占位 + 用法注释，板级拷走填值。
  **DTS node templates**: `board/dtsi/vfs/` (11) + `board/dtsi/drivers/` (37), all-0 placeholders with usage comments.
- **调度方案内存对比基准（最小固件实测）**：`memory_footprint.md` 新增 §4，用最小链接固件（startup + system 层 + 全库，仿 STM32F4 链接脚本）实测全裸 `while` / 协调式 / 抢占式 / 5 个 RTOS 后端 × C/C++ system 后端的 `text`/`data`/`bss`。结论：全裸最省（86 B text，零 RAM）；裸机 xtask 比最小 RTOS 内核（uC/OS-II ~33 KB）省 ~1.7 KB 且无独立任务栈；RTOS 内核开销 uC/OS-II < uC/OS-III < ThreadX < FreeRTOS < RT-Thread；C system 后端比 C++ 省。裸机调度三态（`XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`）由 `Kconfig.mini_tree` choice 选择，CMake 注入 `MINI_TREE_XTASK_*` 宏。
  **Scheduler memory comparison baseline (minimal-firmware measured)**: `memory_footprint.md` §4 now measures, via a minimal linked firmware (startup + system layer + whole library, STM32F4-like script), the `text`/`data`/`bss` of bare `while` / cooperative / preemptive / 5 RTOS backends × C/C++ system backends. Takeaways: bare `while` is smallest (86 B text, zero RAM); bare-metal xtask beats the smallest RTOS kernel (uC/OS-II ~33 KB) by ~1.7 KB with no per-task stack; RTOS kernel cost ordering uC/OS-II < uC/OS-III < ThreadX < FreeRTOS < RT-Thread; C system backend is smaller than C++. The bare-metal scheduler tri-state (`XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`) is a `Kconfig.mini_tree` choice; CMake injects `MINI_TREE_XTASK_*` macros.

### 调度器与 API / Scheduler & API

- **裸机调度器三态落地（choice + CMake 双重门控）**：`Kconfig.mini_tree` 新增「裸机调度器」choice（`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`，默认 `XTASK_COOP`），取代旧的单开关 `CONFIG_XTASK` + `CONFIG_XTASK_PREEMPT` 软编码；CMake 据 `.config` 注入 `MINI_TREE_XTASK_*` 宏决定编译 `xtask_coop.c` 或 `xtask_preempt.c`。`CONFIG_OSAL_NULL_TASK_CPP` 门控改为 `depends on OSAL_NULL && SYSTEM_CPP && !XTASK_NONE`（无调度时强制关闭 C++ 封装）。
  **Bare-metal scheduler tri-state (choice + CMake dual gate)**: `Kconfig.mini_tree` gains a "bare-metal scheduler" choice (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`, default `XTASK_COOP`), replacing the old `CONFIG_XTASK` + `CONFIG_XTASK_PREEMPT` soft switches; CMake injects `MINI_TREE_XTASK_*` macros to pick `xtask_coop.c` or `xtask_preempt.c`. `CONFIG_OSAL_NULL_TASK_CPP` now gates on `!XTASK_NONE`.
- **抢占式调度器 `xtask_preempt.c` 完工可编译**：N+1 链表多优先级（分组优先级 + CLZ 定位最高，O(1)），支持可延迟/可休眠/可抢占，无就绪任务时精确 WFI 到最早到期时刻；补齐 `CHOSEN_SCHEDULER_TIM` fallback（无 chosen 板时 `xscheduler_start()` 直接返回，与协调式对称）。原 Kconfig help 的"实验性/可能编不过"已不适用。
  **Preemptive scheduler `xtask_preempt.c` completed & compilable**: N+1 linked-list multi-priority (grouped priorities + CLZ for O(1) highest pick), delayable / sleepable / preemptive, precise WFI to the earliest deadline when idle; added `CHOSEN_SCHEDULER_TIM` fallback (returns early from `xscheduler_start()` on boards without a chosen tick device, symmetric with the cooperative version). The old "experimental / may not compile" Kconfig note no longer applies.
- **`xtask.h` 对外 API 调整**：新增 `x_scheduler_poll(void)`（无参全局轮询）、`x_task_run_preempt`、`x_scheduler_task_create(name, period_ms, priority, cb, param)`（抢占式带优先级）；协调式 `xscheduler_task_create` 签名简化为 `(task, name, cb, period_ms)`。两套实现对外 API 完全一致，调用方无感切换。
  **`xtask.h` public API adjusted**: added `x_scheduler_poll(void)` (parameterless global poll), `x_task_run_preempt`, and `x_scheduler_task_create(name, period_ms, priority, cb, param)` (preemptive, priority-aware); the cooperative `xscheduler_task_create` signature is simplified to `(task, name, cb, period_ms)`. Both implementations keep an identical external API — caller code switches transparently.
- **裸机 C++ 任务封装 `osal_task.cpp` 双分支**：`osal_task_create` 按 `CONFIG_XTASK_PREEMPT` 分两分支——协调式 `period` 为周期 ms；抢占式同签名新增 `priority`（数值越大越优先），`stack_size` 在裸机下复用为周期。不再"抢占式整段关闭 C++ 重载"。
  **Bare-metal C++ wrapper `osal_task.cpp` dual-branch**: `osal_task_create` now branches on `CONFIG_XTASK_PREEMPT` — cooperative uses `period` as cycle ms; preemptive adds a `priority` arg (higher = more urgent) and reuses `stack_size` as the cycle on bare metal. The C++ wrapper is no longer "fully disabled under preemptive".
- **VFS TIM 新增命令与 inline**：`vfs-tim.h` 新增 `TIM_CMD_CLEAR_UPDATE_FLAG`（第 24 条命令）与 `vfs_tim_fast_clear_update_flag()` inline（ISR 上半部非阻塞清更新标志，无生命周期依赖）。
  **VFS TIM new command & inline**: `vfs-tim.h` adds `TIM_CMD_CLEAR_UPDATE_FLAG` (24th command) and `vfs_tim_fast_clear_update_flag()` inline (non-blocking update-flag clear for ISR top halves, no lifetime dependency).
- **`compiler_compat.h` 新增 `COMPAT_WFI()`**：平台无关低功耗等待封装（Cortex-M `__WFI()` / RISC-V `wfi` / 其他空操作），供调度器空闲精确休眠使用。
  **`compiler_compat.h` adds `COMPAT_WFI()`**: a platform-agnostic wait-for-interrupt wrapper (`__WFI()` on Cortex-M, `wfi` on RISC-V, no-op elsewhere), used by the scheduler's precise idle sleep.

### 工具链 / Toolchain

- **Keil Studio 单独列为支持项**：作者实测确认与经典 µVision 本质不同（VS Code 内核 + CMake 一等公民 + clangd + 官方调试/云编译），推荐作调试（与构建）环境；经典 µVision 维持不推荐、不支持立场。详见 [keil_integration.md](docs/cn/keil_integration.md) §2.1 与 [design_decisions.md](docs/cn/design_decisions.md) 工具链表。
  **Keil Studio is now a supported entry on its own**: hands-on verified as fundamentally different from classic µVision (VS Code core + first-class CMake + clangd + official debug/cloud build); recommended as a debug (and build) environment. Classic µVision stays not recommended / unsupported. See [keil_integration.md](docs/cn/keil_integration.md) §2.1 and the toolchain table in [design_decisions.md](docs/cn/design_decisions.md).

### 目标平台 / Targets

- **新增 `PLATFORM_ARM_CM0`（ARM Cortex-M0 / M0+）**：Kconfig 平台选项，三个 OSAL 后端均可选。FreeRTOS 从官方仓库拉取最新 `ARM_CM0` port（`port.c` + `portasm.c`）；RT-Thread 拉取 `cortex-m0` port（`context_gcc.S` + `cpuport.c`）。裸机 / FreeRTOS / RT-Thread 三后端 M0 全量构建实测通过（`-mcpu=cortex-m0 -mthumb`）。
  **New `PLATFORM_ARM_CM0` (ARM Cortex-M0 / M0+)**: Kconfig target option, selectable for all three OSAL backends. FreeRTOS gains the upstream `ARM_CM0` port (`port.c` + `portasm.c`); RT-Thread gains the `cortex-m0` port (`context_gcc.S` + `cpuport.c`). Full-library builds verified on M0 for bare-metal / FreeRTOS / RT-Thread (`-mcpu=cortex-m0 -mthumb`).
- **FreeRTOSConfig.h 按 `__ARM_ARCH_6M__` 自动适配 M0**：无 MPU（`configENABLE_MPU 0`）、禁用 CLZ 优化任务选择（`configUSE_PORT_OPTIMISED_TASK_SELECTION 0`）、NVIC 仅 4 级优先级（`configMAX_SYSCALL_INTERRUPT_PRIORITY 3`）；M3/M4F/M7 行为不变。
  **FreeRTOSConfig.h auto-adapts to M0 via `__ARM_ARCH_6M__`**: no MPU (`configENABLE_MPU 0`), CLZ-optimised task selection off (`configUSE_PORT_OPTIMISED_TASK_SELECTION 0`), NVIC 4-level priority (`configMAX_SYSCALL_INTERRUPT_PRIORITY 3`); M3/M4F/M7 behavior unchanged.
- **RT-Thread M0 原子操作退回软件实现**：M0/M0+ 无 `LDREX/STREX` 指令，`rtconfig.h` 按 `__ARM_ARCH_6M__` 关闭 `RT_USING_HW_ATOMIC`，由 `rtatomic.h` 内联的 `rt_soft_atomic_*`（关中断）提供，`atomic_arm.c` 不编入。
  **RT-Thread M0 falls back to software atomics**: M0/M0+ lacks `LDREX/STREX`; `rtconfig.h` disables `RT_USING_HW_ATOMIC` on `__ARM_ARCH_6M__`, using the inline `rt_soft_atomic_*` (IRQ-lock) implementations and excluding `atomic_arm.c`.
- **修复 `FREERTOS_PORT` 默认值遮蔽 Kconfig 派生**：`lib/CMakeLists.txt` 原写死 `GCC_ARM_CM4F`，导致 `lib/freeRTOS/CMakeLists.txt` 的 Kconfig 平台自动选 port 逻辑永远不执行；现在未通过 `-D` 指定时完全按 `CONFIG_PLATFORM_*` 自动选择。
  **Fixed `FREERTOS_PORT` default shadowing Kconfig derivation**: `lib/CMakeLists.txt` hardcoded `GCC_ARM_CM4F`, which made the Kconfig-driven port selection in `lib/freeRTOS/CMakeLists.txt` dead code; the port is now always derived from `CONFIG_PLATFORM_*` unless overridden via `-D`.

---

## [v1.0.0] / 正式版 / Official Release

> **正式版 / Official Release**：平台无关的稳定基线——风格统一、构建可验证、文档双语、生态按需。
> A stable, platform-agnostic baseline: unified coding style, verified builds, bilingual docs, on-demand ecosystem.
>
> 本版核心亮点 / Release highlights：代码风格体系（`.clang-format` + 分层 `.clang-tidy`，app 以下强规定）、全库命名统一（`kTag`→`k_tag`、`struct Event`→`event`、`namespace mini_tree`、`xTask`→`x_task`、`dev`→`pdev` 指针显式化）、通用 CMake 芯片无关路径最小构建实测通过、安全类模块与异构多核 AMP 作为可选积木、全部文档中英双语。
> Coding-style enforcement (`.clang-format` + layered `.clang-tidy`, mandatory below `app/`); repo-wide naming unification (`kTag`→`k_tag`, `struct Event`→`event`, `namespace mini_tree`, `xTask`→`x_task`, `dev`→`pdev` explicit pointers); verified chip-agnostic CMake build; safety modules & heterogeneous AMP as optional bricks; fully bilingual docs.

### 产品驱动与布局 / Product Drivers & Layout

- 37 个产品驱动迁入 `drivers/<chip>/{include,src}`，统一 `DRIVER_REGISTER` + dtc-lite 编译期 probe；不再使用独立 `components/driver_*`（ws2812 为唯一厂商例外）。
  The 37 product drivers moved to `drivers/<chip>/{include,src}`, all via `DRIVER_REGISTER` + compile-time probe by dtc-lite; standalone `components/driver_*` is gone (ws2812 is the only vendor exception).
- 板级 DTS/DTSI 外置（`board_port.cmake` 注入）；中间件保持纯架构占位——一份 mini 配多 MCU，不硬编码 `board_*` / `IDF_TARGET`。
  Board DTS/DTSI externalized (injected via `board_port.cmake`); the middleware stays pure-architecture — one mini tree, many MCUs, no hardcoded `board_*` / `IDF_TARGET`.

### 架构与代码 / Architecture & Code

- HAL 全面 weak 空实现；Bus/VFS 覆盖 gpio/spi/uart/i2c/i2s/can/usb/adc/dac/tim/rtc/iwdg/wwdg；USB 经 TinyUSB + 板级 `usb_tusb_port` 约定。
  HAL is fully weak empty implementations; Bus/VFS covers gpio/spi/uart/i2c/i2s/can/usb/adc/dac/tim/rtc/iwdg/wwdg; USB goes through TinyUSB plus the board-level `usb_tusb_port` convention.
- ETL 作为上层 C++ 基础默认链入；`lib/` 仅 vendor FreeRTOS / RT-Thread / ETL，其余积木（TinyUSB / lwIP / cJSON 等）按需 FetchContent。
  ETL is the default-linked C++ foundation; `lib/` vendors only FreeRTOS / RT-Thread / ETL, everything else (TinyUSB / lwIP / cJSON…) is FetchContent'd on demand.
- clangd 体系：`compile_flags.txt` + `ide/stubs`，禁止子目录覆盖。
  clangd setup: `compile_flags.txt` + `ide/stubs`; per-directory overrides are forbidden.

### 代码风格与命名 / Code Style & Naming

- 新增 `.clang-format`（Allman 大括号、单语句 if/for/while 去大括号、4 空格、100 列、指针靠左）与分层 `.clang-tidy`：根 = 内核区（app 以下非 cpp 全小写无前缀）；`app/` 与 `system_cpp/` = Google 区（PascalCase + s_/g_/k_ 前缀）；宏全大写（container_of 等少数例外）；格式化排除 `lib/`。
  New `.clang-format` (Allman braces, no braces for single-statement if/for/while, 4-space, 100 cols, pointer-on-left) and layered `.clang-tidy`: root = kernel zone (all-lowercase below `app/`); `app/` & `system_cpp/` = Google zone (PascalCase + s_/g_/k_ prefixes); macros all-uppercase (a few exceptions like `container_of`); formatting excludes `lib/`.
- 全库命名统一并 clang-tidy 全量扫描清零（`kTag`→`k_tag`、`struct Event/Subscriber`→`event/subscriber`、`namespace MiniTree`→`mini_tree`、`xTask`→`x_task`、`dev`→`pdev` 等）。
  Repo-wide naming unification, full clang-tidy scan clean (`kTag`→`k_tag`, `struct Event/Subscriber`→`event/subscriber`, `namespace MiniTree`→`mini_tree`, `xTask`→`x_task`, `dev`→`pdev`, etc.).

### 构建与文档 / Build & Docs

- 通用 CMake 芯片无关路径最小构建实测通过；全部文档中英双语并统一收进 `docs/`（目录页 [docs/README.md](docs/cn/README.md)）。
  Verified chip-agnostic CMake minimal build; all docs are bilingual and consolidated under `docs/` (index: [docs/README.md](docs/cn/README.md)).
- 补 [LICENSE](LICENSE)（Apache-2.0）；[NOTICE](NOTICE) 全面重写（组件版本 / 版权 / SPDX / 合规要点）；[CONTRIBUTING.md](CONTRIBUTING.md) 新增 SPDX 头规范。
  Added [LICENSE](LICENSE) (Apache-2.0); [NOTICE](NOTICE) fully rewritten (component versions / copyright / SPDX / compliance notes); [CONTRIBUTING.md](CONTRIBUTING.md) gained the SPDX header spec.

---

## [Historical] / 历史

多轮重构（设备树、硬件直投、OSAL、安全回路、文档迁徙等）详见 [docs/design_decisions.md](docs/cn/design_decisions.md)。
Multiple refactoring rounds (device tree, direct hardware mapping, OSAL, safety loops, doc migration, etc.) — see [docs/design_decisions.md](docs/cn/design_decisions.md).
平台验证历史以各 SoC 工程仓库为准。
Platform verification history lives in the per-SoC project repositories.

---

## 相关文档 / Related Documents

- [docs/roadmap.md](docs/cn/roadmap.md) · [docs/todolist.md](docs/cn/todolist.md) · [docs/api_compatibility.md](docs/cn/api_compatibility.md)
