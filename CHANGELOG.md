# Changelog

> 记录中间件 shelf 用户可见的变化。更细的设计动机见 [docs/design_decisions.md](docs/design_decisions.md)。

---

## [Unreleased]

### 产品驱动与布局

- 产品驱动迁入 `drivers/<chip>/{include,src}`（GLOB 编入 `mini_tree`）；**不再**使用独立 `components/driver_*`（除 ws2812）
- 删除旧示例 `drivers/flash`（`winbond,w25q64`）；SPI NOR 统一走 `drivers/w25qxx`（`winbond,w25qxx`）
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
