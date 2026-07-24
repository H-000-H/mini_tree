# 快速开始

> 从零把 `mini_tree` 配进你的平台工程：依赖 → 配置 → CMake → 点火。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台/应用工程师 |
| **前置** | CMake、基本 C；有一块目标板或至少能链出固件 |
| **相关** | [porting_guide.md](porting_guide.md) · [usage.md](usage.md) · [ecosystem.md](ecosystem.md) · [tools/README.md](../tools/README.md) |

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
| Python 3 | genconfig、dtc-lite | — |
| `lark` | dtc-lite 解析 | `pip install lark` |
| `kconfiglib`（可选） | menuconfig | 无则手改 `.config` |
| 平台工具链 + SDK | 真机 | **只**链在平台工程，不进中间件公共头 |

---

## 2. 获取与目录

将本仓库作为子目录或 submodule，例如 `third_party/mini_tree`。你需要经常碰的路径：

| 路径 | 用途 |
| :--- | :--- |
| `CMakeLists.txt` | `add_subdirectory` 入口 |
| `.config` / `Kconfig` | 功能裁剪 |
| `board/dts/board.dts` | 默认占位（必被平台覆盖） |
| `ide/stubs/` | 无生成物时的 IDE 头 |

---

## 3. 配置系统（Kconfig）

### 3.1 生成 `config.h`

```bash
cd path/to/mini_tree
python3 tools/genconfig.py Kconfig build/generated/kconfig/mini_tree --config .config
```

根 `CMakeLists.txt` 在配置阶段也会调用同等逻辑。

### 3.2 常用选项

| 菜单 | 符号 | 说明 |
| :--- | :--- | :--- |
| Platform | `PLATFORM_ARM_CM4F` 等 | 架构提示（与工具链配合） |
| Multi-core | `CPU_CORES` / `AMP_MODE` | 1=单核；2=AMP |
| OSAL | `OSAL_NULL` / `FREERTOS` / `RTTHREAD` | 运行时后端 |
| System | `SYSTEM_CPP` / `SYSTEM_C` | 启动与系统模块语言 |
| Log | `SYS_LOG_USE_PRINTF` / `OSAL` | `SYS_LOG*` 后端 |
| Board Features | `ENABLE_WDT` / `ENABLE_FLASH_SCRUBBER` 等 | 安全相关 |
| Runtime | `EVENT_BUS_*` / `OSAL_MUTEX_POOL_SIZE` | 容量 |

仓库自带 `.config` 常见默认：`OSAL_NULL` + `SYSTEM_CPP` + `SYS_LOG_USE_PRINTF`。

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
| ETL（`cmake/etl.cmake`） | — | **默认进库**：源码 `lib/etl`；根 CMake 始终 link（缺失时 Fetch 兜底） |
| 其它开源积木 | — | `mini_tree_link_*`；默认 Fetch，见 [ecosystem.md](ecosystem.md) |
| `mini_tree_add_rust_crate` | — | 可选；见 `cmake/rust.cmake` |
| `CONFIG_BUILD_DISASM` | Kconfig | 启用后可对目标加反汇编 post-build（`cmake/disasm.cmake`） |

在 `add_subdirectory(mini_tree)` **之前** `set(... CACHE ... FORCE)` 最稳妥，避免首次配置锁死默认占位 DTS。

`mini_tree` 目标会：

1. 跑 `genconfig.py`  
2. 跑 `dtc-lite`（扫描 vfs/bus/drivers 中的 `DRIVER_REGISTER`）  
3. 按 `.config` 挑选 OSAL / SYSTEM 源；按 Kconfig 链入 `lib/` 中的内核（及 TinyUSB 等基础设施）  
4. 可选积木由产品侧 `mini_tree_link_*` 点亮（首次可能联网 Fetch）  

语言后端对照见 [runtime_services.md](runtime_services.md#3-system_c-vs-system_cpp)；USB 板级契约见 [usb_tusb_port.md](usb_tusb_port.md)；积木清单见 [ecosystem.md](ecosystem.md)。

### 4.2 ESP-IDF？

**不要**对本仓根目录直接 `add_subdirectory` 进 IDF。  
ESP 使用 `EXTRA_COMPONENT_DIRS` + `idf_component_register`，见 **[esp_idf_cmake.md](esp_idf_cmake.md)**（对照平台仓 `platform/Espressif/esp32s3`）。

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
    /* 可选：osal_task_create 业务任务 */

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

### 6.2 C++（`system_init.hpp`）

```cpp
MiniTree::System_Pre_OS_Init();
MiniTree::System_Start_Tasks();
system_init_complete();
// 再启动调度器
```

阶段含义见 [architecture.md §3](architecture.md#3-启动时序两段式点火)。

---

## 7. IDE（clangd）

1. 用编辑器打开 **mini_tree 仓库根**（不要只开子文件夹）。  
2. 确认存在根目录 `compile_flags.txt`，**删除**任何子目录里的 `compile_flags.txt`。  
3. ETL 头：已在 `lib/etl`；clangd 用根 `compile_flags.txt`（含 `-Ilib/etl/include`）即可。  
4. 命令面板：`Clangd: Restart language server`。  

无真机构建时，靠 `ide/stubs/` 里的 `config.h`、`board_nodes.h` 等占位头消除红线。积木策略见 [ecosystem.md](ecosystem.md)。

---

## 8. 验收清单

- [ ] `config.h` 生成且 OSAL/SYSTEM 宏符合预期  
- [ ] dtc-lite 产出 `board_nodes.h`，`DEV_ID_COUNT` ≥ 1（真实板应远大于占位）  
- [ ] 链接后 GPIO/UART 等 HAL 为平台实现（非一直 `VFS_ERR_NOTSUPP`）  
- [ ] `board_driver_probe_all` 无意外 FATAL  
- [ ] 开中断后业务任务或裸机 loop 稳定跑  

---

## 相关文档

- [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md)  
- [osal_switching.md](osal_switching.md) · [faq.md](faq.md) · [ecosystem.md](ecosystem.md)  
- [tools/README.md](../tools/README.md)
