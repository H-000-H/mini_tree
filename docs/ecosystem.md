# 积木型开源生态 / Building-Block Open-Source Ecosystem

> mini_tree 中间件本体提供设备模型、VFS/Bus/HAL、OSAL 与运行时服务；**不把所有能力塞进核心**。
> The mini_tree middleware core provides the device model, VFS/Bus/HAL, OSAL and runtime services; it does **not** cram every capability into the core.
>
> 能力扩展走 **积木型链接**：需要什么能力，就按需链入对应开源库，用板级 port 补齐配置与硬件胶水。
> Capability expansion follows a **link-as-a-block** model: link in the needed open-source library on demand, and supply configuration plus hardware glue through a board-level port.
>
> **`lib/` 只保留 vendor**（FreeRTOS、RT-Thread、ETL 三个）；TinyUSB / lwIP / cJSON 为**配置期 FetchContent**，其余开源积木为**链接期 FetchContent**（本地 `lib/<Name>` 仍优先，离线可手动 clone）。**不接入**需付费商业授权的闭源中间件。许可证见各库及 [`NOTICE`](../NOTICE)。
> **`lib/` holds only the vendors** (FreeRTOS, RT-Thread, ETL); TinyUSB / lwIP / cJSON are **configure-time FetchContent**, and the remaining open-source blocks are **link-time FetchContent** (a local `lib/<Name>` still wins; clone manually for offline use). Closed-source middleware requiring paid commercial licenses is **not** integrated. Licenses live in each library and in [`NOTICE`](../NOTICE).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 平台集成、应用开发、想扩展生态的人 / Platform integrators, application developers, and anyone extending the ecosystem |
| **相关 / Related** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 0. 基础设施 vendor / 其余 Fetch / Infrastructure Vendors vs. Fetched Blocks

| 策略 / Strategy | 做法 / Behavior | 组件 / Components |
| :--- | :--- | :--- |
| **vendor（进 git）** | 源码在 `lib/`，随仓库提交 | **FreeRTOS**、**RT-Thread**、**ETL** |
| **配置期 Fetch** | 未本地提供时 configure 阶段拉取；本地 `lib/<Name>` 优先 | **TinyUSB**、**lwIP**、**cJSON** |
| **链接期 Fetch** | 调用 `mini_tree_link_*` 时才拉取；可手动 clone 到 `lib/<Name>` 离线 | littlefs、FatFs、MultiButton、MCUBoot、nanopb、coreMQTT、coreHTTP、miniz、libmodbus、LVGL、u8g2、mbedtls、CMSIS-DSP、FlashDB、SFUD、EasyFlash、EasyLogger、FreeModbus… |
| **C++ 基础（默认进库）** | ETL 在 `lib/etl`；根 CMake **始终** `mini_tree_link_etl(mini_tree)` | 上层 C++ / `SYSTEM_CPP` 基座 |

实现：`cmake/dep_fetch.cmake` 的 `mini_tree_dep_get()`（本地标记文件存在则用本地，否则 `FetchContent`）。
Implementation: `mini_tree_dep_get()` in `cmake/dep_fetch.cmake` (uses the local copy when its marker file exists, otherwise `FetchContent`).

可选积木路径已写入根 [`.gitignore`](../.gitignore)。
Optional block paths are listed in the root [`.gitignore`](../.gitignore).

> **变更 / Change**：`cmake/tinyusb.cmake` 对「本地未提供 `src/CMakeLists.txt`」的离线场景容错——TinyUSB 核心源置空而不报错（`mini_tree` 静态库默认不链接 tinyusb，仅板级 USB port 需要）。
> `cmake/tinyusb.cmake` tolerates the offline case where `src/CMakeLists.txt` is not provided locally — the TinyUSB core sources are left empty instead of failing (the `mini_tree` static library does not link tinyusb by default; only the board-level USB port needs it).

---

## 1. 为什么是「积木」/ Why Blocks

| 原则 / Principle | 含义 / Meaning |
| :--- | :--- |
| **开源积木 / Open-source blocks** | 均为开源项目；商用前请复核各库 `LICENSE`（如 libmodbus 为 LGPL）/ All are open source; re-check each library's `LICENSE` before commercial use (e.g. libmodbus is LGPL) |
| **基础设施 vendor / 其余 Fetch / Vendors for infrastructure, Fetch for the rest** | 控体积；OS/ETL 常驻，USB/网络/JSON 配置期拉取，其它首次链接需联网或预置本地 / Keeps the tree small; OS/ETL are resident, USB/network/JSON are fetched at configure time, the rest need network or a local copy at first link |
| **核心保持瘦 / Core stays lean** | 中间件不绑定厂商 SDK，也不强制带齐 GUI / TLS / 文件系统 / The middleware never binds a vendor SDK, nor forces GUI / TLS / filesystems in |
| **按需链接 / Link on demand** | 可选积木默认不编进固件；调用 `mini_tree_link_*`（或 OSAL Kconfig）时才进入镜像 / Optional blocks are not built into firmware by default; they enter the image only when `mini_tree_link_*` (or the OSAL Kconfig) is used |
| **ETL 默认进库 / ETL ships by default** | **不是可选积木**：上层 C++ 基础，源码在 `lib/etl`，根 CMake 默认链入 `mini_tree` / **Not an optional block**: it is the C++ foundation for upper layers, source lives in `lib/etl`, and the root CMake links it into `mini_tree` by default |
| **CMake 一块积木一个入口 / One CMake entry per block** | 多数库有 `cmake/<name>.cmake`，提供 `mini_tree_link_<name>(target …)` / Most libraries have a `cmake/<name>.cmake` exposing `mini_tree_link_<name>(target …)` |
| **板级补 port / Board supplies the port** | 配置头（如 `lv_conf.h`、`lwipopts.h`）与 diskio/SPI/显示 flush 等由平台提供 / Config headers (e.g. `lv_conf.h`, `lwipopts.h`) and diskio/SPI/display-flush glue come from the platform |

```
┌──────────────────────────────────────────────────────────┐
│  应用 / 产品策略（选积木：网络？GUI？OTA？存储？）          │
└────────────────────────────┬─────────────────────────────┘
                             │ mini_tree_link_* / Kconfig
┌────────────────────────────▼─────────────────────────────┐
│  基础设施 lib/（OS·USB·lwIP·cJSON·ETL）+ Fetch 可选积木   │
└────────────────────────────┬─────────────────────────────┘
                             │ 设备 / ioctl / EventBus
┌────────────────────────────▼─────────────────────────────┐
│  mini_tree 核心：board · vfs · bus · hal · osal · system │
└────────────────────────────┬─────────────────────────────┘
                             │ 板级 DTS + 强符号 HAL
┌────────────────────────────▼─────────────────────────────┐
│  芯片 SDK / 引脚 / Flash 分区 / 显示与网卡硬件             │
└──────────────────────────────────────────────────────────┘
```
（分层示意：应用选积木 → 基础设施 lib/Fetch → 核心 → 板级硬件。/ Layering: app picks blocks → infrastructure lib/Fetch → core → board hardware.）

各位可以在此模型上继续接入更多**开源**库：依赖 Fetch 或放入 `lib/<Name>`，补一个 `cmake/<name>.cmake`，在产品 CMake 里 `mini_tree_link_*` 即可。
You can keep adding more **open-source** libraries on this model: rely on Fetch or drop the sources into `lib/<Name>`, add a `cmake/<name>.cmake`, and call `mini_tree_link_*` from your product CMake.

---

## 2. 已接入的开源库 / Integrated Open-Source Libraries

按能力分类。版本钉在对应 `cmake/*.cmake` 的 `*_VERSION` / `GIT_TAG`。
Grouped by capability. Versions are pinned by `*_VERSION` / `GIT_TAG` in the matching `cmake/*.cmake`.

路径写 `lib/...` 表示约定位置；**Fetch 积木可能仅在构建缓存中存在**。
A `lib/...` path is the conventional location; **fetched blocks may exist only in the build cache**.

### 2.1 内核 / 调度（基础设施）/ Kernels & Scheduling (Infrastructure)

| 库 / Library | 路径 / Path | 版本 / Version | 作用 / Role | 接入方式 / Integration |
| :--- | :--- | :--- | :--- | :--- |
| FreeRTOS | `lib/freeRTOS` | Kernel V11.3.0 | RTOS 内核 / RTOS kernel | `CONFIG_OSAL_FREERTOS` |
| RT-Thread | `lib/rtthread` | v5.3.0 | RTOS 内核 / RTOS kernel | `CONFIG_OSAL_RTTHREAD` |
| （裸机）/ (Bare metal) | `time_slice/task` | — | 协作式调度 / Cooperative scheduling | `CONFIG_OSAL_NULL` |

### 2.2 连接与协议 / Connectivity & Protocols

| 库 / Library | 路径 / Path | 版本 / Version | 作用 / Role | 接入方式 / Integration |
| :--- | :--- | :--- | :--- | :--- |
| TinyUSB | Fetch / `lib/tinyusb` | 0.21.0 | USB 设备/主机栈 / USB device/host stack | 板级 `usb_tusb_port` |
| lwIP | Fetch / `lib/lwip` | 2.2.1 | TCP/IP | `mini_tree_link_lwip` + `lwipopts.h` |
| coreMQTT | Fetch / `lib/coreMQTT` | v5.0.2 | MQTT 客户端 / MQTT client | `mini_tree_link_coremqtt` + `core_mqtt_config.h` |
| coreHTTP | Fetch / `lib/coreHTTP` | v3.1.3 | HTTP 客户端 / HTTP client | `mini_tree_link_corehttp` + `core_http_config.h` |
| libmodbus | Fetch / `lib/libmodbus` | v3.1.10 | Modbus RTU/TCP | `mini_tree_link_libmodbus`（宜 POSIX/RTOS / prefer POSIX/RTOS） |
| FreeModbus | Fetch / `lib/FreeModbus` | 1.6.0 | Modbus RTU 从站 / Modbus RTU slave | `mini_tree_link_freemodbus` + `mbport.h` |
| mbedtls | Fetch / `lib/mbedtls` | mbedtls-4.2.0 | TLS / 密码学 / TLS & crypto | `mini_tree_link_mbedtls` + `mbedtls_config.h` |

### 2.3 存储与升级 / Storage & Upgrade

| 库 / Library | 路径 / Path | 版本 / Version | 作用 / Role | 接入方式 / Integration |
| :--- | :--- | :--- | :--- | :--- |
| SFUD | Fetch / `lib/SFUD` | 1.1.0 | SPI Flash 统一驱动 / Unified SPI Flash driver | `mini_tree_link_sfud` + `sfud_cfg.h` |
| littlefs | Fetch / `lib/littlefs` | v2.11.3 | 掉电安全文件系统 / Power-loss-safe filesystem | `mini_tree_link_littlefs` |
| FatFs | Fetch / `lib/FatFs` | R0.16 | FAT/exFAT | `mini_tree_link_fatfs` + `ffconf.h` |
| EasyFlash | Fetch / `lib/EasyFlash` | master | Flash ENV/IAP | `mini_tree_link_easyflash` |
| FlashDB | Fetch / `lib/FlashDB` | 2.2.0 | KV + 时序库 / KV + time-series DB | `mini_tree_link_flashdb` + `fdb_cfg.h` |
| MCUBoot | Fetch / `lib/mcuboot` | v2.4.0 | 安全 Boot / OTA | `mini_tree_link_mcuboot` |

### 2.4 人机与输入 / HMI & Input

| 库 / Library | 路径 / Path | 版本 / Version | 作用 / Role | 接入方式 / Integration |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | Fetch / `lib/lvgl` | v9.5.0 | 彩色 GUI / Color GUI | `mini_tree_link_lvgl` + `lv_conf.h` |
| u8g2 | Fetch / `lib/u8g2` | 2.37.1 | 单色/OLED / Monochrome/OLED | `mini_tree_link_u8g2` |
| MultiButton | Fetch / `lib/MultiButton` | master | 多按键状态机 / Multi-button state machine | `mini_tree_link_multibutton` |

### 2.5 数据、日志与计算 / Data, Logging & Compute

| 库 / Library | 路径 / Path | 版本 / Version | 作用 / Role | 接入方式 / Integration |
| :--- | :--- | :--- | :--- | :--- |
| cJSON | Fetch / `lib/cJSON` | 1.7.19 | JSON | `mini_tree_link_cjson` |
| ETL | `lib/etl` | 20.48.1 | **上层 C++ 基础** / **C++ foundation for upper layers** | **默认进库** / **ships by default** |
| nanopb | Fetch / `lib/nanopb` | 0.4.9.1 | Protobuf | `mini_tree_link_nanopb` |
| EasyLogger | Fetch / `lib/EasyLogger` | 2.2.0 | 日志 / Logging | `mini_tree_link_easylogger` |
| CMSIS-DSP | Fetch / `lib/CMSIS-DSP` | v1.17.1 | DSP | `mini_tree_link_cmsis_dsp` |
| miniz | Fetch / `lib/miniz` | 3.1.2 | zlib 兼容压缩 / zlib-compatible compression | `mini_tree_link_miniz` |

---

## 3. 典型积木组合（示例）/ Typical Block Combinations (Examples)

| 产品形态 / Product Form | 建议积木 / Suggested Blocks |
| :--- | :--- |
| 裸机仪表 / 小屏 / Bare-metal instrument / small display | OSAL_NULL + u8g2 或 LVGL + MultiButton + EasyLogger |
| 联网传感器 / Networked sensor | FreeRTOS/RTT + lwIP + coreMQTT/coreHTTP + mbedtls + cJSON/nanopb |
| 带 SPI Flash 记录仪 / SPI-Flash data logger | SFUD + littlefs 或 FlashDB + EasyLogger +（可选 miniz / optionally miniz） |
| USB 大容量 / 网卡 / USB mass storage / NIC | TinyUSB +（可选 / optionally）FatFs / lwIP |
| 可 OTA 量产机 / OTA-capable production device | MCUBoot + mbedtls（验签 / signature verify）+ 下载通道（USB/网络 / USB/network）+（可选 / optionally）miniz |
| 工控从站 / Industrial slave | FreeModbus（RTU）或 libmodbus（POSIX） |

---

## 4. 怎么再接一块新积木 / Adding a New Block

1. 优先用 `mini_tree_dep_get()` + Fetch；需要离线时再 clone 到 `lib/<Name>`。
   Prefer `mini_tree_dep_get()` + Fetch; clone into `lib/<Name>` only when offline.
2. 新增 `cmake/<name>.cmake`：默认 **不**链入；提供 `mini_tree_link_<name>(target …)`。
   Add `cmake/<name>.cmake`: do **not** link it by default; provide `mini_tree_link_<name>(target …)`.
3. 在根 `CMakeLists.txt` 中 `include`；更新 `README` / 本文档 / [`NOTICE`](../NOTICE)；加入 `.gitignore`。
   `include` it in the root `CMakeLists.txt`; update `README` / this doc / [`NOTICE`](../NOTICE); add it to `.gitignore`.
4. 产品工程提供 port，再调用 link 函数。
   The product project provides the port, then calls the link function.

策略：**只接开源；除基础设施外优先 Fetch，不主动提交巨量源码。**
Policy: **open source only; prefer Fetch for everything except infrastructure; never commit bulk third-party sources.**

---

## 5. 和中间件核心的边界 / Boundary with the Middleware Core

- **可以 / Allowed**：在应用或板级服务里调用开源库 API；经 `device_*` / EventBus 与中间件协作。
  Call open-source library APIs from applications or board services; cooperate with the middleware via `device_*` / EventBus.
- **不要 / Avoid**：在 `vfs/` / `bus/` 公共头强绑某个 GUI/TLS 实现，或把厂商 HAL typedef 泄漏进中间件公共 API。
  Hard-binding a GUI/TLS implementation in `vfs/` / `bus/` public headers, or leaking vendor HAL typedefs into the middleware public API.
- **南向 / Southbound**：Flash/显示/网卡仍通过板级 HAL 或 port 回调接触硬件，保持「硬件直投、中间件不绑 SDK」。
  Flash/display/NIC still touch hardware through board-level HAL or port callbacks, keeping "hardware direct-inject, middleware never binds an SDK".

产品驱动（37 个）位于 `drivers/<chip>/{include,src}`，是生态的一部分但走本仓 `DRIVER_REGISTER` 契约，与积木库互不绑定。
The 37 product drivers live in `drivers/<chip>/{include,src}`; they are part of the ecosystem but follow this repo's `DRIVER_REGISTER` contract and stay independent of the block libraries.

---

## 6. 致谢 / Acknowledgements

mini_tree 的积木生态建立在广大开源作者与社区之上。感谢（排名不分先后）：
mini_tree's block ecosystem stands on the shoulders of many open-source authors and communities. Thanks (in no particular order):

| 项目 / Project | 上游 / Upstream | 致谢要点 / What We Thank Them For |
| :--- | :--- | :--- |
| FreeRTOS | Amazon FreeRTOS / FreeRTOS.org | 实时内核 / Real-time kernel |
| RT-Thread | RT-Thread 团队 / RT-Thread team | 国产 RTOS 与组件生态 / Homegrown RTOS and component ecosystem |
| TinyUSB | Ha Thach 与贡献者 / Ha Thach & contributors | 可移植 USB 栈 / Portable USB stack |
| lwIP | Savannah / lwIP 社区 / lwIP community | 轻量 TCP/IP / Lightweight TCP/IP |
| coreMQTT | FreeRTOS / Amazon | 嵌入式 MQTT / Embedded MQTT |
| libmodbus | Stéphane Raimbault 与贡献者 / Stéphane Raimbault & contributors | Modbus 协议栈 / Modbus protocol stack |
| Mbed TLS | TrustedFirmware / Mbed-TLS | TLS 与密码学 / TLS & cryptography |
| SFUD / EasyFlash / FlashDB / EasyLogger | armink 与贡献者 / armink & contributors | Flash 与日志工具链 / Flash & logging toolchain |
| littlefs | littlefs-project | 掉电安全文件系统 / Power-loss-safe filesystem |
| FatFs | ChaN | 通用 FAT 文件系统 / General-purpose FAT filesystem |
| MCUBoot | MCUBoot / Zephyr 等贡献者 / MCUBoot, Zephyr & contributors | 安全启动与升级 / Secure boot & upgrade |
| LVGL | kisvegabor 与 LVGL 社区 / kisvegabor & LVGL community | 嵌入式 GUI / Embedded GUI |
| u8g2 | olikraus 与贡献者 / olikraus & contributors | 单色显示库 / Monochrome display library |
| MultiButton | 0x1abin 与贡献者 / 0x1abin & contributors | 按键状态机 / Button state machine |
| cJSON | Dave Gamble 与贡献者 / Dave Gamble & contributors | JSON 解析 / JSON parsing |
| nanopb | Petteri Aimonen 与贡献者 / Petteri Aimonen & contributors | 嵌入式 Protobuf / Embedded Protobuf |
| ETL | John Wellbelove / ETLCPP | 无堆模板库 / Heap-free template library |
| CMSIS-DSP | Arm 与贡献者 / Arm & contributors | DSP 算法库 / DSP algorithm library |
| coreHTTP | FreeRTOS / Amazon（含 llhttp / incl. llhttp） | 嵌入式 HTTP / Embedded HTTP |
| miniz | Rich Geldreich 与贡献者 / Rich Geldreich & contributors | zlib 兼容压缩 / zlib-compatible compression |
| FreeModbus | Christian Walter 与贡献者 / Christian Walter & contributors | Modbus 从站 / Modbus slave |

若遗漏署名或许可表述有误，欢迎提 Issue / PR 更正。完整版权与许可声明以各组件目录内文件及 [`NOTICE`](../NOTICE) 为准。
If a credit is missing or a license statement is wrong, feel free to open an Issue / PR. Full copyright and license statements are governed by the files inside each component and by [`NOTICE`](../NOTICE).
