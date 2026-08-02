# 常见问题 FAQ / Frequently Asked Questions

> 构建、链接、clangd、probe、OSAL 切换中最常踩的坑。
> The most common pitfalls in build, linking, clangd, probe, and OSAL switching.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 所有使用者 / Everyone |
| **相关 / Related** | [getting_started.md](getting_started.md) · [problem_summary.md](problem_summary.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md) |

---

## clangd / IDE

### 全文件报错，`compiler_compat.h` not found / Whole-file errors, `compiler_compat.h` not found

1. 是否打开了 **mini_tree 根目录**？/ Did you open the **mini_tree root directory**?
2. 是否有子目录 `compile_flags.txt` 覆盖了根配置？（应删除子目录那份）/ Is there a subdirectory `compile_flags.txt` overriding the root config? (delete the subdirectory one)
3. 重启 Clangd。/ Restart Clangd.

### `SYS_LOG backend not configured`

`config.h`（或 `ide/stubs/config.h`）需定义 `CONFIG_SYS_LOG_USE_PRINTF` 或其它日志后端。
`config.h` (or `ide/stubs/config.h`) must define `CONFIG_SYS_LOG_USE_PRINTF` or another log backend.

### `device_id_t` / `DEV_ID_COUNT` 未知 / Unknown

缺少 dtc-lite 生成头。IDE 依赖 `ide/stubs/board_nodes.h`；真机构建把 generated 目录加入 `-I`。
The dtc-lite generated headers are missing. The IDE relies on `ide/stubs/board_nodes.h`; for real builds, add the generated directory to `-I`.

### ETL / `etl/string.h` not found

ETL 是 **上层 C++ 基础库**，vendor 于 `lib/etl`（仅 include + cmake），配置时默认链入 `mini_tree`（缺失时由 `cmake/etl.cmake` Fetch 兜底）。
ETL is the **upper-layer C++ base library**, vendored under `lib/etl` (include + cmake only), linked into `mini_tree` by default at configure time (with a Fetch fallback in `cmake/etl.cmake` if missing).

若 IDE 仍报 `etl/string.h` not found：确认 `lib/etl/include` 存在，或刷新 `compile_commands.json` / 根 `compile_flags.txt`（已含 `-Ilib/etl/include`）。详见 [ecosystem.md](ecosystem.md)。
If the IDE still reports `etl/string.h` not found: confirm `lib/etl/include` exists, or refresh `compile_commands.json` / the root `compile_flags.txt` (which already contains `-Ilib/etl/include`). See [ecosystem.md](ecosystem.md).

### 能不能用 Keil 当主 IDE？/ Can Keil be the main IDE?

**不推荐，作者已不支持。** 若客户强制要工程文件，降级路径是：CMake 生成头之后，用 **Python 自动生成 `.uvprojx`**（远古有过类似做法，现不维护）。日常仍应用 Cursor / VS Code / CLion / Qoder。见 [keil_integration.md](keil_integration.md)。
**Not recommended; no longer supported by the author.** If a customer forces a project file, the fallback path is: after CMake generates the headers, use **Python to auto-generate the `.uvprojx`** (a similar approach existed long ago and is no longer maintained). For daily work, keep using Cursor / VS Code / CLion / Qoder. See [keil_integration.md](keil_integration.md).

---

## 构建与链接 / Build & Linking

### HAL 调用总是 `VFS_ERR_NOTSUPP` / HAL calls always return `VFS_ERR_NOTSUPP`

平台强符号未链入，仍在用中间件 weak 空实现。检查目标源文件列表与链接顺序。
The platform strong symbols are not linked in, so the middleware weak empty implementations are still used. Check the target source list and link order.

### `hal_usb_*` poisoned

实现文件须先 `#define HAL_USB_IMPL` 再 `#include "hal_usb.h"`。
The implementation file must `#define HAL_USB_IMPL` before `#include "hal_usb.h"`.

### 调用 `hal_can_*` 等报 poisoned / Calling `hal_can_*` etc. reports poisoned

应走 `can_bus_*`；仅 bus 实现文件定义 `CAN_BUS_IMPL`（或对应宏）。
Use `can_bus_*` instead; only the bus implementation file defines `CAN_BUS_IMPL` (or the corresponding macro).

### `ERR_PTR` / 链接缺 `ERR_SECTION_BASE` / linking fails on `ERR_SECTION_BASE`

合并 `error_symbols.ld` 或平台提供等价 `PROVIDE(ERR_SECTION_BASE=…)`。
Merge `error_symbols.ld` or have the platform provide an equivalent `PROVIDE(ERR_SECTION_BASE=…)`.

---

## 设备树与 Probe / Device Tree & Probe

### `board_driver_probe_all` 失败很多 / Many failures in `board_driver_probe_all`

查 DTS `status`、时钟属性是否展开为 0、compatible 是否与 `DRIVER_REGISTER` 一致、依赖 `deps` 是否先 probe。
Check the DTS `status`, whether clock properties expand to 0, whether `compatible` matches `DRIVER_REGISTER`, and whether dependency `deps` probe first.

### 改了驱动宏但表没变 / Changed a driver macro but the table did not change

清理构建目录，确保 CMake 依赖到了该 `.c`，dtc-lite 重新跑。
Clean the build directory, make sure CMake depends on that `.c`, and let dtc-lite rerun. Also confirm `lark` is installed (`pip install lark`) for dtc-lite.

---

## 运行时 / Runtime

### 裸机无调度 / Bare-metal has no scheduling

`CONFIG_OSAL_NULL` 下用 `mini_tree_system_loop` + 裸机调度器（`x_scheduler` / `x_task`，见 `time_slice/xtask.h`），不要调用 `vTaskStartScheduler`。
Under `CONFIG_OSAL_NULL`, use `mini_tree_system_loop` + the bare-metal scheduler (`x_scheduler` / `x_task`, see `time_slice/xtask.h`); do not call `vTaskStartScheduler`.

### 切 RTOS 后优先级行为相反 / Priority behavior is inverted after switching RTOS

见 [osal_switching.md](osal_switching.md)：FreeRTOS 与 RT-Thread 优先级数值语义相反。
See [osal_switching.md](osal_switching.md): FreeRTOS and RT-Thread have opposite numeric priority semantics.

### 复位后异常、上电正常 / Fails after reset, works after power-on

查全局构造、WDT、外设时钟门控、链接段；清单见 [problem_summary.md](problem_summary.md)。
Check global constructors, WDT, peripheral clock gating, and link sections; the checklist is in [problem_summary.md](problem_summary.md).

---

## 相关文档 / Related Documents

- [debug_monitor.md](debug_monitor.md) · [porting_guide.md](porting_guide.md)
- [usage.md](usage.md) · [ecosystem.md](ecosystem.md)
