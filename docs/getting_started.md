# 快速开始 / Quick Start

> 从零把 `mini_tree` 配进你的平台工程：依赖 → 配置 → CMake → 点火。
> Integrate `mini_tree` into your platform project from scratch: dependencies → configuration → CMake → ignition.
>
> **参考模板工程 / Reference template**：[Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore)——mini_tree 的配套平台示例，含完整移植（DTS、HAL、`board_port.cmake`、AMP）。
> [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) — mini_tree's companion platform example with a full port (DTS, HAL, `board_port.cmake`, AMP).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 平台/应用工程师 / Platform and application engineers |
| **前置 / Prerequisites** | CMake、基本 C；有一块目标板或至少能链出固件 / CMake, basic C; have a target board or at least be able to link a firmware |
| **相关 / Related** | [porting_guide.md](porting_guide.md) · [usage.md](usage.md) · [ecosystem.md](ecosystem.md) · [tools/README.md](../tools/README.md) |

---

## 目录 / Table of Contents

1. [依赖 / 1. Dependencies](#1-依赖)
2. [获取与目录 / 2. Acquisition & Layout](#2-获取与目录)
3. [配置系统（Kconfig）/ 3. Configuration System (Kconfig)](#3-配置系统kconfig)
4. [CMake 集成 / 4. CMake Integration](#4-cmake-集成)
5. [板级 DTS 覆盖 / 5. Board DTS Override](#5-板级-dts-覆盖)
6. [点火时序 / 6. Ignition Sequence](#6-点火时序)
7. [IDE（clangd）/ 7. IDE (clangd)](#7-ideclangd)
8. [验收清单 / 8. Acceptance Checklist](#8-验收清单)

---

## 1. 依赖 / 1. Dependencies

| 依赖 / Dependency | 用途 / Purpose | 备注 / Notes |
| :--- | :--- | :--- |
| CMake ≥ 3.16 | 构建静态库 / Build the static library | Ninja / Make 均可 / either works |
| Python 3 | genconfig、dtc-lite、gen_compile_db / genconfig, dtc-lite, gen_compile_db | — |
| `lark` | dtc-lite 解析 / dtc-lite parsing | `pip install lark` |
| `kconfiglib`（可选 / optional） | menuconfig | 无则手改 `.config` / otherwise edit `.config` by hand |
| clang-format ≥ 15 / clang-tidy（可选 / optional） | 代码风格与命名检查 / Code style and naming checks | 见 [coding_style.md](coding_style.md) |
| 平台工具链 + SDK / Platform toolchain + SDK | 真机 / Real hardware | **只**链在平台工程，不进中间件公共头 / **only** linked into the platform project, never into middleware public headers |

---

## 2. 获取与目录 / 2. Acquisition & Layout

将本仓库作为子目录或 submodule，例如 `third_party/mini_tree`。你需要经常碰的路径：
Vendor this repository as a subdirectory or submodule, e.g. `third_party/mini_tree`. Paths you will touch often:

| 路径 / Path | 用途 / Purpose |
| :--- | :--- |
| `CMakeLists.txt` | `add_subdirectory` 入口 / entry point |
| `.config` / `Kconfig` | 功能裁剪 / feature trimming |
| `board/dts/board.dts` | 默认占位（必被平台覆盖）/ default placeholder (must be overridden by the platform) |
| `board/dtsi/` | 节点模板库：`example-soc.dtsi` + `vfs/`（11）+ `drivers/`（37），参数全 0 占位，板级拷走填值（见 [driver_guide.md](driver_guide.md) §1）/ node templates: `example-soc.dtsi` + `vfs/` (11) + `drivers/` (37), all-0 placeholders to copy & fill (see [driver_guide.md](driver_guide.md) §1) |
| `ide/stubs/` | 无生成物时的 IDE 头 / IDE headers when there are no build artifacts |

---

## 3. 配置系统（Kconfig）/ 3. Configuration System (Kconfig)

### 3.1 生成 `config.h` / Generate `config.h`

```bash
cd path/to/mini_tree
python3 tools/genconfig.py Kconfig build/generated/kconfig/mini_tree --config .config
```

根 `CMakeLists.txt` 在配置阶段也会调用同等逻辑。
The root `CMakeLists.txt` runs the same logic during the configure stage.

### 3.2 常用选项 / Common Options

| 菜单 / Menu | 符号 / Symbol | 说明 / Description |
| :--- | :--- | :--- |
| Platform | `PLATFORM_ARM_CM4F` 等 / etc. | 架构提示（与工具链配合）/ architecture hint (paired with the toolchain) |
| Multi-core | `CPU_CORES` / `AMP_MODE` | 1=单核；2=AMP / 1=single core; 2=AMP |
| OSAL | `OSAL_NULL` / `FREERTOS` / `RTTHREAD` | 运行时后端：裸机协作 / FreeRTOS v11.3.0 / RT-Thread v5.3.0 / runtime backend: bare-metal cooperative / FreeRTOS v11.3.0 / RT-Thread v5.3.0 |
| OSAL 容量 / OSAL Capacity | `OSAL_NULL_MAX_QUEUES`（基础队列数，EventBus 开自动 +1）/ `OSAL_NULL_QUEUE_BUF_SZ` / `FREERTOS_HEAP_SIZE` / `RTT_HEAP_SIZE` | 队列/堆内存（仅对应后端可见）/ queue & heap RAM (backend-scoped) |
| System | `SYSTEM` / `SYSTEM_CPP` / `SYSTEM_C` | 总开关（默认自开）+ 语言后端 / master switch (default on) + language backend |
| Log | `SYS_LOG_USE_PRINTF` / `OSAL` | `SYS_LOG*` 后端 / backend |
| Board Features | `SYSTEM_WDT` / `SYSTEM_SCRUBBER` 等 / etc. | 框架看门狗（默认开）/ CRC 巡检（默认关），依赖 `SYSTEM` / framework watchdog (on) / CRC scrubber (off), depends on `SYSTEM` |
| Runtime | `EVENT_BUS` / `EVENT_BUS_*` / `OSAL_MUTEX_POOL_SIZE` / `BOTTOM_HALF_QUEUE_DEPTH` | 总开关 + 容量 / master switch + capacity |

`SYSTEM` 为**默认自开启**的可选模块，`EVENT_BUS` 与 `SYSTEM_CMD` 为**默认关闭**：关闭 `SYSTEM` 后 `system_c/`、`system_cpp/` 与 EventBus 一并裁剪；仅开启 `EVENT_BUS` 则保留两阶段启动与看门狗，加上发布/订阅总线。
`SYSTEM` is an optional module **enabled by default**; `EVENT_BUS` and `SYSTEM_CMD` are **off by default**: turning off `SYSTEM` trims `system_c/`, `system_cpp/` and EventBus together; turning on `EVENT_BUS` adds the pub/sub bus while keeping the two-phase boot and watchdogs.

仓库自带 `.config` 常见默认：`OSAL_NULL` + `SYSTEM`/`SYSTEM_CPP` + `SYSTEM_WDT` + `SYS_LOG_USE_PRINTF`（`EVENT_BUS` / `SYSTEM_CMD` / `SYSTEM_SCRUBBER` 默认关）。
The repository's bundled `.config` uses common defaults: `OSAL_NULL` + `SYSTEM`/`SYSTEM_CPP` + `SYSTEM_WDT` + `SYS_LOG_USE_PRINTF` (`EVENT_BUS` / `SYSTEM_CMD` / `SYSTEM_SCRUBBER` off).

---

## 4. CMake 集成 / 4. CMake Integration

```cmake
# 平台工程 CMakeLists.txt（示意 / example platform project CMakeLists.txt）
add_subdirectory(third_party/mini_tree)

add_executable(my_fw
    Core/Src/main.c
    platform/hal_gpio_stm32.c          # 例：强符号覆盖 / e.g. strong-symbol override
    platform/hal_uart_stm32.c
    # …
)

target_link_libraries(my_fw PRIVATE mini_tree)

# 覆盖设备树（必须 / required）
set(BOARD_DTS      "${CMAKE_SOURCE_DIR}/board/dts/my_board.dts" CACHE FILEPATH "" FORCE)
set(BOARD_DTSI_DIR "${CMAKE_SOURCE_DIR}/board/dtsi" CACHE PATH "" FORCE)

# 供 dtsi #include 厂商头展开宏（按需 / for macro expansion of vendor headers #included by dtsi, as needed）
set(VENDOR_INC_DIRS "${CUBE_INC};${HAL_INC}" CACHE STRING "" FORCE)
```

### 4.1 平台常设 CACHE / 变量 / Platform CACHE Variables / Options

| 变量 / Variable | 类型 / Type | 作用 / Purpose |
| :--- | :--- | :--- |
| `BOARD_DTS` | `FILEPATH` | 板级入口 `.dts`（**必须**覆盖默认占位）/ board-level entry `.dts` (**must** override the default placeholder) |
| `BOARD_DTSI_DIR` | `PATH` | dtsi 搜索目录 / dtsi search directory |
| `VENDOR_INC_DIRS` | `STRING` | 厂商头 `-I`，供 dtc/cpp 展开宏 / vendor-header `-I` for dtc/cpp macro expansion |
| `VENDOR_DEFINES` | `STRING` | 额外 `-D`（少用）/ extra `-D` (rarely used) |
| ETL（`cmake/etl.cmake`） | — | **vendor 于 `lib/etl`**（仅 include + cmake）；根 CMake 始终 link（缺失时 Fetch 兜底）/ **vendored in `lib/etl`** (include + cmake only); always linked by the root CMake (Fetch fallback if missing) |
| 其它开源积木 / Other open-source bricks | — | TinyUSB / lwIP / cJSON 与其余（LVGL、u8g2、littlefs、FatFs、SFUD、Mbed TLS、coreMQTT、coreHTTP、nanopb、miniz、MCUBoot、FreeModbus、libmodbus、CMSIS-DSP、MultiButton、EasyFlash、EasyLogger、FlashDB）均为链接期 FetchContent，由 `mini_tree_link_*` 点亮，首次联网 Fetch，见 [ecosystem.md](ecosystem.md) / TinyUSB / lwIP / cJSON and the rest (LVGL, u8g2, littlefs, FatFs, SFUD, Mbed TLS, coreMQTT, coreHTTP, nanopb, miniz, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB) all use link-time FetchContent, enabled via `mini_tree_link_*`, fetching over the network on first use, see [ecosystem.md](ecosystem.md) |
| `mini_tree_add_rust_crate` | — | 可选 / optional；见 `cmake/rust.cmake` |
| `CONFIG_BUILD_DISASM` | Kconfig | 启用后可对目标加反汇编 post-build（`cmake/disasm.cmake`）/ adds a disassembly post-build step when enabled (`cmake/disasm.cmake`) |

在 `add_subdirectory(mini_tree)` **之前** `set(... CACHE ... FORCE)` 最稳妥，避免首次配置锁死默认占位 DTS。
Set `... CACHE ... FORCE` **before** `add_subdirectory(mini_tree)` to avoid locking in the default placeholder DTS on the first configure.

`mini_tree` 目标会 / The `mini_tree` target will:

1. 跑 `genconfig.py` / Run `genconfig.py`
2. 跑 `dtc-lite`（扫描 vfs/bus/drivers 中的 `DRIVER_REGISTER`，生成编译期 probe 表）/ Run dtc-lite (scan `DRIVER_REGISTER` in vfs/bus/drivers and generate the compile-time probe table)
3. 按 `.config` 挑选 OSAL / SYSTEM 源；链入 `lib/` 中的 vendor 内核（FreeRTOS v11.3.0 / RT-Thread v5.3.0）/ Pick OSAL / SYSTEM sources per `.config`; link the vendored kernels in `lib/` (FreeRTOS v11.3.0 / RT-Thread v5.3.0)
4. 全部可选积木（含 TinyUSB / lwIP / cJSON）由产品侧 `mini_tree_link_*` 链接期点亮（首次可能联网 Fetch）/ All optional bricks (incl. TinyUSB / lwIP / cJSON) are enabled at link time by the product side via `mini_tree_link_*` (may fetch over the network on first use)

语言后端对照见 [runtime_services.md](runtime_services.md#3-system_c-vs-system_cpp)；USB 板级契约见 [usb_tusb_port.md](usb_tusb_port.md)；积木清单见 [ecosystem.md](ecosystem.md)。
Language-backend comparison: [runtime_services.md](runtime_services.md#3-system_c-vs-system_cpp); USB board-level contract: [usb_tusb_port.md](usb_tusb_port.md); brick list: [ecosystem.md](ecosystem.md).

### 4.2 ESP-IDF？/ What about ESP-IDF?

**不要**对本仓根目录直接 `add_subdirectory` 进 IDF。
**Do not** `add_subdirectory` this repository's root directly into IDF.
ESP 使用 `EXTRA_COMPONENT_DIRS` + `idf_component_register`（组件路径由 `ESP_PLATFORM` 触发），见 **[esp_idf_cmake.md](esp_idf_cmake.md)**（对照平台仓 `platform/Espressif/esp32s3`）。
ESP uses `EXTRA_COMPONENT_DIRS` + `idf_component_register` (the component path is triggered by `ESP_PLATFORM`); see **[esp_idf_cmake.md](esp_idf_cmake.md)** (against the `platform/Espressif/esp32s3` platform repo).

---

## 5. 板级 DTS 覆盖 / 5. Board DTS Override

中间件默认 `board/dts/board.dts` **只有空根节点**，不能驱动真实外设。
The middleware's default `board/dts/board.dts` has **only an empty root node** and cannot drive real peripherals.

平台至少提供 / The platform must at least provide:

- 入口 `.dts`（model/compatible、chosen、status）/ an entry `.dts` (model/compatible, chosen, status)
- SoC / 外设 `.dtsi` / SoC / peripheral `.dtsi`
- 需要时在节点里写厂商宏（经 `VENDOR_INC_DIRS` cpp）/ vendor macros in nodes when needed (cpp-expanded via `VENDOR_INC_DIRS`)

细节与 compatible 列表见 [driver_guide.md](driver_guide.md)。
Details and the compatible list are in [driver_guide.md](driver_guide.md).

---

## 6. 点火时序 / 6. Ignition Sequence

### 6.1 C（`system_init.h`）/ 6.1 C (`system_init.h`)

```c
#include "system_init.h"
#include "config.h"

int main(void)
{
    /* 平台：时钟、堆、控制台 … / platform: clocks, heap, console … */

    mini_tree_pre_os_init();
    /* 可选：业务服务静态 init / optional: static init of business services */

    mini_tree_start_tasks();   /* probe + 框架任务 / probe + framework tasks */
    /* 可选：osal_task_create 业务任务 / optional: create business tasks via osal_task_create */

    system_init_complete();

#if defined(CONFIG_OSAL_NULL)
    for (;;)
        mini_tree_system_loop();
#elif defined(CONFIG_OSAL_FREERTOS)
    vTaskStartScheduler();
#elif defined(CONFIG_OSAL_RTTHREAD)
    rt_system_scheduler_start();
#endif
    return 0;
}
```

### 6.2 C++（`system_init.hpp`）/ 6.2 C++ (`system_init.hpp`)

```cpp
#include "system_init.hpp"

mini_tree::system_pre_os_init();
/* 可选：业务服务静态 init（SystemCmd::get_instance().register_cmd(…) 等）*/
mini_tree::system_start_tasks();   /* probe + 框架任务 */
/* 可选：osal_task_create 业务任务 */

system_init_complete();
// 再启动调度器（vTaskStartScheduler / rt_system_scheduler_start / mini_tree_system_loop）
```

> 裸机（`CONFIG_OSAL_NULL`）下 `osal_task_create` **C 版恒返回 `OSAL_ERR_NOTSUPP`**：
> C++ 工程请用 `osal_null.h` 的 C++ 重载 `osal_task_create`（`CONFIG_OSAL_NULL_TASK_CPP`，默认开启；
> `period` 参数为任务周期 ms，`param1` 为调用方静态分配的 `x_task*` TCB）；
> C 工程直接调 `xscheduler_task_create`（见 `time_slice/task/xtask.h`）。OS 后端无此限制。
> Under bare-metal (`CONFIG_OSAL_NULL`), the C `osal_task_create` **always returns `OSAL_ERR_NOTSUPP`**:
> C++ projects should use the C++ overload in `osal_null.h` (`CONFIG_OSAL_NULL_TASK_CPP`, on by default;
> `period` is the task period in ms, `param1` is a caller-provided static `x_task*` TCB);
> C projects call `xscheduler_task_create` directly (see `time_slice/task/xtask.h`). No such limit on OS backends.

阶段含义见 [architecture.md §3](architecture.md#3-启动时序两段式点火)。
Phase meanings are in [architecture.md §3](architecture.md#3-启动时序两段式点火).

---

## 7. IDE（clangd）/ 7. IDE (clangd)

1. 用编辑器打开 **mini_tree 仓库根**（不要只开子文件夹）。
   Open the **mini_tree repository root** in your editor (not just a subfolder).
2. 确认存在根目录 `compile_flags.txt`，**删除**任何子目录里的 `compile_flags.txt`。
   Make sure the root `compile_flags.txt` exists, and **delete** any `compile_flags.txt` inside subdirectories.
3. ETL 头：已在 `lib/etl`；clangd 用根 `compile_flags.txt`（含 `-Ilib/etl/include`）即可。
   ETL headers: already under `lib/etl`; clangd can use the root `compile_flags.txt` (which contains `-Ilib/etl/include`).
4. 需要 `compile_commands.json` 时，在 mini_tree 根运行 `python3 tools/gen_compile_db.py` 生成（覆盖 `.c/.cpp` 与 `.h/.hpp` 头文件条目，父项目 configure 也不会覆盖）。
   If you need `compile_commands.json`, generate it by running `python3 tools/gen_compile_db.py` at the mini_tree root (covers `.c/.cpp` sources and `.h/.hpp` header entries; a parent-project configure will not clobber it).
5. 命令面板：`Clangd: Restart language server`。
   Command palette: `Clangd: Restart language server`.

无真机构建时，靠 `ide/stubs/` 里的 `config.h`、`board_nodes.h` 等占位头消除红线。积木策略见 [ecosystem.md](ecosystem.md)。
Without a real build, placeholder headers in `ide/stubs/` (e.g. `config.h`, `board_nodes.h`) remove the red squiggles. Brick strategy: [ecosystem.md](ecosystem.md).

---

## 8. 验收清单 / 8. Acceptance Checklist

- [ ] `config.h` 生成且 OSAL/SYSTEM 宏符合预期 / `config.h` is generated and OSAL/SYSTEM macros match expectations
- [ ] dtc-lite 产出 `board_nodes.h`，`DEV_ID_COUNT` ≥ 1（真实板应远大于占位）/ dtc-lite produces `board_nodes.h` with `DEV_ID_COUNT` ≥ 1 (a real board should be far larger than the placeholder)
- [ ] 链接后 GPIO/UART 等 HAL 为平台实现（非一直 `VFS_ERR_NOTSUPP`）/ after linking, HALs like GPIO/UART are platform implementations (not always `VFS_ERR_NOTSUPP`)
- [ ] `board_driver_probe_all` 无意外 FATAL / `board_driver_probe_all` finishes without unexpected FATAL
- [ ] 开中断后业务任务或裸机 loop 稳定跑 / business tasks or the bare-metal loop run stably once interrupts are enabled

---

## 相关文档 / Related Documents

- [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md)
- [osal_switching.md](osal_switching.md) · [faq.md](faq.md) · [ecosystem.md](ecosystem.md)
- [tools/README.md](../tools/README.md)
