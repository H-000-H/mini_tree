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
| `compile_flags.txt` / `.clangd` | clangd 编译数据库 |
| `.clang-format` · `.clang-format-ignore` · 分层 `.clang-tidy` | 代码风格：格式化 + 命名规范；app 层建议、app 以下强规定 |
| `error_symbols.ld` | `ERR_SECTION_BASE` |
| `LICENSE` / `NOTICE` | Apache-2.0 全文 / 第三方归属 |
| `.gitignore` | 构建产物与本地 IDE 噪声（对齐平台仓） |
| `README.md` / `CHANGELOG.md` / `CONTRIBUTING.md` | 入口、变更、贡献（开源惯例留根目录） |
| `docs/` | 全部专题文档（见 [README.md](README.md)） |

> 构建为通用 CMake：HAL 提供 weak 空实现，板级经 `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR` 注入。ESP-IDF 组件路径（`cmake/esp_idf.cmake`）已**迁移到 `esp` 分支**。

---

## board/（设备模型与设备树）

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
| `dts/board.dts` | **占位** DTS（无真实外设，仅 `compatible = "mini-tree,placeholder"`）；板级设 `BOARD_DTS` 覆盖；无板级节点亦可编过 |
| `dtsi/example-soc.dtsi` | 通用示例（cpus/soc/gpio/uart 模板，无 SoC 专有片段）；平台用 `BOARD_DTSI_DIR` 覆盖 |
| `dt-bindings/` | gpio/spi/uart/tim 参数宏（中间件通用） |

---

## hal / vfs / bus

外设矩阵见 [architecture.md §2.1](architecture.md#21-外设覆盖当前)。

命名约定：

- HAL：`hal/<name>/hal_<name>.{h,c}`
- VFS：`vfs/<name>/vfs-<name>.{c,h}`
- Bus：`bus/<name>/<name>_bus.{c,h}`

另：`hal/amp`、`hal/storage`、`hal/system`、`hal/hal_if_dummy.c`（HAL weak 空实现）、`hal/paths.cmake`。

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
| `osal/include/osal_null.h` | 裸机后端辅助接口 + C++ 任务重载声明（`CONFIG_OSAL_NULL_TASK_CPP`） |
| `osal/src/osal_{null,freertos,rtthread}.c` | 三后端 |
| `osal/src/osal_task.cpp` | 裸机 C++ 任务创建封装（`CONFIG_OSAL_NULL_TASK_CPP`） |
| `interrupt/interrupt.{c,h}` | VIRQ |
| `system_c/` · `system_cpp/` | init、wdt、scrubber、safe_state、task_manager、cmd（Kconfig 选 C 或 C++） |
| `time_slice/task/xtask*.{c,h}` | 裸机调度（协调式 `xtask_coop.c` / 抢占式 `xtask_preempt.c` / 共用 `xtask.h`） |

---

## tools / ide / 其它

| 路径 | 说明 |
| :--- | :--- |
| `tools/dtc-lite.py` · `tools/dtc_lite/` | 设备树编译器包 |
| `tools/genconfig.py` | Kconfig → `config.h` |
| `tools/system_scrubber_crc_stub.h` | CRC 占位 |
| `ide/stubs/` | clangd 生成头占位 |
| `drivers/<chip>/` | 产品驱动共 **37 个**（`include/` + `src/`，`DRIVER_REGISTER` + dtc-lite 编译期 probe）；例 `w25qxx`、`st7789`、`ssd1306`…；**无**旧 `drivers/flash` |
| `can_hook/` | CAN 协议超集钩子（见 [can_hook.md](can_hook.md)） |
| `algorithm/buffer/` | 环形/双缓冲 |
| `cmake/*.cmake` | `dep_fetch` + 各 `mini_tree_link_*`（见 [ecosystem.md](ecosystem.md)）；另有 `disasm` / `rust` / `esp_idf` |

---

## net/（网络协议栈胶水）

> 大部分子目录已 gitignore，仅提交 MQTT 最小构建集 + PPP/USB 网卡。

| 路径 | 说明 |
| :--- | :--- |
| `port/mqtt/mqtt_client.{c,h}` | coreMQTT v5 薄包装，`NET_*` 错误码 |
| `port/mqtt/core_mqtt_config.h` | coreMQTT 配置头 |
| `port/pppif/pppif.c` | PPP 网卡 lwIP netif 适配 |
| `port/usb/usbethif.c` | TinyUSB CDC-NCM/RNDIS 网卡 lwIP netif 适配 |

---

## ui/（UI 库胶水层）

> 大部分子目录已 gitignore，仅提交 `display/` 统一桥接头。

| 路径 | 说明 |
| :--- | :--- |
| `display/display_ui_bridge.h` | 面向 UI 库回调的入口（LVGL flush / u8g2 SendBuffer），走 `DISPLAY_CMD_*`，零第三方库依赖 |

> `lib/` 现状：vendor 仅 **FreeRTOS、RT-Thread、ETL**；**TinyUSB / lwIP** 为配置期 FetchContent，其余积木为链接期 FetchContent。

---

## 列出全部源文件

```bash
find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
  ! -path './lib/*' ! -path './build/*' | sort
```

以上命令列出中间件源码（不含积木与构建产物）。

---

## 相关文档

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md)
