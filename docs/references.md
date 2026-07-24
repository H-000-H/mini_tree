# 参考项目与外部对照

> 设计时心里对照过的外部项目：学什么、不学什么。  
> **不是依赖清单**，也不要求业务去链接这些工程。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 选型 / 移植 / 想理解「为什么长这样」的人 |
| **相关** | [design_decisions.md](design_decisions.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) |

---

## 目录

1. [一句话定位](#1-一句话定位)
2. [内核与 OS](#2-内核与-os)
3. [VFS / 设备模型](#3-vfs--设备模型)
4. [UI 与应用框架](#4-ui-与应用框架)
5. [对照表（和 mini_tree）](#5-对照表和-mini_tree)

---

## 1. 一句话定位

| 外部 | 我们主要借什么 |
| :--- | :--- |
| Linux | 设备树心智、`file_operations` 式 VFS、驱动与总线分层 |
| ESP-IDF / ESP32 | 在 RTOS 上叠一层统一 VFS/设备注册的工程结构 |
| FreeRTOS | 纯粹调度与 IPC，作 OSAL 首选后端 |
| RT-Thread | 组件丰富、可软绑定的中间件生态 |
| LVGL / Qt | **应用层以上** UI；不进入 board→vfs→bus→hal |

---

## 2. 内核与 OS

| 项目 | 角色 | 对本仓的启示 |
| :--- | :--- | :--- |
| [FreeRTOS](https://www.freertos.org/) | 嵌入式内核 | OSAL 首选；模型干净。ESP32 等平台已内置时，用平台自带内核，勿再嵌一份。 |
| [RT-Thread](https://www.rt-thread.org/) | 内核 + 软件包 | 组件多、绑定松；可作 OSAL 后端。设备框架与本仓并行时，**外设仍走 mini_tree**。 |
| Linux | 桌面/服务器 OS | 分层与 VFS 语义参考；**不**把内核驱动模型原样搬进 MCU。 |
| Zephyr | 嵌入式 OS + 自研 dts | **不推荐跟**；见 [design_decisions.md](design_decisions.md)「作者偏好」。本仓无 Zephyr 后端。 |

---

## 3. VFS / 设备模型

| 项目 | 看点 | 和 mini_tree 的差异 |
| :--- | :--- | :--- |
| **Linux VFS** | `file_operations`、inode/dentry、总线与驱动注册 | MCU 上无完整 VFS 内核；本仓是瘦封装：`device_*` + 静态 probe。 |
| **ESP-IDF VFS**（ESP32） | `esp_vfs_register`、fd 统一、UART/文件系统挂到同一套 open/read/write | 工程上「应用只认 VFS」的习惯值得学；本仓对应 `device.h` / vfs 驱动，不绑定 ESP 注册 API。 |
| **ESP-IDF 驱动栈** | HAL → driver → 可选 VFS | 近于本仓 `hal → bus → vfs`；本仓额外强调 **weak HAL + 平台强符号** 与 **编译期 dtc-lite**。 |
| **RT-Thread 设备框架** | `rt_device`、组件包 | 可并存；勿让业务同时走两套 `open`。 |

设备树：本仓学 Linux **源语法与属性契约**，用自研 **dtc-lite** 做编译期表，避免再维护一套运行期 `strcmp` probe。

---

## 4. UI 与应用框架

| 项目 | 建议落点 | 说明 |
| :--- | :--- | :--- |
| [LVGL](https://lvgl.io/) | 应用层以上 | 嵌入式 UI；经 OSAL 任务 + 本仓设备 I/O 取数，不 `#include` `hal_*`。产品侧按需 `mini_tree_link_lvgl`（见 [ecosystem.md](ecosystem.md)），**不进入** board→vfs→bus→hal 契约。 |
| [Qt](https://www.qt.io/) | 应用层以上（多在 MPU / 大资源） | 富 UI / 工具链；与 MCU 侧 mini_tree 通过协议或远端服务解耦，而非链进同一固件分层。 |

语言偏好（应用层 C++/Rust）见 [design_decisions.md](design_decisions.md)。

---

## 5. 对照表（和 mini_tree）

| 维度 | 常见外部做法 | mini_tree |
| :--- | :--- | :--- |
| 板级描述 | Linux dts；Zephyr dts+宏生成；ESP Kconfig+板头 | 通用占位 DTS + 平台 dtsi；**dtc-lite → 静态表** |
| 应用 I/O | Linux fd / ESP VFS fd / RTT `rt_device` | `device_*` + `file_operations` |
| 南向 HAL | 厂商库直调或厚封装 | 中立头 + weak `.c`，平台覆盖 |
| 调度 | FreeRTOS / RTT / Zephyr / 裸机 | OSAL 三后端（无 Zephyr） |
| UI | LVGL / Qt / 无 | **不在中间件核心契约内**；LVGL 可作为开源积木按需链接（[ecosystem.md](ecosystem.md)） |

---

## 相关文档

- [design_decisions.md](design_decisions.md) · [architecture.md](architecture.md) · [service_spec.md](service_spec.md) · [ecosystem.md](ecosystem.md)  
- [osal_switching.md](osal_switching.md) · [porting_guide.md](porting_guide.md)
