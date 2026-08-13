# 快速开始

> 从零把 `mini_tree`（ESP-IDF 专用分支）接入你的 **ESP-IDF** 工程：依赖 → 配置 → CMake → 点火。
>
> 本分支为纯 ESP-IDF 组件：Kconfig 走 `idf.py menuconfig`，`CONFIG_*` 写入 `sdkconfig.h`，第三方依赖走 IDF Component Manager / registry。非 ESP（裸机 / 其他 RTOS）支持见 mini_tree 主仓库。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台/应用工程师（ESP-IDF 项目） |
| **前置** | ESP-IDF 环境、基本 C；有一块 ESP32 目标板 |
| **相关** | [esp_idf_cmake.md](esp_idf_cmake.md) · [device_tree_porting.md](device_tree_porting.md) · [usage.md](usage.md) · [ecosystem.md](ecosystem.md) |

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
| ESP-IDF（≥ 5.0） | 构建与 Kconfig | `idf.py` / `idf_component_manager` |
| Python 3 | dtc-lite | — |
| `lark` | dtc-lite 解析 | `pip install lark` |
| clang-format ≥ 15 / clang-tidy（可选） | 代码风格与命名检查 | 见 [coding_style.md](coding_style.md) |

Kconfig 库（kconfiglib）**由 ESP-IDF 自带**，本分支不再 vendor，`tools/_vendor/` 已移除。

---

## 2. 获取与目录

将本分支作为 `components/mini_tree` 放入你的 ESP-IDF 工程（submodule / symlink / 拷贝均可）。你可以用 IDF Component Manager 直接声明依赖：

```yaml
# idf_component.yml
dependencies:
  h-000-h/mini_tree: "1.2.0"
```

或手动放置后 `idf.py reconfigure`。你需要经常碰的路径：

| 路径 | 用途 |
| :--- | :--- |
| `cmake/esp_idf.cmake` | ESP 组件入口（`idf_component_register`） |
| `Kconfig.projbuild` / `Kconfig.mini_tree` | ESP Kconfig（经 `idf.py menuconfig`） |
| `board/dts/board.dts` | 默认占位（必被板级覆盖） |
| `board/dtsi/` | 节点模板库：`example-soc.dtsi` + `vfs/`（11）+ `drivers/`（37），参数全 0 占位，板级拷走填值（见 [driver_guide.md](driver_guide.md) §1） |
| `ide/stubs/` | 无生成物时的 IDE 头 |

---

## 3. 配置系统（Kconfig）

### 3.0 入口

本分支配置**完全走 ESP-IDF Kconfig**，不再有独立的非 ESP 入口：

| 文件 | 路径 | 作用 |
| :--- | :--- | :--- |
| `Kconfig.mini_tree` | 仓库根 | 公共配置树（`menu "mini_tree Configuration" ... endmenu`），不含 `mainmenu` |
| `Kconfig.projbuild` | 仓库根 | ESP-IDF 入口：`orsource "Kconfig.mini_tree"`，被 IDF confgen 注入顶层 Kconfig 树 |

`idf.py menuconfig` 即可在顶层菜单看到 "mini_tree Configuration" 子菜单，所有 `OSAL_*` / `SYSTEM_*` / `EVENT_BUS` 等开关经 IDF 的 `depends on` / `default` / `range` 正确求值后写入 `sdkconfig.h`。无需手编 `.config`，也无需 `tools/genconfig.py`（已移除）。

### 3.1 生成 `config.h`

ESP 路径下 `config.h` 仅是 `sdkconfig.h` 的转发头（由 `cmake/esp_idf.cmake` 生成），真值全部来自 `sdkconfig.h`。不需要手动运行任何 genconfig。

### 3.2 常用选项

| 菜单 | 符号 | 说明 |
| :--- | :--- | :--- |
| Platform | `PLATFORM_ESP32` | 平台身份声明（默认开启，本分支仅 ESP） |
| Multi-core | `CPU_CORES` / `AMP_MODE` | 1=单核；2=AMP |
| OSAL | `OSAL_FREERTOS`（默认） / `OSAL_NULL` | 运行时后端：IDF 内置 FreeRTOS / 裸机后备 |
| OSAL 容量 | `OSAL_NULL_MAX_QUEUES`（基础队列数，EventBus 开自动 +1）/ `OSAL_NULL_QUEUE_BUF_SZ` | 队列内存（仅 `OSAL_NULL` 可见） |
| System | `SYSTEM` / `SYSTEM_CPP` / `SYSTEM_C` | 总开关（默认自开）+ 语言后端 |
| Log | `SYS_LOG_USE_PRINTF` / `SYS_LOG_USE_ESP` | `SYS_LOG*` 后端（ESP 推荐 `SYS_LOG_USE_ESP`） |
| Board Features | `SYSTEM_WDT`（默认开）/ `SYSTEM_SCRUBBER`（默认关）等 | 框架看门狗 / CRC 巡检 |
| Runtime | `EVENT_BUS`（默认开）/ `SYSTEM_CMD`（默认开）/ `OSAL_MUTEX_POOL_SIZE` / `BOTTOM_HALF_QUEUE_DEPTH` | 总开关 + 容量 |

`SYSTEM` 为**默认自开启**的可选模块；`EVENT_BUS`、`SYSTEM_CMD`、`PRODUCTION_LOG` 现为**默认开启**（纯软件功能）。ESP 路径下 FreeRTOS 定时器/堆由 IDF 自身管理（`CONFIG_FREERTOS_TIMERS` / IDF 堆），不归本 shelf。

---

## 4. CMake 集成

将本分支放在 ESP-IDF 工程的 `components/mini_tree` 下，IDF 在 `ESP_PLATFORM` 时自动 `include(cmake/esp_idf.cmake)` 并 `idf_component_register`。板级通过 `components/board_port.cmake`（或 `MINI_TREE_BOARD_PORT`）注入：

```cmake
# components/board_port.cmake —— 板级注入契约（详见 esp_idf_cmake.md §3）
get_filename_component(_BOARD_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

set(BOARD_DTS      "${_BOARD_ROOT}/board_<soc>/dts/board.dts")   # 板级设备树
set(BOARD_DTSI_DIR "${_BOARD_ROOT}/board_<soc>/dtsi")            # dtsi 片段目录

# 芯片专属 dtc 参数（-I 与目标宏）
list(APPEND MINI_TREE_DTC_EXTRA_ARGS "-DCONFIG_IDF_TARGET_<CHIP>=1")
```

HAL 强实现放在独立组件 `hal_<soc>`（`WHOLE_ARCHIVE`），见 [esp_idf_cmake.md §7](esp_idf_cmake.md#7-halesp-全屏蔽--板级-strong-实现)。

**不要**对本分支根目录直接 `add_subdirectory` 进 IDF —— ESP 路径走组件化，具体见 **[esp_idf_cmake.md](esp_idf_cmake.md)**。

---

## 5. 板级 DTS 覆盖

中间件默认 `board/dts/board.dts` **只有空根节点**，不能驱动真实外设。

平台至少提供：

- 入口 `.dts`（model/compatible、chosen、status）
- SoC / 外设 `.dtsi`
- 需要时在节点里写厂商宏（经 `MINI_TREE_DTC_EXTRA_ARGS` 的芯片 `-I` cpp）

细节与 compatible 列表见 [driver_guide.md](driver_guide.md) 与 [esp_idf_cmake.md §6](esp_idf_cmake.md#6-dts-与生成物)。

---

## 6. 点火时序

### 6.1 C（`system_init.h`）

```c
#include "system_init.h"
#include "config.h"

void app_main(void)
{
    mini_tree_pre_os_init();
    /* 可选：业务服务静态 init */

    mini_tree_start_tasks();   /* probe + 框架任务 */
    /* 可选：osal_task_create 业务任务 */

    system_init_complete();

    /* ESP-IDF 已运行 FreeRTOS 调度器；若选 OSAL_NULL 则自行写循环 */
#if defined(CONFIG_OSAL_FREERTOS)
    /* 框架任务已并入 IDF 调度的 FreeRTOS，无需额外启动 */
#endif
}
```

### 6.2 C++（`system_init.hpp`）

```cpp
#include "config.h"            // CONFIG_OSAL_* 等宏为相关头所需
#include "system_init.hpp"

mini_tree::system_pre_os_init();
/* 可选：业务服务静态 init（SystemCmd::get_instance().register_cmd(…) 等）*/
mini_tree::system_start_tasks();   /* probe + 框架任务 */
/* 可选：osal_task_create 业务任务 */

system_init_complete();
// ESP-IDF 已运行 FreeRTOS 调度器，无需额外启动
```

> 裸机（`CONFIG_OSAL_NULL`）下 `osal_task_create` **C 版恒返回 `OSAL_ERR_NOTSUPP`**：
> C++ 工程请用 `osal_null.h` 的 C++ 重载 `osal_task_create`（`CONFIG_OSAL_NULL_TASK_CPP`，默认开启）；
> C 工程直接调 `xscheduler_task_create`（见 `time_slice/task/xtask.h`）。FreeRTOS 后端无此限制。

阶段含义见 [architecture.md §3](architecture.md#3-启动时序两段式点火)。

---

## 7. IDE（clangd）

1. 用编辑器打开 **mini_tree 仓库根**（不要只开子文件夹）。
2. 确认存在根目录 `compile_flags.txt`，**删除**任何子目录里的 `compile_flags.txt`。
3. ETL 头：已在 `lib/etl`；clangd 用根 `compile_flags.txt`（含 `-Ilib/etl/include`）即可。
4. 在 ESP-IDF 工程里可 `idf.py build` 后由 `build/compile_commands.json` 提供索引。
5. 命令面板：`Clangd: Restart language server`。

无真机构建时，靠 `ide/stubs/` 里的 `config.h`、`board_nodes.h` 等占位头消除红线。

---

## 8. 验收清单

- [ ] `idf.py build` 通过，生成 `board_probe.c` / `dt_config_gen.h`
- [ ] `sdkconfig.h` 中 OSAL/SYSTEM 宏符合预期（默认 `OSAL_FREERTOS`）
- [ ] dtc-lite 产出 `board_nodes.h`，`DEV_ID_COUNT` ≥ 1（真实板应远大于占位）
- [ ] 板级 `hal_<soc>` 组件带 `WHOLE_ARCHIVE`，实现 ESP 构建引用的全部 `hal_*`
- [ ] 链接后 GPIO/UART 等 HAL 为板级实现（非一直 `VFS_ERR_NOTSUPP`）
- [ ] `board_driver_probe_all` 无意外 FATAL
- [ ] 烧录后业务任务稳定跑

---

## 相关文档

- [esp_idf_cmake.md](esp_idf_cmake.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md)
- [osal_switching.md](osal_switching.md) · [faq.md](faq.md) · [ecosystem.md](ecosystem.md)
