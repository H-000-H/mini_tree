# 文件索引 / File Index

> 中间件源码导航（不含 `lib/**` 积木树）。路径相对仓库根。可选积木可能仅在 Fetch 缓存中，见 [ecosystem.md](ecosystem.md)。
> Middleware source navigation (excluding the `lib/**` brick tree). Paths are relative to the repo root. Optional bricks may live only in the Fetch cache — see [ecosystem.md](ecosystem.md).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 找文件 / Code Review — locating files / code review |
| **相关 / Related** | [architecture.md](architecture.md) · [design_decisions.md](design_decisions.md) |

---

## 顶层 / Top Level

| 路径 / Path | 说明 / Description |
| :--- | :--- |
| `CMakeLists.txt` | 静态库 `mini_tree`、genconfig、dtc-lite、源文件集合 / static lib `mini_tree`, genconfig, dtc-lite, source set |
| `Kconfig` / `.config` | 配置菜单与点文件 / config menu and dotfile |
| `compile_flags.txt` / `.clangd` | clangd 编译数据库 / clangd compilation database |
| `.clang-format` · `.clang-format-ignore` · 分层 `.clang-tidy` | 代码风格：格式化 + 命名规范；app 层建议、app 以下强规定 / code style: formatting + naming rules; suggested at app layer, enforced below |
| `error_symbols.ld` | `ERR_SECTION_BASE` |
| `LICENSE` / `NOTICE` | Apache-2.0 全文 / 第三方归属 / Apache-2.0 full text / third-party attribution |
| `.gitignore` | 构建产物与本地 IDE 噪声（对齐平台仓）/ build artifacts & local IDE noise (aligned with platform repos) |
| `README.md` / `CHANGELOG.md` / `CONTRIBUTING.md` | 入口、变更、贡献（开源惯例留根目录）/ entry, changelog, contributing (kept at root per OSS convention) |
| `docs/` | 全部专题文档（见 [README.md](README.md)）/ all topical docs (see README.md) |

> 构建为通用 CMake：HAL 提供 weak 空实现，板级经 `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR` 注入；另提供 ESP-IDF 组件路径（`cmake/esp_idf.cmake`）。
> Build is generic CMake: HAL ships weak empty implementations, and the board is injected via `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR`; an ESP-IDF component path (`cmake/esp_idf.cmake`) is also provided.

---

## board/（设备模型与设备树）/ Device Model & Device Tree

| 路径 / Path | 说明 / Description |
| :--- | :--- |
| `include/device.h` | 设备模型、属性、持锁 VFS 包装 / device model, properties, lock-holding VFS wrappers |
| `include/driver.h` | `DRIVER_REGISTER`、`board_driver_probe_all` |
| `include/bus.h` | 总线控制器抽象 / bus controller abstraction |
| `include/dev_lifecycle.h` | 驱动 I/O 生命周期 / driver I/O lifecycle |
| `include/board_config.h` | 容量宏聚合、`dt_config_gen` / `config.h` / capacity macro aggregation |
| `include/VFS.h` | 兼容包装（转发 status 等）/ compatibility wrapper (forwards status, etc.) |
| `src/board_device.c` | 设备实例与查找 / device instances and lookup |
| `src/board_driver.c` | probe 调度、safety-hw 注册 / probe scheduling, safety-hw registration |
| `src/bus.c` | 控制器表 / controller table |
| `src/dev_lifecycle.c` | lifecycle 实现 / lifecycle implementation |
| `src/config_store.c` | 配置存储 / config storage |
| `src/task_config.c` · `task_utils.c` | 任务辅助 / task helpers |
| `dts/board.dts` | **占位** DTS（无真实外设，仅 `compatible = "mini-tree,placeholder"`）；板级设 `BOARD_DTS` 覆盖；无板级节点亦可编过 / **placeholder** DTS (no real peripherals); override with `BOARD_DTS`; builds even with no board nodes |
| `dtsi/example-soc.dtsi` | 通用示例（cpus/soc/gpio/uart 模板，无 SoC 专有片段）；平台用 `BOARD_DTSI_DIR` 覆盖 / generic example (cpus/soc/gpio/uart template, no SoC-specific fragments); override with `BOARD_DTSI_DIR` |
| `dt-bindings/` | gpio/spi/uart/tim 参数宏（中间件通用）/ parameter macros shared by middleware |

---

## hal / vfs / bus

外设矩阵见 [architecture.md §2.1](architecture.md#21-外设覆盖当前)。
Peripheral matrix: see [architecture.md §2.1](architecture.md#21-外设覆盖当前).

命名约定 / Naming convention:

- HAL：`hal/<name>/hal_<name>.{h,c}` / HAL: `hal/<name>/hal_<name>.{h,c}`
- VFS：`vfs/<name>/vfs-<name>.{c,h}` / VFS: `vfs/<name>/vfs-<name>.{c,h}`
- Bus：`bus/<name>/<name>_bus.{c,h}` / Bus: `bus/<name>/<name>_bus.{c,h}`

另：`hal/amp`、`hal/storage`、`hal/system`、`hal/hal_if_dummy.c`（HAL weak 空实现）、`hal/paths.cmake`。
Also: `hal/amp`, `hal/storage`, `hal/system`, `hal/hal_if_dummy.c` (HAL weak empty implementations), `hal/paths.cmake`.

---

## core / osal / interrupt / system

| 路径 / Path | 说明 / Description |
| :--- | :--- |
| `core/include/status.h` | `VFS_ERR_*`、`ERR_PTR` |
| `core/include/compiler_compat.h` | 可移植属性与 mem API / portable attributes & mem API |
| `core/include/compiler_compat_poison.h` | poison 层 / poison layer |
| `core/include/event_bus.h` · `event_bus.hpp` | 事件总线 / event bus |
| `core/include/buffer_pool.h` | 缓冲池 / buffer pool |
| `core/include/system_log.h` · `production_log.h` | 日志 / logging |
| `core/src/*.c` | 上述实现 / implementations above |
| `osal/include/osal.h` | OSAL 总头 / OSAL master header |
| `osal/src/osal_{null,freertos,rtthread}.c` | 三后端 / three backends |
| `interrupt/interrupt.{c,h}` | VIRQ |
| `system_c/` · `system_cpp/` | init、wdt、scrubber、safe_state、task_manager、cmd（Kconfig 选 C 或 C++）/ init, wdt, scrubber, safe_state, task_manager, cmd (C or C++ via Kconfig) |
| `time_slice/task/xtask.{c,h}` | 裸机调度 / bare-metal scheduler |

---

## tools / ide / 其它 / Others

| 路径 / Path | 说明 / Description |
| :--- | :--- |
| `tools/dtc-lite.py` · `tools/dtc_lite/` | 设备树编译器包 / device tree compiler package |
| `tools/genconfig.py` | Kconfig → `config.h` |
| `tools/system_scrubber_crc_stub.h` | CRC 占位 / CRC stub |
| `ide/stubs/` | clangd 生成头占位 / generated-header stubs for clangd |
| `drivers/<chip>/` | 产品驱动共 **37 个**（`include/` + `src/`，`DRIVER_REGISTER` + dtc-lite 编译期 probe）；例 `w25qxx`、`st7789`、`ssd1306`…；**无**旧 `drivers/flash` / **37** product drivers (`include/` + `src/`, `DRIVER_REGISTER` + dtc-lite compile-time probe); e.g. `w25qxx`, `st7789`, `ssd1306`…; no legacy `drivers/flash` |
| `can_hook/` | CAN 协议超集钩子（见 [can_hook.md](can_hook.md)）/ CAN protocol superset hooks |
| `algorithm/buffer/` | 环形/双缓冲 / ring & double buffers |
| `cmake/*.cmake` | `dep_fetch` + 各 `mini_tree_link_*`（见 [ecosystem.md](ecosystem.md)）；另有 `disasm` / `rust` / `esp_idf` / `dep_fetch` + `mini_tree_link_*` helpers (see ecosystem.md); also `disasm` / `rust` / `esp_idf` |

> `lib/` 现状：vendor 仅 **FreeRTOS、RT-Thread、ETL**；**TinyUSB / lwIP / cJSON** 与其余积木均为链接期 FetchContent。
> `lib/` status: only **FreeRTOS, RT-Thread, ETL** are vendored; **TinyUSB / lwIP / cJSON** and all other bricks are fetched at link time.

---

## 列出全部源文件 / List All Source Files

```bash
find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
  ! -path './lib/*' ! -path './build/*' | sort
```

以上命令列出中间件源码（不含积木与构建产物）。
The command above lists middleware sources (excluding bricks and build output).

---

## 相关文档 / Related Docs

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md)
