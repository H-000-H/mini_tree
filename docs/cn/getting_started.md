# 快速开始

> 从零把 `mini_tree` 配进你的平台工程：依赖 → 配置 → CMake → 点火。
>
> **参考模板工程**：[Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore)——mini_tree 的配套平台示例，含完整移植（DTS、HAL、`board_port.cmake`、AMP）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台/应用工程师 |
| **前置** | CMake、基本 C；有一块目标板或至少能链出固件 |
| **相关** | [device_tree_porting.md](device_tree_porting.md) · [usage.md](usage.md) · [ecosystem.md](ecosystem.md) · [tools_guide.md](../tools_guide.md) |

---

## 目录

1. [依赖](#1-依赖)
2. [获取与目录](#2-获取与目录)
3. [配置系统（Kconfig）](#3-配置系统kconfig)
4. [CMake 集成](#4-cmake-集成)
5. [板级 DTS 覆盖](#5-板级-dts-覆盖)
6. [点火时序](#6-点火时序)
7. [IDE（clangd）](#7-ideclangd)
8. [验收清单](#8-验收清单)

---

## 1. 依赖

| 依赖 | 用途 | 备注 |
| :--- | :--- | :--- |
| CMake ≥ 3.16 | 构建静态库 | Ninja / Make 均可 |
| Python 3 | genconfig、dtc-lite、gen_compile_db | — |
| `lark` | dtc-lite 解析 | `pip install lark` |
| 内置 kconfiglib（`tools/_vendor/`） | menuconfig / guiconfig | 仓库自带，**无需安装**；两种界面见 [tools_guide.md](../tools_guide.md) |
| clang-format ≥ 15 / clang-tidy（可选） | 代码风格与命名检查 | 见 [coding_style.md](coding_style.md) |
| 平台工具链 + SDK | 真机 | **只**链在平台工程，不进中间件公共头 |

---

## 2. 获取与目录

将本仓库作为子目录或 submodule，例如 `third_party/mini_tree`。你需要经常碰的路径：

| 路径 | 用途 |
| :--- | :--- |
| `CMakeLists.txt` | `add_subdirectory` 入口 |
| `.config` / `Kconfig` | 功能裁剪 |
| `board/dts/board.dts` | 默认占位（必被平台覆盖） |
| `board/dtsi/` | 节点模板库：`example-soc.dtsi` + `vfs/`（11）+ `drivers/`（39），参数全 0 占位，板级拷走填值（见 [driver_guide.md](driver_guide.md) §1） |
| `ide/stubs/` | 无生成物时的 IDE 头 |

---

## 3. 配置系统（Kconfig）

### 3.0 文件结构

Kconfig 入口按构建后端分两套，共用同一份公共配置树 `Kconfig.mini_tree`，避免分叉：

| 文件 | 路径 | 作用 | 谁用 |
| :--- | :--- | :--- | :--- |
| `Kconfig.mini_tree` | 仓库根 | 公共配置树（`menu "mini_tree Configuration" ... endmenu`），不含 `mainmenu` | 两套入口各自 `source` |
| `Kconfig.non_esp` | 仓库根 | 非 ESP 入口：`mainmenu` + `source "Kconfig.mini_tree"`（改名避免被 IDF 组件扫描自动收录，造成与 `Kconfig.projbuild` 双重 source） | `tools/genconfig.py` / `menuconfig.py` / 非 ESP `CMakeLists.txt` |
| `Kconfig.projbuild` | 仓库根 | ESP-IDF 入口：`orsource "Kconfig.mini_tree"`（相对本文件目录），被 IDF confgen 注入顶层 Kconfig 树 | ESP-IDF（`idf.py menuconfig` / `idf.py reconfigure`） |

ESP 路径下，`idf.py menuconfig` 即可在顶层菜单看到 "mini_tree Configuration" 子菜单，所有 `OS_*` / `SYSTEM_*` / `EVENT_BUS` 等开关经 IDF 的 `depends on` / `default` / `range` 正确求值后写入 `sdkconfig.h`，无需再手编 `.config`。

### 3.1 生成 `config.h`

```bash
cd path/to/mini_tree
python3 tools/genconfig.py Kconfig build/generated/kconfig/mini_tree --config .config
```

根 `CMakeLists.txt` 在配置阶段也会调用同等逻辑（ESP 路径不调用 genconfig，改由 IDF 的 `sdkconfig.h` 注入 `CONFIG_*`）。

### 3.2 常用选项

| 菜单 | 符号 | 说明 |
| :--- | :--- | :--- |
| Platform | `PLATFORM_ARM_CM4F` 等 | 架构提示（与工具链配合） |
| Multi-core | `CPU_CORES` | 1=单核；2=AMP（互斥锁自动走原子 CAS，无独立开关） |
| OS 后端 | `OS_BARE` / `OS_MINI_OS` / `OS_FREERTOS` / `OS_RTTHREAD` | 运行时后端：裸机 / mini-os（自研，仅 Cortex-M）/ FreeRTOS v11.3.0 / RT-Thread v5.3.0 |
| 后端容量 | `OS_BARE_MAX_QUEUES`（基础队列数，EventBus 开自动 +1）/ `OS_BARE_QUEUE_BUF_SZ` / `FREERTOS_HEAP_SIZE` / `RTT_HEAP_SIZE` | 队列/堆内存（仅对应后端可见） |
| System | `SYSTEM` | 总开关（默认自开）；系统层为纯 C（`system_c/`） |
| Log | `SYS_LOG_USE_MINI_LOG` / `SYS_LOG_USE_ESP` | `MT_LOG_*` 后端（随仓库 mini-log / ESP-IDF esp_log） |
| Board Features | `SYSTEM_WDT` / `SYSTEM_SCRUBBER` 等 | 框架看门狗（默认开）/ CRC 巡检（默认关）；后者依赖 `SYSTEM` + `MINI_OTA` |
| Runtime | `EVENT_BUS` / `EVENT_BUS_*` / `MINI_MUTEX_POOL_SIZE` / `BOTTOM_HALF_QUEUE_DEPTH` | 总开关 + 容量 |

`SYSTEM` 为**默认自开启**的可选模块，`EVENT_BUS` 与 `SYSTEM_CMD` 为**默认关闭**：关闭 `SYSTEM` 后 `system_c/` 与 EventBus 一并裁剪（命令模块 `system_cmd` 随 `SYSTEM_CMD` 关闭）；仅开启 `EVENT_BUS` 则保留两阶段启动与看门狗，加上发布/订阅总线。

仓库自带 `.config` 常见默认：`OS_BARE` + `SYSTEM` + `SYSTEM_WDT` + `SYS_LOG_USE_MINI_LOG`（`EVENT_BUS` / `SYSTEM_CMD` / `SYSTEM_SCRUBBER` 默认关）。

---

## 4. CMake 集成

```cmake
# 平台工程 CMakeLists.txt（示意）
add_subdirectory(third_party/mini_tree)

add_executable(my_fw
    Core/Src/main.c
    platform/hal_gpio_stm32.c          # 例：强符号覆盖
    platform/hal_uart_stm32.c
    # …
)

target_link_libraries(my_fw PRIVATE mini_tree)

# 覆盖设备树（必须）
set(BOARD_DTS      "${CMAKE_SOURCE_DIR}/board/dts/my_board.dts" CACHE FILEPATH "" FORCE)
set(BOARD_DTSI_DIR "${CMAKE_SOURCE_DIR}/board/dtsi" CACHE PATH "" FORCE)

# 供 dtsi #include 厂商头展开宏（按需）
set(VENDOR_INC_DIRS "${CUBE_INC};${HAL_INC}" CACHE STRING "" FORCE)
```

### 4.1 平台常设 CACHE / 变量

| 变量 | 类型 | 作用 |
| :--- | :--- | :--- |
| `BOARD_DTS` | `FILEPATH` | 板级入口 `.dts`（**必须**覆盖默认占位） |
| `BOARD_DTSI_DIR` | `PATH` | dtsi 搜索目录 |
| `VENDOR_INC_DIRS` | `STRING` | 厂商头 `-I`，供 dtc/cpp 展开宏 |
| `VENDOR_DEFINES` | `STRING` | 额外 `-D`（少用） |
| ETL（`cmake/etl.cmake`） | — | **vendor 于 `lib/etl`**（仅 include + cmake）；根 CMake 始终 link（缺失时 Fetch 兜底） |
| 其它开源积木 | — | TinyUSB / lwIP 为**配置期** FetchContent（根 CMake 直接 include 对应 `cmake/*.cmake`），其余（LVGL、u8g2、littlefs、FatFs、SFUD、coreMQTT、MCUBoot、MultiButton、EasyFlash、EasyLogger、FlashDB）均为链接期 FetchContent，由 `mini_tree_link_*` 点亮，首次联网 Fetch，见 [ecosystem.md](ecosystem.md) |
| `mini_tree_add_rust_crate` | — | 可选；见 `cmake/rust.cmake` |
| `CONFIG_BUILD_DISASM` | Kconfig | 启用后可对目标加反汇编 post-build（`cmake/disasm.cmake`） |

在 `add_subdirectory(mini_tree)` **之前** `set(... CACHE ... FORCE)` 最稳妥，避免首次配置锁死默认占位 DTS。

`mini_tree` 目标会：

1. 跑 `genconfig.py`
2. 跑 `dtc-lite`（扫描 vfs/bus/drivers 中的 `DRIVER_REGISTER`，生成编译期 probe 表）
3. 按 `.config` 挑选 OS / SYSTEM 源；链入 `lib/` 中的 vendor 内核（mini-os / FreeRTOS v11.3.0 / RT-Thread v5.3.0）
4. 配置期积木（TinyUSB / lwIP）由根 CMake 直接 `include` 对应 `cmake/*.cmake`；其余可选积木由产品侧 `mini_tree_link_*` 链接期点亮（首次可能联网 Fetch）

系统运行时后端见 [runtime_services.md](runtime_services.md#3-系统运行时后端)；USB 板级契约见 [usb_tusb_port.md](usb_tusb_port.md)；积木清单见 [ecosystem.md](ecosystem.md)。

### 4.2 ESP-IDF？

主仓 `main` 保留了完整的 ESP-IDF 构建路径（`cmake/esp_idf.cmake`、`Kconfig.projbuild`、`idf_component.yml`），但**不能**像普通平台一样直接 `add_subdirectory`——需要走 IDF 组件路径（由 `ESP_PLATFORM` 触发 → `cmake/esp_idf.cmake`）。

完整参考板工程和移植指南在 **`esp` 分支**（`espidf-branch`）维护：

```bash
git clone -b espidf-branch https://github.com/H-000-H/mini_tree.git
```

也可通过乐鑫组件注册表拉取：在 `idf_component.yml` 里添加 `h-000-h/mini_tree: ">=1.2.0"`。

---

## 5. 板级 DTS 覆盖

中间件默认 `board/dts/board.dts` **只有空根节点**，不能驱动真实外设。

平台至少提供：

- 入口 `.dts`（model/compatible、chosen、status）
- SoC / 外设 `.dtsi`
- 需要时在节点里写厂商宏（经 `VENDOR_INC_DIRS` cpp）

细节与 compatible 列表见 [driver_guide.md](driver_guide.md)。

---

## 6. 点火时序

### 6.1 C（`system_init.h`）

```c
#include "system_init.h"
#include "config.h"

int main(void)
{
    /* 平台：时钟、堆、控制台 … */

    mini_tree_pre_os_init();
    /* 可选：业务服务静态 init */

    mini_tree_start_tasks();   /* probe + 框架任务 */
    /* 可选：mini_task_create 业务任务 */

    system_init_complete();

#if defined(CONFIG_OS_BARE)
    for (;;)
        mini_tree_system_loop();
#elif defined(CONFIG_OS_MINI_OS)
    mini_os_schedule_start();
#elif defined(CONFIG_OS_FREERTOS)
    vTaskStartScheduler();
#elif defined(CONFIG_OS_RTTHREAD)
    rt_system_scheduler_start();
#endif
    return 0;
}
```

> 系统层为纯 C；上述点火函数均为 `extern "C"`，C++ 工程也直接调用同一套 C API（命令基础设施 `SystemCmd` 除外，仍为 C++）。

> 裸机（`CONFIG_OS_BARE`）下 `mini_task_create` **C 版恒返回 `MINI_ERR_NOTSUPP`**：
> C++ 工程请用 `mini_backend.h` 的 C++ 重载 `mini_task_create`（`CONFIG_XTASK_PREEMPT`，默认开启；
> `period` 参数为任务周期 ms，`param1` 为调用方静态分配的 `x_task*` TCB）；
> C 工程直接调 `xscheduler_task_create`（见 `time_slice/task/xtask.h`）。OS 后端无此限制。
>
> **抢占式 (`CONFIG_XTASK_PREEMPT=y`) 注意**: C++ 重载仍提供, 但 `mini_task_create` 切换为带 `priority` 的分支（`stack_size` 在裸机下复用为周期）; 也可走 `xscheduler_task_create` 原生 API. 调度器实现换成 `xtask_preempt.c` (N+1 多优先级, 已完整实现可编译).

阶段含义见 [architecture.md §3](architecture.md#3-启动时序两段式点火)。

---

## 7. IDE（clangd）

1. 用编辑器打开 **mini_tree 仓库根**（不要只开子文件夹）。
2. 确认存在根目录 `compile_flags.txt`，**删除**任何子目录里的 `compile_flags.txt`。
3. ETL 头：已在 `lib/etl`；clangd 用根 `compile_flags.txt`（含 `-Ilib/etl/include`）即可。
4. 需要 `compile_commands.json` 时，在 mini_tree 根运行 `python3 tools/gen_compile_db.py` 生成（覆盖 `.c/.cpp` 与 `.h/.hpp` 头文件条目，父项目 configure 也不会覆盖）。
5. 命令面板：`Clangd: Restart language server`。

无真机构建时，靠 `ide/stubs/` 里的 `config.h`、`board_nodes.h` 等占位头消除红线。积木策略见 [ecosystem.md](ecosystem.md)。

### 7.1 操作系统选择（Linux / Windows）

本仓是基于 **CMake + clangd** 的跨系统架构，**编译器层面 Windows 与 Linux 没有区别**（同一套 ARM GCC / Clang、同一份 CMake 流程），因此两边都能正常开发：

| 维度 | 说明 |
| :--- | :--- |
| **Linux（推荐）** | 如果你熟悉这套工具链，**推荐直接去 Linux 里写 MCU**：CMake / Python 脚本 / 生成头流程在 Linux 上更快、更顺、权限与路径管理也更干净，日常构建与依赖管理更省心；同时也能**为你进入 Linux 开发环境做提前准备**（服务端、CI、交叉编译大多在 Linux）。 |
| **Windows（同样支持）** | Windows 这边同样完全可用——编译器无区别、工程结构一致，clangd / Keil Studio（Keil 6）在 Windows 上也能跑通本仓 CMake 流程；只是脚本与权限细节不如 Linux 顺手。 |

> 结论：**优先 Linux，不排斥 Windows**。两边产物一致，按需选顺手的即可；团队里想练 Linux 的人可直接切过去，不影响交付。

---

## 8. 验收清单

- [ ] `config.h` 生成且 OS/SYSTEM 宏符合预期
- [ ] dtc-lite 产出 `board_nodes.h`，`DEV_ID_COUNT` ≥ 1（真实板应远大于占位）
- [ ] 链接后 GPIO/UART 等 HAL 为平台实现（非一直 `MINI_ERR_NOTSUPP`）
- [ ] `board_driver_probe_all` 无意外 FATAL
- [ ] 开中断后业务任务或裸机 loop 稳定跑

---

## 相关文档

- [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md)
- [backend_switching.md](backend_switching.md) · [faq.md](faq.md) · [ecosystem.md](ecosystem.md)
- [tools_guide.md](../tools_guide.md)
