# 积木型开源生态

> mini_tree 中间件本体提供设备模型、VFS/Bus/HAL、OSAL 与运行时服务；**不把所有能力塞进核心**。
>
> 能力扩展走 **积木型链接**：需要什么能力，就按需链入对应开源库，用板级 port 补齐配置与硬件胶水。
>
> **`lib/` 只保留 vendor**（FreeRTOS、RT-Thread、ETL 三个）；TinyUSB / lwIP 为**配置期 FetchContent**（根 CMake 直接 include 对应 `cmake/*.cmake`），其余开源积木为**链接期 FetchContent**（本地 `lib/<Name>` 仍优先，离线可手动 clone）。**不接入**需付费商业授权的闭源中间件。许可证见各库及 [`NOTICE`](../NOTICE)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台集成、应用开发、想扩展生态的人 |
| **相关** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 0. 基础设施 vendor 与其余 Fetch

| 策略 | 做法 | 组件 |
| :--- | :--- | :--- |
| **vendor（进 git）** | 源码在 `lib/`，随仓库提交 | **FreeRTOS**、**RT-Thread**、**ETL** |
| **配置期 Fetch** | 根 CMake 直接 `include(cmake/*.cmake)`，local-or-fetch | **TinyUSB**、**lwIP** |
| **链接期 Fetch** | 调用 `mini_tree_link_*` 时才拉取；可手动 clone 到 `lib/<Name>` 离线 | littlefs、FatFs、MultiButton、MCUBoot、coreMQTT、LVGL、u8g2、FlashDB、SFUD、EasyFlash、EasyLogger… |
| **C++ 基础（默认进库）** | ETL 在 `lib/etl`；根 CMake **始终** `mini_tree_link_etl(mini_tree)` | 上层 C++ / `SYSTEM_CPP` 基座 |

实现：`cmake/dep_fetch.cmake` 的 `mini_tree_dep_get()`（本地标记文件存在则用本地，否则 `FetchContent`）。

可选积木路径已写入根 [`.gitignore`](../.gitignore)。

> **变更**：`cmake/tinyusb.cmake` 对「本地未提供 `src/CMakeLists.txt`」的离线场景容错——TinyUSB 核心源置空而不报错（`mini_tree` 静态库默认不链接 tinyusb，仅板级 USB port 需要）。

---

## 1. 为什么是「积木」

| 原则 | 含义 |
| :--- | :--- |
| **开源积木** | 均为开源项目；商用前请复核各库 `LICENSE` |
| **基础设施 vendor，其余 Fetch** | 控体积；OS/ETL 常驻，全部积木首次链接需联网或预置本地 |
| **核心保持瘦** | 中间件不绑定厂商 SDK，也不强制带齐 GUI / 文件系统 |
| **按需链接** | 可选积木默认不编进固件；调用 `mini_tree_link_*`（或 OSAL Kconfig）时才进入镜像 |
| **ETL 默认进库** | **不是可选积木**：上层 C++ 基础，源码在 `lib/etl`，根 CMake 默认链入 `mini_tree` |
| **CMake 一块积木一个入口** | 多数库有 `cmake/<name>.cmake`，提供 `mini_tree_link_<name>(target …)` |
| **板级补 port** | 配置头（如 `lv_conf.h`、`lwipopts.h`）与 diskio/SPI/显示 flush 等由平台提供 |

```
┌──────────────────────────────────────────────────────────┐
│  应用 / 产品策略（选积木：网络？GUI？OTA？存储？）          │
└────────────────────────────┬─────────────────────────────┘
                             │ mini_tree_link_* / Kconfig
┌────────────────────────────▼─────────────────────────────┐
│  基础设施 lib/（OS·ETL）+ 按需 Fetch 积木（link 时）   │
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
（分层示意：应用选积木 → 基础设施 lib/Fetch → 核心 → 板级硬件。）

各位可以在此模型上继续接入更多**开源**库：依赖 Fetch 或放入 `lib/<Name>`，补一个 `cmake/<name>.cmake`，在产品 CMake 里 `mini_tree_link_*` 即可。

---

## 2. 已接入的开源库

按能力分类。版本钉在对应 `cmake/*.cmake` 的 `*_VERSION` / `GIT_TAG`。

路径写 `lib/...` 表示约定位置；**Fetch 积木可能仅在构建缓存中存在**。

### 2.1 内核 / 调度（基础设施）

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| FreeRTOS | `lib/freeRTOS` | Kernel V11.3.0 | RTOS 内核 | `CONFIG_OSAL_FREERTOS` |
| RT-Thread | `lib/rtthread` | v5.3.0 | RTOS 内核 | `CONFIG_OSAL_RTTHREAD` |
| （裸机） | `time_slice/task` | — | 协作式调度 | `CONFIG_OSAL_NULL` |

### 2.2 连接与协议

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| TinyUSB | Fetch / `lib/tinyusb` | 0.21.0 | USB 设备/主机栈 | 板级 `usb_tusb_port` |
| lwIP | Fetch / `lib/lwip` | 2.2.1 | TCP/IP | `mini_tree_link_lwip` + `lwipopts.h` |
| coreMQTT | Fetch / `lib/coreMQTT` | v5.0.2 | MQTT 客户端 | `mini_tree_link_coremqtt` + `core_mqtt_config.h` |

### 2.3 人机与输入

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | Fetch / `lib/lvgl` | v9.5.0 | 彩色 GUI | `mini_tree_link_lvgl` + `lv_conf.h` |
| u8g2 | Fetch / `lib/u8g2` | 2.37.1 | 单色/OLED | `mini_tree_link_u8g2` |
| MultiButton | Fetch / `lib/MultiButton` | master | 多按键状态机 | `mini_tree_link_multibutton` |

### 2.4 日志

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| ETL | `lib/etl` | 20.48.1 | **上层 C++ 基础** | **默认进库** |
| EasyLogger | Fetch / `lib/EasyLogger` | 2.2.0 | 日志 | `mini_tree_link_easylogger` |

---

## 3. 典型积木组合（示例）

| 产品形态 | 建议积木 |
| :--- | :--- |
| 裸机仪表 / 小屏 | OSAL_NULL + u8g2 或 LVGL（经 `ui/` 胶水层 + `DISPLAY_CMD_*`）+ MultiButton + EasyLogger |
| 联网传感器 | FreeRTOS/RTT + lwIP + coreMQTT |
| USB 大容量 / 网卡 | TinyUSB +（可选）FatFs / lwIP |

---

## 4. 怎么再接一块新积木

1. 优先用 `mini_tree_dep_get()` + Fetch；需要离线时再 clone 到 `lib/<Name>`。
2. 新增 `cmake/<name>.cmake`：默认 **不**链入；提供 `mini_tree_link_<name>(target …)`。
3. 在根 `CMakeLists.txt` 中 `include`；更新 `README` / 本文档 / [`NOTICE`](../NOTICE)；加入 `.gitignore`。
4. 产品工程提供 port，再调用 link 函数。

策略：**只接开源；除基础设施外优先 Fetch，不主动提交巨量源码。**

---

## 5. 和中间件核心的边界

- **可以**：在应用或板级服务里调用开源库 API；经 `device_*` / EventBus 与中间件协作。
- **不要**：在 `vfs/` / `bus/` 公共头强绑某个 GUI 实现，或把厂商 HAL typedef 泄漏进中间件公共 API。
- **南向**：Flash/显示/网卡仍通过板级 HAL 或 port 回调接触硬件，保持「硬件直投、中间件不绑 SDK」。
- **UI 胶水层 (`ui/`)**：LVGL / u8g2 的 flush 回调经 `ui/display/display_ui_bridge.h` 统一入口，走 `device_ioctl(DISPLAY_CMD_*)` 触显示硬件；不直调 `bus_*` / `hal_*`，换屏仅需更换 device 指针。

产品驱动（37 个）位于 `drivers/<chip>/{include,src}`，是生态的一部分但走本仓 `DRIVER_REGISTER` 契约，与积木库互不绑定。

---

## 6. 致谢

mini_tree 的积木生态建立在广大开源作者与社区之上。感谢（排名不分先后）：

| 项目 | 上游 | 致谢要点 |
| :--- | :--- | :--- |
| FreeRTOS | Amazon FreeRTOS / FreeRTOS.org | 实时内核 |
| RT-Thread | RT-Thread 团队 | 国产 RTOS 与组件生态 |
| TinyUSB | Ha Thach 与贡献者 | 可移植 USB 栈 |
| lwIP | Savannah / lwIP 社区 | 轻量 TCP/IP |
| coreMQTT | FreeRTOS / Amazon | 嵌入式 MQTT |
| SFUD / EasyFlash / FlashDB / EasyLogger | armink 与贡献者 | Flash 驱动与日志工具链 |
| littlefs | littlefs-project | 掉电安全文件系统 |
| FatFs | ChaN | 通用 FAT 文件系统 |
| MCUBoot | MCUBoot / Zephyr 等贡献者 | 安全启动与升级 |
| LVGL | kisvegabor 与 LVGL 社区 | 嵌入式 GUI |
| u8g2 | olikraus 与贡献者 | 单色显示库 |
| MultiButton | 0x1abin 与贡献者 | 按键状态机 |
| ETL | John Wellbelove / ETLCPP | 无堆模板库 |

若遗漏署名或许可表述有误，欢迎提 Issue / PR 更正。完整版权与许可声明以各组件目录内文件及 [`NOTICE`](../NOTICE) 为准。
