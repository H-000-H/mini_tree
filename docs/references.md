# 参考项目与外部对照 / Reference Projects & External Comparisons

> 设计时心里对照过的外部项目：学什么、不学什么。
> External projects we compared against while designing: what to learn, what to skip.
>
> **不是依赖清单**，也不要求业务去链接这些工程。
> **Not a dependency list**, and nothing here requires products to link these projects.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 选型 / 移植 / 想理解「为什么长这样」的人 / Evaluators, porters, and anyone wondering "why is it shaped this way" |
| **相关 / Related** | [design_decisions.md](design_decisions.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) |

---

## 目录 / Contents

1. [一句话定位 / One-Line Positioning](#1-一句话定位-one-line-positioning)
2. [内核与 OS / Kernels & OSes](#2-内核与-os-kernels-oses)
3. [VFS / 设备模型 / VFS & Device Models](#3-vfs-设备模型-vfs-device-models)
4. [UI 与应用框架 / UI & Application Frameworks](#4-ui-与应用框架-ui-application-frameworks)
5. [对照表（和 mini_tree）/ Comparison Table (vs. mini_tree)](#5-对照表和-mini_tree-comparison-table-vs-mini_tree)

---

## 1. 一句话定位 / One-Line Positioning

| 外部 / External | 我们主要借什么 / What We Mainly Borrow |
| :--- | :--- |
| Linux | 设备树心智、`file_operations` 式 VFS、驱动与总线分层 / Device-tree mindset, `file_operations`-style VFS, driver & bus layering |
| ESP-IDF / ESP32 | 在 RTOS 上叠一层统一 VFS/设备注册的工程结构 / A project structure that layers unified VFS/device registration on top of an RTOS |
| FreeRTOS | 纯粹调度与 IPC，作 OSAL 首选后端 / Pristine scheduling & IPC, the preferred OSAL backend |
| RT-Thread | 组件丰富、可软绑定的中间件生态 / Rich, softly-bindable middleware ecosystem |
| LVGL / Qt | **应用层以上** UI；不进入 board→vfs→bus→hal / **Above-the-app-layer** UI; never enters board→vfs→bus→hal |

---

## 2. 内核与 OS / Kernels & OSes

| 项目 / Project | 角色 / Role | 对本仓的启示 / Lessons for This Repo |
| :--- | :--- | :--- |
| [FreeRTOS](https://www.freertos.org/) | 嵌入式内核 / Embedded kernel | OSAL 首选；模型干净。ESP32 等平台已内置时，用平台自带内核，勿再嵌一份。 / Preferred OSAL backend; clean model. When the platform (e.g. ESP32) already has one, use it — do not embed another. |
| [RT-Thread](https://www.rt-thread.org/) | 内核 + 软件包 / Kernel + packages | 组件多、绑定松；可作 OSAL 后端。设备框架与本仓并行时，**外设仍走 mini_tree**。 / Many components, loose binding; can serve as an OSAL backend. When its device framework runs in parallel, **peripherals still go through mini_tree**. |
| Linux | 桌面/服务器 OS / Desktop/server OS | 分层与 VFS 语义参考；**不**把内核驱动模型原样搬进 MCU。 / Reference for layering & VFS semantics; do **not** transplant its kernel driver model onto an MCU as-is. |
| Zephyr | 嵌入式 OS + 自研 dts / Embedded OS + own dts | **暂未接入**；本仓无 Zephyr 后端，取舍见 [design_decisions.md](design_decisions.md)「作者偏好」。 / **Not currently integrated**; this repo has no Zephyr backend — see "Author Preferences" in [design_decisions.md](design_decisions.md). |

---

## 3. VFS / 设备模型 / VFS & Device Models

| 项目 / Project | 看点 / What to Look At | 和 mini_tree 的差异 / Difference from mini_tree |
| :--- | :--- | :--- |
| **Linux VFS** | `file_operations`、inode/dentry、总线与驱动注册 / `file_operations`, inode/dentry, bus & driver registration | MCU 上无完整 VFS 内核；本仓是瘦封装：`device_*` + 静态 probe。 / No full VFS kernel on MCUs; this repo is a thin wrapper: `device_*` + static probe. |
| **ESP-IDF VFS**（ESP32）| `esp_vfs_register`、fd 统一、UART/文件系统挂到同一套 open/read/write / `esp_vfs_register`, unified fds, UART/filesystems behind one open/read/write | 工程上「应用只认 VFS」的习惯值得学；本仓对应 `device.h` / vfs 驱动，不绑定 ESP 注册 API。 / The "apps only see VFS" habit is worth learning; this repo maps to `device.h` / vfs drivers without binding ESP's registration API. |
| **ESP-IDF 驱动栈 / driver stack** | HAL → driver → 可选 VFS | 近于本仓 `hal → bus → vfs`；本仓额外强调 **weak HAL + 平台强符号** 与 **编译期 dtc-lite**。 / Close to this repo's `hal → bus → vfs`; this repo additionally stresses **weak HAL + platform strong symbols** and **compile-time dtc-lite**. |
| **RT-Thread 设备框架 / device framework** | `rt_device`、组件包 / `rt_device`, component packages | 可并存；勿让业务同时走两套 `open`。 / Can coexist; do not let apps go through two `open` systems at once. |

设备树：本仓学 Linux **源语法与属性契约**，用自研 **dtc-lite** 做编译期表，避免再维护一套运行期 `strcmp` probe。
Device tree: this repo learns Linux's **source syntax & property contracts**, then uses its own **dtc-lite** for compile-time tables — avoiding a runtime `strcmp` probe system.

---

## 4. UI 与应用框架 / UI & Application Frameworks

| 项目 / Project | 建议落点 / Suggested Placement | 说明 / Notes |
| :--- | :--- | :--- |
| [LVGL](https://lvgl.io/) | 应用层以上 / Above the app layer | 嵌入式 UI；经 OSAL 任务 + 本仓设备 I/O 取数，不 `#include` `hal_*`。产品侧按需 `mini_tree_link_lvgl`（见 [ecosystem.md](ecosystem.md)），**不进入** board→vfs→bus→hal 契约。 / Embedded UI; pulls data via OSAL tasks + this repo's device I/O, never `#include`s `hal_*`. Products call `mini_tree_link_lvgl` on demand (see [ecosystem.md](ecosystem.md)); it **stays out of** the board→vfs→bus→hal contract. |
| [Qt](https://www.qt.io/) | 应用层以上（多在 MPU / 大资源）/ Above the app layer (mostly MPU / rich resources) | 富 UI / 工具链；与 MCU 侧 mini_tree 通过协议或远端服务解耦，而非链进同一固件分层。 / Rich UI / tooling; decoupled from the MCU-side mini_tree via protocols or remote services, not linked into the same firmware layering. |

语言偏好（应用层 C++/Rust）见 [design_decisions.md](design_decisions.md)。
Language preferences (C++/Rust at the app layer) are in [design_decisions.md](design_decisions.md).

---

## 5. 对照表（和 mini_tree）/ Comparison Table (vs. mini_tree)

| 维度 / Dimension | 常见外部做法 / Common External Approach | mini_tree |
| :--- | :--- | :--- |
| 板级描述 / Board description | Linux dts；Zephyr dts+宏生成；ESP Kconfig+板头 / Linux dts; Zephyr dts+macrogen; ESP Kconfig+board headers | 通用占位 DTS + 平台 dtsi；**dtc-lite → 静态表** / Generic placeholder DTS + platform dtsi; **dtc-lite → static tables** |
| 应用 I/O / App I/O | Linux fd / ESP VFS fd / RTT `rt_device` | `device_*` + `file_operations` |
| 南向 HAL / Southbound HAL | 厂商库直调或厚封装 / Vendor-lib calls or thick wrappers | 中立头 + weak `.c`，平台覆盖 / Neutral headers + weak `.c`, overridden by the platform |
| 调度 / Scheduling | FreeRTOS / RTT / Zephyr / 裸机 / bare metal | OSAL 三后端（无 Zephyr）/ OSAL triple-backend (no Zephyr) |
| UI | LVGL / Qt / 无 / none | **不在中间件核心契约内**；LVGL 可作为开源积木按需链接（[ecosystem.md](ecosystem.md)）/ **Not part of the middleware core contract**; LVGL can be linked on demand as an open-source block ([ecosystem.md](ecosystem.md)) |

---

## 相关文档 / Related Documents

- [design_decisions.md](design_decisions.md) · [architecture.md](architecture.md) · [service_spec.md](service_spec.md) · [ecosystem.md](ecosystem.md)
- [osal_switching.md](osal_switching.md) · [porting_guide.md](porting_guide.md)
