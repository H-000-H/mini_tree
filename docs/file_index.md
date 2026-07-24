# 文件索引

> 中间件源码导航（不含 `lib/**` 积木树）。路径相对仓库根。可选积木可能仅在 Fetch 缓存中，见 [ecosystem.md](ecosystem.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 找文件 / Code Review |
| **相关** | [architecture.md](architecture.md) · [design_decisions.md](design_decisions.md) |

---

## 顶层

| 路径 | 说明 |
| :--- | :--- |
| `CMakeLists.txt` | 静态库 `mini_tree`、genconfig、dtc-lite、源文件集合 |
| `Kconfig` / `.config` | 配置菜单与点文件 |
| `compile_flags.txt` / `.clangd` | clangd |
| `error_symbols.ld` | `ERR_SECTION_BASE` |
| `LICENSE` / `NOTICE` | Apache-2.0 全文 / 第三方归属 |
| `.gitignore` | 构建产物与本地 IDE 噪声（对齐平台仓） |
| `README.md` / `CHANGELOG.md` / `CONTRIBUTING.md` | 入口、变更、贡献（开源惯例留根目录） |
| `docs/` | 全部专题文档（见 [README.md](README.md)） |

---

## board/

| 路径 | 说明 |
| :--- | :--- |
| `include/device.h` | 设备模型、属性、持锁 VFS 包装 |
| `include/driver.h` | `DRIVER_REGISTER`、`board_driver_probe_all` |
| `include/bus.h` | 总线控制器抽象 |
| `include/dev_lifecycle.h` | 驱动 I/O 生命周期 |
| `include/board_config.h` | 容量宏聚合、`dt_config_gen` / `config.h` |
| `include/VFS.h` | 兼容包装（转发 status 等） |
| `src/board_device.c` | 设备实例与查找 |
| `src/board_driver.c` | probe 调度、safety-hw 注册 |
| `src/bus.c` | 控制器表 |
| `src/dev_lifecycle.c` | lifecycle 实现 |
| `src/config_store.c` | 配置存储 |
| `src/task_config.c` · `task_utils.c` | 任务辅助 |
| `dts/board.dts` | 默认占位 DTS |
| `dtsi/` | 平台覆盖目录（空） |
| `dt-bindings/` | gpio/spi/uart/tim 参数宏 |

---

## hal / vfs / bus

外设矩阵见 [architecture.md §2.1](architecture.md#21-外设覆盖当前)。

命名约定：

- HAL：`hal/<name>/hal_<name>.{h,c}`  
- VFS：`vfs/<name>/vfs-<name>.{c,h}`  
- Bus：`bus/<name>/<name>_bus.{c,h}`  

另：`hal/amp`、`hal/storage`、`hal/system`、`hal/hal_if_dummy.c`、`hal/paths.cmake`。

---

## core / osal / interrupt / system

| 路径 | 说明 |
| :--- | :--- |
| `core/include/status.h` | `VFS_ERR_*`、`ERR_PTR` |
| `core/include/compiler_compat.h` | 可移植属性与 mem API |
| `core/include/compiler_compat_poison.h` | poison 层 |
| `core/include/event_bus.h` · `event_bus.hpp` | 事件总线 |
| `core/include/buffer_pool.h` | 缓冲池 |
| `core/include/system_log.h` · `production_log.h` | 日志 |
| `core/src/*.c` | 上述实现 |
| `osal/include/osal.h` | OSAL 总头 |
| `osal/src/osal_{null,freertos,rtthread}.c` | 三后端 |
| `interrupt/interrupt.{c,h}` | VIRQ |
| `system_c/` · `system_cpp/` | init、wdt、scrubber、safe_state、task_manager、cmd |
| `time_slice/task/xtask.{c,h}` | 裸机调度 |

---

## tools / ide / 其它

| 路径 | 说明 |
| :--- | :--- |
| `tools/dtc-lite.py` · `tools/dtc_lite/` | 设备树编译器包 |
| `tools/genconfig.py` | Kconfig → `config.h` |
| `tools/system_scrubber_crc_stub.h` | CRC 占位 |
| `ide/stubs/` | clangd 生成头占位 |
| `drivers/flash/` | W25Q64 示例驱动 |
| `can_hook/` | CAN 协议超集钩子（见 [can_hook.md](can_hook.md)） |
| `algorithm/buffer/` | 环形/双缓冲 |
| `cmake/*.cmake` | `dep_fetch` + 各 `mini_tree_link_*`（见 [ecosystem.md](ecosystem.md)）；另有 `disasm` / `rust` |

---

## 列出全部源文件

```bash
find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
  ! -path './lib/*' ! -path './build/*' | sort
```

---

## 相关文档

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md)
