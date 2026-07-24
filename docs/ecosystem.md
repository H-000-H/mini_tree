# 积木型开源生态

> mini_tree 中间件本体提供设备模型、VFS/Bus/HAL、OSAL 与运行时服务；**不把所有能力塞进核心**。  
> 能力扩展走 **积木型链接**：需要什么能力，就按需链入 `lib/` 里对应开源库，用板级 port 补齐配置与硬件胶水。
>
> **`lib/` 只承载开源生态**：当前已 vendoring 的组件均为可公开获取的开源项目（MIT / BSD / Apache / Zlib / FatFs 许可 / LGPL 等），**不接入需付费商业授权的闭源中间件**（例如 Qt Quick Ultralite）。许可证原文见各库目录及仓库 [`NOTICE`](../NOTICE)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台集成、应用开发、想扩展生态的人 |
| **相关** | [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [../README.md](../README.md) · [../NOTICE](../NOTICE) |

---

## 1. 为什么是「积木」

| 原则 | 含义 |
| :--- | :--- |
| **开源积木** | `lib/` 内均为开源项目；商用前请复核各库 `LICENSE`（如 libmodbus 为 LGPL） |
| **核心保持瘦** | 中间件不绑定厂商 SDK，也不强制带齐 GUI / TLS / 文件系统 |
| **按需链接** | 默认不把第三方编进固件；只有调用 `mini_tree_link_*`（或 OSAL Kconfig）时才进入镜像 |
| **源码落在 `lib/`** | 第三方以源码树形式 vendoring，版本可审计、可离线构建 |
| **CMake 一块积木一个入口** | 多数库有 `cmake/<name>.cmake`，提供 `mini_tree_link_<name>(target …)` |
| **板级补 port** | 配置头（如 `lv_conf.h`、`lwipopts.h`）与 diskio/SPI/显示 flush 等由平台提供 |

```
┌──────────────────────────────────────────────────────────┐
│  应用 / 产品策略（选积木：网络？GUI？OTA？存储？）          │
└────────────────────────────┬─────────────────────────────┘
                             │ mini_tree_link_* / Kconfig
┌────────────────────────────▼─────────────────────────────┐
│  lib/ 开源积木（本页清单） + cmake/*.cmake                 │
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

各位可以在此模型上继续接入更多**开源**库：放入 `lib/<Name>`，补一个 `cmake/<name>.cmake`，在产品 CMake 里 `mini_tree_link_*` 即可，无需改中间件分层契约。

---

## 2. 已接入的开源库

按能力分类。版本以仓库当前 vendoring 为准；链接方式见对应 `cmake/*.cmake`。  
下列路径均在 `lib/`，构成当前开源积木目录。

### 2.1 内核 / 调度

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| FreeRTOS | `lib/freeRTOS` | Kernel V11.3.0 | RTOS 内核 | `CONFIG_OSAL_FREERTOS` |
| RT-Thread | `lib/rtthread` | v5.3.0 | RTOS 内核 | `CONFIG_OSAL_RTTHREAD` |
| （裸机） | `time_slice/task` | — | 协作式调度 | `CONFIG_OSAL_NULL` |

### 2.2 连接与协议

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| TinyUSB | `lib/tinyusb` | 随仓 | USB 设备/主机栈 | 板级 `usb_tusb_port` |
| lwIP | `lib/lwip` | 2.2.1 | TCP/IP | `mini_tree_link_lwip` + `lwipopts.h` |
| coreMQTT | `lib/coreMQTT` | 5.0.2 | MQTT 客户端 | `mini_tree_link_coremqtt` + `core_mqtt_config.h` |
| libmodbus | `lib/libmodbus` | 3.2.0 | Modbus RTU/TCP | `mini_tree_link_libmodbus`（宜 POSIX/RTOS） |
| mbedtls | `lib/mbedtls` | 4.2.0 | TLS / 密码学 | `mini_tree_link_mbedtls` + `mbedtls_config.h` |

### 2.3 存储与升级

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| SFUD | `lib/SFUD` | 1.1.0 | SPI Flash 统一驱动 | `mini_tree_link_sfud` + `sfud_cfg.h` |
| littlefs | `lib/littlefs` | 2.11.3 | 掉电安全文件系统 | `mini_tree_link_littlefs` |
| FatFs | `lib/FatFs` | R0.16 | FAT/exFAT | `mini_tree_link_fatfs` + `ffconf.h` |
| EasyFlash | `lib/EasyFlash` | master (post-4.1) | Flash ENV/IAP | `mini_tree_link_easyflash` |
| FlashDB | `lib/FlashDB` | 2.2.0 | KV + 时序库 | `mini_tree_link_flashdb` + `fdb_cfg.h` |
| MCUBoot | `lib/mcuboot` | 2.4.0 | 安全 Boot / OTA 切换 | `mini_tree_link_mcuboot` |

### 2.4 人机与输入

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| LVGL | `lib/lvgl` | 9.5.0 | 彩色 GUI | `mini_tree_link_lvgl` + `lv_conf.h` |
| u8g2 | `lib/u8g2` | 2.37.1 | 单色/OLED（常 I2C） | `mini_tree_link_u8g2` |
| MultiButton | `lib/MultiButton` | 1.1.1 | 多按键状态机 | `mini_tree_link_multibutton` |

### 2.5 数据、日志与计算

| 库 | 路径 | 版本 | 作用 | 接入方式 |
| :--- | :--- | :--- | :--- | :--- |
| cJSON | `lib/cJSON` | 1.7.19 | JSON | `mini_tree_link_cjson` |
| nanopb | `lib/nanopb` | 0.4.9.1 | Protobuf | `mini_tree_link_nanopb` |
| ETL | `lib/etl` | 20.48.1 | 无堆 STL 风格容器 | `mini_tree_link_etl`（`SYSTEM_CPP`） |
| EasyLogger | `lib/EasyLogger` | 2.2.0 | 日志 | `mini_tree_link_easylogger` |
| CMSIS-DSP | `lib/CMSIS-DSP` | 1.17.1 | DSP | `mini_tree_link_cmsis_dsp`（MCU 宜设 `CMSISCORE`） |

---

## 3. 典型积木组合（示例）

| 产品形态 | 建议积木 |
| :--- | :--- |
| 裸机仪表 / 小屏 | OSAL_NULL + u8g2 或 LVGL + MultiButton + EasyLogger |
| 联网传感器 | FreeRTOS/RTT + lwIP + coreMQTT + mbedtls + cJSON/nanopb |
| 带 SPI Flash 记录仪 | SFUD + littlefs 或 FlashDB + EasyLogger |
| USB 大容量 / 网卡 | TinyUSB +（可选）FatFs / lwIP |
| 可 OTA 量产机 | MCUBoot + mbedtls（验签）+ 下载通道（USB/网络） |

---

## 4. 怎么再接一块新积木

1. 将**开源**上游源码放入 `lib/<Name>`（去掉嵌套 `.git`，与现有库一致）。  
2. 新增 `cmake/<name>.cmake`：默认 **不**链入；提供 `mini_tree_link_<name>(target …)`。  
3. 在根 `CMakeLists.txt` 中 `include` 该 cmake；`lib/CMakeLists.txt` / `README` / 本文档与 [`NOTICE`](../NOTICE) 登记一行。  
4. 产品工程提供 port（配置头 + 硬件回调），再调用 link 函数。  

第三方许可证以各库自带 `LICENSE` 及仓库 [`NOTICE`](../NOTICE) 为准；商用前请自行复核。本仓库生态策略：**不主动 vendoring 需单独商业授权的闭源组件**。

---

## 5. 和中间件核心的边界

- **可以**：在应用或板级服务里调用开源库 API；经 `device_*` / EventBus 与中间件协作。  
- **不要**：在 `vfs/` / `bus/` 公共头强绑某个 GUI/TLS 实现，或把厂商 HAL typedef 泄漏进中间件公共 API。  
- **南向**：Flash/显示/网卡仍通过板级 HAL 或 port 回调接触硬件，保持「硬件直投、中间件不绑 SDK」。

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

若遗漏署名或许可表述有误，欢迎提 Issue / PR 更正。完整版权与许可声明以各组件目录内文件及 [`NOTICE`](../NOTICE) 为准。
