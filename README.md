# mini_tree

平台无关的嵌入式中间件：用类似 Linux 的设备树与驱动模型，把裸机 / FreeRTOS / RT-Thread 上的外设访问统一起来。

**不绑定任何厂商 SDK。** 芯片 HAL、引脚与板级 DTS 由你的平台工程提供；本仓库只提供可裁剪的中间件核心，以及可选的开源积木（`lib/`）。

## 目录

* [简介](#简介)
* [特性](#特性)
* [适用场景](#适用场景)
* [积木型开源生态](#积木型开源生态)
* [快速开始](#快速开始)
* [架构](#架构)
* [文档](#文档)
* [开发](#开发)
* [许可证](#许可证)
* [致谢](#致谢)

---

## 简介

mini_tree 想做的事很简单：让应用通过 `device_open` / `read` / `write` / `ioctl` 访问外设，而不是在业务里直接摸厂商寄存器头文件。

为此它提供：

* **设备树（DTS/DTSI）**：板级描述硬件；编译期展开，运行期按表 probe  
* **VFS / Bus / HAL 分层**：上层看不到厂商 typedef；南向用 weak HAL，由平台强符号覆盖  
* **OSAL**：同一套 API 可跑在裸机协作调度、FreeRTOS 或 RT-Thread 上  
* **积木链接**：GUI、网络、文件系统、OTA 等能力放在 `lib/`，**默认不进固件**，需要时再 `mini_tree_link_*`

哲学接近 cJSON / lwIP 一类库：**够用、可移植、不挡路**。中间件核心保持瘦；生态靠开源积木按需叠加，而不是把所有能力焊死进一个巨无库。

---

## 特性

* **平台隔离**：公共头禁止厂商 HAL 类型；Bus 头对上层 `poison` `hal_*`  
* **硬件直投**：DTSI 中的厂商宏经预处理写入配置结构体，HAL 不做二次 enum 映射  
* **编译期 probe**：`DRIVER_REGISTER` + `dtc-lite` 生成静态表，运行期不做 `strcmp` 式匹配  
* **三后端 OSAL**：`CONFIG_OSAL_NULL` / `FREERTOS` / `RTTHREAD`  
* **SYSTEM_C / SYSTEM_CPP**：启动、看门狗、命令等可选 C 或 C++ 实现  
* **EventBus / 虚拟中断 / 缓冲池**：横向运行时服务  
* **Kconfig 裁剪**：功能用 `.config` 打开或关掉  
* **开源积木**：`lib/` 只留基础设施（OS / USB / lwIP / cJSON / ETL）；其余默认 FetchContent（本地可覆盖）；均为开源，不接入需付费商业闭源栈 

---

## 适用场景

| 底层 | mini_tree 做什么 |
| :--- | :--- |
| 裸机 | `OSAL_NULL` + 协作调度、设备树 Probe、EventBus、安全回路 |
| FreeRTOS | 在内核之上提供统一设备模型与 VFS/Bus |
| RT-Thread | OSAL 垫片；外设仍走本仓库，不混用两套设备框架 |
| ESP-IDF / Cube 等 | 平台侧提供 DTS 与 `hal_*_<soc>.c`；中间件不 `#include` 厂商头 |

更细的移植步骤见 [docs/porting_guide.md](docs/porting_guide.md)。

---

## 积木型开源生态

能力扩展走 **积木型链接**：核心不强制带齐 GUI / TLS / 文件系统；产品需要什么，就链什么。

```
应用选积木 ──► lib/ + cmake/mini_tree_link_* ──► mini_tree 核心 ──► 板级 HAL / DTS
```

* **`lib/` 只保留基础设施**：FreeRTOS、RT-Thread、TinyUSB、lwIP、cJSON、ETL  
* **其余开源积木默认 FetchContent**（也可手动放到 `lib/<Name>` 离线）  
* 可选积木默认不编进固件；通过 Kconfig 或 `mini_tree_link_<name>` 接入  
* **ETL 默认进库**：上层 C++ 基础，源码在 `lib/etl`，根 CMake 链入 `mini_tree`  
* 清单与致谢：[docs/ecosystem.md](docs/ecosystem.md) · 许可汇总：[NOTICE](NOTICE)  

可选积木示例：mbedtls*、littlefs*、FatFs*、SFUD*、LVGL*、u8g2*、nanopb*、coreMQTT*、coreHTTP*、miniz*、MCUBoot*、FreeModbus*、CMSIS-DSP*、MultiButton*、EasyLogger*、libmodbus* 等（`*` = 默认 Fetch）。

---

## 快速开始

### 依赖

* CMake ≥ 3.16  
* Python 3（`lark`：`pip install lark`；可选 `kconfiglib` 做 menuconfig）  
* 目标工具链与平台 SDK（只链在**你的**工程里）

### 获取

```bash
git clone https://github.com/H-000-H/mini_tree.git
```

把仓库作为子目录或 submodule（例如 `third_party/mini_tree`）加入平台工程。

### CMake 集成（示意）

```cmake
add_subdirectory(path/to/mini_tree)

# 链上中间件核心目标（名称以你工程里实际导出的为准，常见为 mini_tree）
target_link_libraries(your_firmware PUBLIC mini_tree)

# 按需点亮开源积木（示例）
# mini_tree_link_cjson(your_firmware)
# mini_tree_link_lwip(your_firmware "${CMAKE_CURRENT_SOURCE_DIR}/port")
# mini_tree_link_lvgl(your_firmware "${CMAKE_CURRENT_SOURCE_DIR}/port")
```

板级还需：覆盖 DTS、实现强符号 `hal_*`、按积木提供 port 头（如 `lwipopts.h`、`lv_conf.h`）。

逐步说明见 [docs/getting_started.md](docs/getting_started.md)。

### IDE

打开**仓库根目录**，配合 clangd（`compile_flags.txt` / `ide/stubs`）。详见 [docs/debug_monitor.md](docs/debug_monitor.md)。

---

## 架构

```
Application  ──device_* / ioctl──►  board/  ──DRIVER_REGISTER──►  vfs/
                                                          │
                                                          ▼
                                                     bus/ ──► hal/(weak) ──► 平台 HAL / 厂商 SDK

横向: core · osal · interrupt · system_c|cpp · can_hook · tools(dtc-lite, genconfig)
横向积木: lib/*（按需链接，不进核心契约）
```

要点：

1. **硬件直投** — DTSI 宏展开进配置结构体  
2. **编译期 probe** — 静态驱动表  
3. **南向隔离** — 公共 API 不泄漏厂商类型  

详解：[docs/architecture.md](docs/architecture.md)

---

## 文档

根目录只保留入口与开源惯例文件；专题都在 [`docs/`](docs/README.md)。

| 你想… | 去看 |
| :--- | :--- |
| 5 分钟建立整体印象 | [docs/overview.html](docs/overview.html) · [docs/usage.md](docs/usage.md) |
| 配进工程 / 点火 | [docs/getting_started.md](docs/getting_started.md) |
| 分层与数据流 | [docs/architecture.md](docs/architecture.md) |
| 开源积木清单与致谢 | [docs/ecosystem.md](docs/ecosystem.md) |
| 移植一块板 | [docs/porting_guide.md](docs/porting_guide.md) · [docs/driver_guide.md](docs/driver_guide.md) |
| 写应用 | [docs/service_spec.md](docs/service_spec.md) · [docs/peripherals.md](docs/peripherals.md) |
| 查文件 | [docs/file_index.md](docs/file_index.md) |
| 常见问题 | [docs/faq.md](docs/faq.md) |
| 应用层建议 | [app/app_must_pre_view.cpp](app/app_must_pre_view.cpp) |
工具链：[tools/README.md](tools/README.md)。完整索引：[docs/README.md](docs/README.md)。

---

## 开发

欢迎 Issue 与 PR。贡献约定见 [CONTRIBUTING.md](CONTRIBUTING.md)。

* 变更记录：[CHANGELOG.md](CHANGELOG.md)  
* 设计取舍：[docs/design_decisions.md](docs/design_decisions.md)  
* 规划：[docs/roadmap.md](docs/roadmap.md)

新文档请放进 `docs/`；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与法律文件。

---

## 许可证

本项目主体为 **Apache License 2.0**，全文见 [LICENSE](LICENSE)。源文件 SPDX 头与之对应。

`lib/` 基础设施与 Fetch 所得开源组件遵循各自许可证，汇总见 [NOTICE](NOTICE)。商用前请自行复核（例如 libmodbus 为 LGPL）。

---

## 致谢

mini_tree 的积木生态建立在众多开源作者与社区之上（FreeRTOS、lwIP、LVGL、cJSON、littlefs、armink 工具链、MCUBoot、Mbed TLS……）。完整致谢表见 [docs/ecosystem.md](docs/ecosystem.md) 第 6 节。

若署名或许可表述有误，欢迎提 Issue / PR 更正。
