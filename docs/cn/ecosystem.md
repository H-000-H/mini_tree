# 积木型开源生态

> mini_tree 中间件本体提供设备模型、VFS/Bus/HAL、OSAL 与运行时服务；**不把所有能力塞进核心**。
>
> 能力扩展走 **积木型链接**：需要什么能力，就按需链入对应开源库，用板级 port 补齐配置与硬件胶水。
>
> **`lib/` 只保留 vendor ETL**；其余开源积木走 **ESP-IDF 组件生态**（`idf_component.yml` / registry），不再使用 FetchContent。**不接入**需付费商业授权的闭源中间件。许可证见各库及 [`NOTICE`](../NOTICE)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台集成、应用开发、想扩展生态的人 |
| **相关** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 0. 依赖策略

| 策略 | 做法 | 组件 |
| :--- | :--- | :--- |
| **vendor（进 git）** | 源码在 `lib/`，随仓库提交 | **ETL**（唯一） |
| **IDF 组件** | 经 `idf_component.yml` / `idf.py add-dependency` 从 registry 拉取 | FreeRTOS、TinyUSB、lwIP、cJSON、LVGL、mbedtls 等（按需） |
| **C++ 基础（默认进库）** | ETL 在 `lib/etl`；`cmake/esp_idf.cmake` 默认链入 `lib/etl/include` | 上层 C++ / `SYSTEM_CPP` 基座 |

> 本分支不再使用 `cmake/dep_fetch.cmake` / FetchContent / `mini_tree_link_*` 体系（已移除）。FreeRTOS 由 ESP-IDF 内置提供，`CONFIG_OSAL_FREERTOS` 对接 IDF 内核。

---

## 1. 为什么是「积木」

| 原则 | 含义 |
| :--- | :--- |
| **开源积木** | 均为开源项目；商用前请复核各库 `LICENSE`（如 libmodbus 为 LGPL） |
| **IDF 组件按需拉取** | 控体积；在 `idf_component.yml` 声明所需组件即可，由 IDF Component Manager 托管版本与下载 |
| **核心保持瘦** | 中间件不绑定厂商 SDK，也不强制带齐 GUI / TLS / 文件系统 |
| **按需链接** | 可选积木默认不编进固件；在 `idf_component.yml` 声明依赖时才进入镜像 |
| **ETL 默认进库** | **不是可选积木**：上层 C++ 基础，源码在 `lib/etl`，`cmake/esp_idf.cmake` 默认链入 |
| **板级补 port** | 配置头（如 `lv_conf.h`、`lwipopts.h`）与 diskio/SPI/显示 flush 等由平台提供 |

```
┌──────────────────────────────────────────────────────────┐
│  应用 / 产品策略（选积木：网络？GUI？OTA？存储？）          │
└────────────────────────────┬─────────────────────────────┘
                             │ idf_component.yml 声明依赖
┌────────────────────────────▼─────────────────────────────┐
│  IDF 组件体系（registry / managed_components）+ lib/ETL  │
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
（分层示意：应用选积木 → IDF 组件体系 → 核心 → 板级硬件。）

各位可以在此模型上继续接入更多**开源**库：在 `idf_component.yml` 声明对应组件即可。

---

## 2. 已接入的开源库

按能力分类。版本以 ESP Component Registry / IDF 组件声明为准。

「接入方式」指在 ESP 工程里如何启用该积木（多经 `idf_component.yml` 声明）。

### 2.1 内核 / 调度（基础设施）

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| FreeRTOS | ESP-IDF 内置 | IDF 随附 | RTOS 内核 | `CONFIG_OSAL_FREERTOS`（默认） |
| （裸机） | `time_slice/task` | — | 协作式调度 | `CONFIG_OSAL_NULL` |

### 2.2 连接与协议

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| TinyUSB | `esp_tinyusb`（registry） | IDF 随附 | USB 设备/主机栈 | 板级 `usb_tusb_port` |
| lwIP | ESP-IDF 内置 | IDF 随附 | TCP/IP | IDF 网络组件 + `lwipopts.h` |
| coreMQTT | registry 组件 | registry | MQTT 客户端 | `idf_component.yml` 声明 + `core_mqtt_config.h` |
| coreHTTP | registry 组件 | registry | HTTP 客户端 | `idf_component.yml` 声明 + `core_http_config.h` |
| libmodbus | registry 组件 | registry | Modbus RTU/TCP | `idf_component.yml` 声明（宜 POSIX/RTOS） |
| FreeModbus | registry 组件 | registry | Modbus RTU 从站 | `idf_component.yml` 声明 + `mbport.h` |
| mbedtls | ESP-IDF 内置 | IDF 随附 | TLS / 密码学 | IDF 内置组件 + `mbedtls_config.h` |

### 2.3 存储与升级

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| SFUD | registry 组件 | registry | SPI Flash 统一驱动 | `idf_component.yml` 声明 + `sfud_cfg.h` |
| littlefs | registry 组件 | registry | 掉电安全文件系统 | `idf_component.yml` 声明 |
| FatFs | registry 组件 | registry | FAT/exFAT | `idf_component.yml` 声明 + `ffconf.h` |
| EasyFlash | registry 组件 | registry | Flash ENV/IAP | `idf_component.yml` 声明 |
| FlashDB | registry 组件 | registry | KV + 时序库 | `idf_component.yml` 声明 + `fdb_cfg.h` |
| MCUBoot | registry 组件 | registry | 安全 Boot / OTA | `idf_component.yml` 声明 |

### 2.4 人机与输入

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | `lvgl` / `esp_lvgl_port`（registry） | registry | 彩色 GUI | `idf_component.yml` 声明 + `lv_conf.h` |
| u8g2 | registry 组件 | registry | 单色/OLED | `idf_component.yml` 声明 |
| MultiButton | registry 组件 | registry | 多按键状态机 | `idf_component.yml` 声明 |

### 2.5 数据、日志与计算

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| cJSON | registry 组件 | registry | JSON | `idf_component.yml` 声明 |
| ETL | `lib/etl` | 20.48.1 | **上层 C++ 基础** | **默认进库** |
| nanopb | registry 组件 | registry | Protobuf | `idf_component.yml` 声明 |
| EasyLogger | registry 组件 | registry | 日志 | `idf_component.yml` 声明 |
| CMSIS-DSP | registry 组件 | registry | DSP | `idf_component.yml` 声明 |
| miniz | registry 组件 | registry | zlib 兼容压缩 | `idf_component.yml` 声明 |

---

## 3. 典型积木组合（示例）

| 产品形态 | 建议积木 |
| :--- | :--- |
| 裸机仪表 / 小屏 | OSAL_NULL + u8g2 或 LVGL + MultiButton + EasyLogger |
| 联网传感器 | FreeRTOS + lwIP + coreMQTT/coreHTTP + mbedtls + cJSON/nanopb |
| 带 SPI Flash 记录仪 | SFUD + littlefs 或 FlashDB + EasyLogger +（可选 miniz） |
| USB 大容量 / 网卡 | TinyUSB +（可选）FatFs / lwIP |
| 可 OTA 量产机 | MCUBoot + mbedtls（验签）+ 下载通道（USB/网络）+（可选 miniz） |
| 工控从站 | FreeModbus（RTU）或 libmodbus（POSIX） |

---

## 4. 怎么再接一块新积木

1. 在 ESP 工程 `idf_component.yml` 声明组件（`idf.py add-dependency` 或手动编辑）。
2. 需要板级 port 的（如显示、网络），在板级工程提供 port。
3. 更新 `README` / 本文档 / [`NOTICE`](../NOTICE)。

策略：**只接开源；依赖走 IDF 组件体系，不主动提交巨量源码。**

---

## 5. 和中间件核心的边界

- **可以**：在应用或板级服务里调用开源库 API；经 `device_*` / EventBus 与中间件协作。
- **不要**：在 `vfs/` / `bus/` 公共头强绑某个 GUI/TLS 实现，或把厂商 HAL typedef 泄漏进中间件公共 API。
- **南向**：Flash/显示/网卡仍通过板级 HAL 或 port 回调接触硬件，保持「硬件直投、中间件不绑 SDK」。

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
| libmodbus | Stéphane Raimbault 与贡献者 | Modbus 协议栈 |
| Mbed TLS | TrustedFirmware / Mbed-TLS | TLS 与密码学 |
| SFUD / EasyFlash / FlashDB / EasyLogger | armink 与贡献者 | Flash 与日志工具链 |
| littlefs | littlefs-project | 掉电安全文件系统 |
| FatFs | ChaN | 通用 FAT 文件系统 |
| MCUBoot | MCUBoot / Zephyr 等贡献者 | 安全启动与升级 |
| LVGL | kisvegabor 与 LVGL 社区 | 嵌入式 GUI |
| u8g2 | olikraus 与贡献者 | 单色显示库 |
| MultiButton | 0x1abin 与贡献者 | 按键状态机 |
| cJSON | Dave Gamble 与贡献者 | JSON 解析 |
| nanopb | Petteri Aimonen 与贡献者 | 嵌入式 Protobuf |
| ETL | John Wellbelove / ETLCPP | 无堆模板库 |
| CMSIS-DSP | Arm 与贡献者 | DSP 算法库 |
| coreHTTP | FreeRTOS / Amazon（含 llhttp） | 嵌入式 HTTP |
| miniz | Rich Geldreich 与贡献者 | zlib 兼容压缩 |
| FreeModbus | Christian Walter 与贡献者 | Modbus 从站 |

若遗漏署名或许可表述有误，欢迎提 Issue / PR 更正。完整版权与许可声明以各组件目录内文件及 [`NOTICE`](../NOTICE) 为准。
