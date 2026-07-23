# 文档目录

> 中间件专题文档入口。仓库根只保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与 `LICENSE` / `NOTICE`。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 所有人 |
| **相关** | [README.md](../README.md) · [usage.md](usage.md) · [overview.html](overview.html)（视觉总览） |

---

## 怎么选文档

| 角色 | 建议顺序 |
| :--- | :--- |
| 第一次接入 | [overview.html](overview.html)（全文合集）→ [getting_started.md](getting_started.md) → [usage.md](usage.md) → [faq.md](faq.md) |
| 平台移植 | [porting_guide.md](porting_guide.md) → [esp_idf_cmake.md](esp_idf_cmake.md)（若 ESP）→ [usb_tusb_port.md](usb_tusb_port.md) → [amp.md](amp.md) → [driver_guide.md](driver_guide.md) |
| 写应用 | [service_spec.md](service_spec.md) → [peripherals.md](peripherals.md) → [runtime_services.md](runtime_services.md) → [fast_path.md](fast_path.md) |
| 查设计动机 | [design_decisions.md](design_decisions.md) · [references.md](references.md) · [architecture.md](architecture.md) |
| 查文件 | [file_index.md](file_index.md) |

---

## 全部专题

### 入门

| 文档 | 说明 |
| :--- | :--- |
| [overview.html](overview.html) | 全部专题 Markdown 全文合集（磨玻璃页内阅读；`.md` 源文件仍保留） |
| [usage.md](usage.md) | 术语 + 阅读路线 |
| [getting_started.md](getting_started.md) | 依赖、Kconfig、CMake、点火 |
| [faq.md](faq.md) | 常见问题 |

### 架构与移植

| 文档 | 说明 |
| :--- | :--- |
| [architecture.md](architecture.md) | 分层与数据流 |
| [design_decisions.md](design_decisions.md) | 仍生效的设计决策与作者偏好 |
| [references.md](references.md) | 外部对照：ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt |
| [porting_guide.md](porting_guide.md) | 平台移植清单 |
| [esp_idf_cmake.md](esp_idf_cmake.md) | ESP-IDF 组件式 CMake（对照 esp32s3） |
| [driver_guide.md](driver_guide.md) | DTS / `DRIVER_REGISTER` |
| [peripherals.md](peripherals.md) | 外设 compatible / ioctl 一览 |
| [usb_tusb_port.md](usb_tusb_port.md) | TinyUSB 板级契约 |
| [amp.md](amp.md) | 双核 AMP |
| [osal_switching.md](osal_switching.md) | OSAL 后端切换 |

### 编码与运行时

| 文档 | 说明 |
| :--- | :--- |
| [service_spec.md](service_spec.md) | 应用层允许/禁止 |
| [runtime_services.md](runtime_services.md) | EventBus / VIRQ / SYSTEM_C·CPP / 缓冲 |
| [can_hook.md](can_hook.md) | CAN 协议超集钩子 |
| [fast_path.md](fast_path.md) | ISR / 热路径红线 |
| [api_compatibility.md](api_compatibility.md) | API 稳定面 |

### 调试与历史

| 文档 | 说明 |
| :--- | :--- |
| [debug_monitor.md](debug_monitor.md) | 日志、生成物、clangd |
| [keil_integration.md](keil_integration.md) | Keil / IDE |
| [problem_summary.md](problem_summary.md) | 历史问题轴 |

### 规划与索引

| 文档 | 说明 |
| :--- | :--- |
| [file_index.md](file_index.md) | 源码导航 |
| [roadmap.md](roadmap.md) | 路线图 |
| [todolist.md](todolist.md) | 待办 |

工具链说明见 [README.md](../tools/README.md)。贡献规约见 [CONTRIBUTING.md](../CONTRIBUTING.md)。

---

## 相关文档

- [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md)
