# Changelog

> 记录中间件 shelf 用户可见的变化。更细的设计动机见 [docs/design_decisions.md](docs/design_decisions.md)。

---

## [Unreleased]

### 产品驱动与布局

- 产品驱动迁入 `drivers/<chip>/{include,src}`（GLOB 编入 `mini_tree`）；**不再**使用独立 `components/driver_*`（除 ws2812）
- 删除旧示例 `drivers/flash`（`winbond,w25q64`）；SPI NOR 统一走 `drivers/w25qxx`（`winbond,w25qxx`）
- **板级 DTS/DTSI 外置**：经 `board_port.cmake` 注入；中间件仅保留占位 `board/dts/board.dts` 与通用 `dt-bindings/`
- **一份 mini 配多 MCU**：中间件不硬编码 `board_*` / `IDF_TARGET`；每板自带 `board_port` + `board_*` + `hal_*`
- ESP 板工程：`components/driver_ws2812` 为唯一允许厂商 RMT/`led_strip` 的例外；`app` 仅 `REQUIRES mini_tree` + `driver_ws2812`
- CMake：`file(GLOB drivers/*/src/*.c)` + `drivers/*/include|src` 进 `INCLUDE_DIRS` / dtc-lite 扫描（`CMakeLists.txt`、`cmake/esp_idf.cmake`、`board/CMakeLists.txt`）
- `compile_flags.txt` 补齐全部产品驱动 `-Idrivers/*/include`（及含头的 `src/`）

### 架构与代码

- HAL 全面 weak 空实现；默认 `board/dts/board.dts` 为通用占位  
- Bus/VFS 覆盖 gpio/spi/uart/i2c/i2s/can/usb/adc/dac/tim/rtc/iwdg/wwdg 等  
- USB 经 TinyUSB + 板级 `usb_tusb_port` 约定  
- clangd：`compile_flags.txt` + `ide/stubs`；禁止子目录覆盖  
- ETL 明确为 **上层 C++ 基础**：源码在 `lib/etl`，根 CMake 默认进 `mini_tree`  
- 开源积木：**基础设施进仓**（OS / USB / lwIP / cJSON / ETL）；**其余全部 FetchContent**（`cmake/dep_fetch.cmake`）  

### 代码风格与命名

- 新增 `.clang-format`：Allman 大括号、单语句 if/for/while 去大括号、4 空格缩进、100 列、指针靠左
- 新增分层 `.clang-tidy`（readability-identifier-naming）：根 = 内核区（app 以下非 cpp，全小写无前缀）；`app/` 与 `system_cpp/` = Google 区（类型 PascalCase + s_/g_/k_ 前缀）；宏全大写（container_of / likely / IS_ERR / COMPAT_* / `__XXX_H__` 头文件卫士例外）
- 新增 `.clang-format-ignore`：格式化排除 `lib/`
- 命名统一（clang-tidy 全量扫描清零）：`kTag`→`k_tag`、`struct Event/Subscriber`→`event/subscriber`、`namespace MiniTree`→`mini_tree`、`System_Pre_OS_Init`→`system_pre_os_init`、`xTask/xScheduler/ListNode`→`x_task/x_scheduler/list_node`、`Fifo_Data_type`→`fifo_data_type`、C++ 侧 `getInstance/registerCmd/kMaxCmdNameLen`→小写 等
- `tools/gen_compile_db.py`：补头文件条目与 include 目录（clang-tidy / clangd 可对头文件单独检查）

### 构建修复（通用 CMake 路径，最小构建实测通过）

- `core/include/status.h`：补 `#include <stddef.h>`（`NULL` 未声明）
- `vfs/gpio/vfs-gpio.c`：`DTC_GEN_COUNT_HETEROGENEOUS_GPIOS` 补 `#ifndef` 保护
- `time_slice/task/xtask.c`：`CHOSEN_SCHEDULER_TIM` 补 `#ifndef` 保护
- `cmake/tinyusb.cmake`：本地未提供 `src/CMakeLists.txt` 时 TinyUSB 核心源置空（离线不报错，详见 [docs/ecosystem.md](docs/ecosystem.md)）

### 文档

- 刷新 [esp_idf_cmake.md](docs/esp_idf_cmake.md) / [driver_guide.md](docs/driver_guide.md) / [file_index.md](docs/file_index.md)：产品驱动目录、ws2812 例外、与 ESP 板同步说明  
- 新增 [docs/ecosystem.md](docs/ecosystem.md)：积木型生态、Fetch 策略与致谢  
- 专题文档对齐 hybrid 依赖：去掉过时 `ide/third_party/etl`；交叉链接 ecosystem；刷新 `overview.html`  
- 根 [README.md](README.md) 中文改版（简介 / 特性 / 快速开始）  
- 专题文档统一放到 `docs/`；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING`  
- 新增 [docs/README.md](docs/README.md) 作为文档目录页  
- 原根目录 `NOTICE.md` → [docs/design_decisions.md](docs/design_decisions.md)；新增 Apache 惯例 [NOTICE](NOTICE)  
- 补充 [docs/references.md](docs/references.md) 与作者偏好取舍  
- 补缺口专题：USB / 外设 / AMP / can_hook / 运行时服务；ESP-IDF CMake 见 [esp_idf_cmake](docs/esp_idf_cmake.md)  
- 不推荐 Keil 作主 IDE；推荐 Cursor / VS Code / CLion / Qoder  
- [NOTICE](NOTICE) 全面重写：补全各组件版本号、版权人、SPDX 标识、合规提示（LGPL/双许可/ChaN）  
- [LICENSE](LICENSE) APPENDIX 填充实际版权行  
- [README.md](README.md) 许可证节增加商用合规要点（libmodbus LGPL / Mbed TLS 双许可 / FatFs）  
- [CONTRIBUTING.md](CONTRIBUTING.md) 新增 SPDX 头规范与 NOTICE 同步要求  
- [docs/README.md](docs/README.md) 导航表新增「合规 / 许可证」入口  
- [docs/peripherals.md](docs/peripherals.md) §5 新增 RS485 Modbus RTU 驱动条目  
- [docs/driver_guide.md](docs/driver_guide.md) §7 修正 `device_lc_bind` 说明（产品驱动由框架统一绑定）  

### 仓库卫生

- 补 [LICENSE](LICENSE)（Apache-2.0）  
- 补 [.gitignore](.gitignore)（对齐 Heterogeneous-Multicore / ST 板工程共用规则）  

### 配置

- OSAL：FreeRTOS V11.3.0 / RT-Thread v5.3.0 / NULL+xtask  
- SYSTEM_C / SYSTEM_CPP 二选一  

---

## [Historical]

多轮重构（设备树、硬件直投、OSAL、安全回路、文档迁徙等）详见 [docs/design_decisions.md](docs/design_decisions.md)。  
平台验证历史以各 SoC 工程仓库为准。

---

## 相关文档

- [docs/roadmap.md](docs/roadmap.md) · [docs/todolist.md](docs/todolist.md) · [docs/api_compatibility.md](docs/api_compatibility.md)
