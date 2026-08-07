# 移植指南

> 把中间件接到新 SoC / 新板 / 新 RTOS 的步骤。涉及：板级 `dtsi`、HAL 后端、OSAL 后端、CMake 注入。核心约定见 [architecture.md](architecture.md)、[design_decisions.md](design_decisions.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 接新平台 |
| **相关** | [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [getting_started.md](getting_started.md) |

---

## 1. 总体步骤

1. 建板级目录，放置 `dtsi`（设 `BOARD_DTSI_DIR`）。
2. 写 / 复用 HAL 后端（无板级用 weak 空实现）。
3. 选 OSAL 后端（`CONFIG_OSAL_NULL` / `FREERTOS` / `RTTHREAD`）。
4. 平台 CMake 注入 `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR`。
5. 跑 `genconfig` + `dtc-lite` 验证。
6. 编 `mini_tree` 库并接平台链接脚本（含 `ERR_SECTION_BASE`）。

---

## 2. 板级设备树

板级 `dtsi/` 提供 cpus / soc / gpio / uart 等节点模板（参考 `board/dtsi/example-soc.dtsi`）。平台用 `BOARD_DTSI_DIR` 覆盖；也可完全自定义 `BOARD_DTS`。无板级节点亦可编过（占位 DTS）。

| 项 | 注入方式 |
| :--- | :--- |
| `BOARD_DTSI_DIR` | CMake 变量，指向板级 `dtsi/` |
| `BOARD_DTS` | CMake 变量，指向完整板级 `board.dts` |
| `MINI_TREE_BOARD_PORT` | CMake 变量，指向板级端口目录 |

---

## 3. HAL 后端

无板级时 HAL 提供 weak 空实现（`hal/hal_if_dummy.c`），可编过但不工作；接真实硬件需实现 `hal_<name>.c` 的对应函数。多核平台还需 `hal/amp`。

---

## 4. OSAL 后端

| 后端 | Kconfig | 说明 |
| :--- | :--- | :--- |
| 裸机 | `CONFIG_OSAL_NULL` | 三后端之一；配合 `time_slice` 协调式/抢占式调度 |
| FreeRTOS | `CONFIG_OSAL_FREERTOS` | 预编译 / 链接期接入 |
| RT-Thread | `CONFIG_OSAL_RTTHREAD` | 预编译 / 链接期接入 |

裸机 C++ 任务封装见 `osal/src/osal_task.cpp`（`CONFIG_OSAL_NULL_TASK_CPP`）。

---

## 5. CMake 注入

平台工程通过以下变量注入（见 [getting_started.md](getting_started.md) §2）：

| 变量 | 作用 |
| :--- | :--- |
| `MINI_TREE_BOARD_PORT` | 板级端口目录 |
| `BOARD_DTS` | 完整板级 DTS 路径 |
| `BOARD_DTSI_DIR` | 板级 dtsi 目录 |
| `CMAKE_SOURCE_DIR` | 平台工程根（用于 `add_subdirectory(mini_tree)`） |

> 构建为通用 CMake：HAL 提供 weak 空实现，板级经上述变量注入；另提供 ESP-IDF 组件路径（`cmake/esp_idf.cmake`）。

---

## 6. 验证

1. `genconfig` 产出 `config.h`，确认 `CONFIG_*` 与 `.config` 一致。
2. `dtc-lite` 编译期探针命中（驱动注册）。
3. 编 `mini_tree` 静态库，无未定义符号。
4. 接平台链接脚本，确认 `ERR_SECTION_BASE` 段放置正确。

---

## 相关文档

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [getting_started.md](getting_started.md) · [keil_integration.md](keil_integration.md)
