# 文档目录 / Documentation Index

> 中间件专题文档入口。仓库根只保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与 `LICENSE` / `NOTICE`。
> Entry point for the middleware topic documents. The repository root keeps only `README` / `CHANGELOG` / `CONTRIBUTING` and `LICENSE` / `NOTICE`.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 所有人 / Everyone |
| **相关 / Related** | [README.md](../README.md) · [usage.md](usage.md) · [overview.html](overview.html)（视觉总览 / visual overview） |

---

## 怎么选文档 / How to Choose a Document

| 角色 / Role | 建议顺序 / Suggested Order |
| :--- | :--- |
| 第一次接入 / First-time integration | [overview.html](overview.html)（全文合集 / full-text collection）→ [getting_started.md](getting_started.md) → [usage.md](usage.md) → [faq.md](faq.md) |
| 选开源积木 / 扩生态 / Picking open-source bricks / extending the ecosystem | [ecosystem.md](ecosystem.md) |
| 平台移植 / Platform porting | [porting_guide.md](porting_guide.md) → [esp_idf_cmake.md](esp_idf_cmake.md)（若 ESP / if ESP）→ [usb_tusb_port.md](usb_tusb_port.md) → [amp.md](amp.md) → [driver_guide.md](driver_guide.md) |
| 写应用 / Writing applications | [service_spec.md](service_spec.md) → [peripherals.md](peripherals.md) → [runtime_services.md](runtime_services.md) → [fast_path.md](fast_path.md) |
| 写代码 / 查命名 / Writing code / checking naming | [coding_style.md](coding_style.md)（语言规范：app 以下强规定 / app 层建议；language rules: enforced below `app/`, recommended in `app/`） |
| 查设计动机 / Design rationale | [design_decisions.md](design_decisions.md) · [references.md](references.md) · [architecture.md](architecture.md) |
| 查文件 / File lookup | [file_index.md](file_index.md) |
| 合规 / 许可证 / Compliance / license | [../NOTICE](../NOTICE)（第三方清单与合规提示 / third-party list and compliance notes）· [../LICENSE](../LICENSE)（Apache-2.0 全文 / full text）· [ecosystem.md](ecosystem.md) §6（致谢 / acknowledgements） |

---

## 全部专题 / All Topics

### 入门 / Getting Started

| 文档 / Document | 说明 / Description |
| :--- | :--- |
| [overview.html](overview.html) | 全部专题 Markdown 全文合集（磨玻璃页内阅读；`.md` 源文件仍保留）/ Full-text collection of all topic Markdown (frosted-glass in-page reading; `.md` sources are kept) |
| [usage.md](usage.md) | 术语 + 阅读路线 / Terminology + reading paths |
| [getting_started.md](getting_started.md) | 依赖、Kconfig、CMake、点火 / Dependencies, Kconfig, CMake, ignition |
| [faq.md](faq.md) | 常见问题 / Frequently asked questions |

### 架构与移植 / Architecture & Porting

| 文档 / Document | 说明 / Description |
| :--- | :--- |
| [architecture.md](architecture.md) | 分层与数据流 / Layers and data flow |
| [ecosystem.md](ecosystem.md) | 积木型链接：已接入开源库清单与扩展方式 / Brick-style linking: integrated open-source libraries and how to extend |
| [design_decisions.md](design_decisions.md) | 仍生效的设计决策与作者偏好 / Design decisions still in force and author preferences |
| [references.md](references.md) | 外部对照：ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt / External references |
| [porting_guide.md](porting_guide.md) | 平台移植清单 / Platform porting checklist |
| [esp_idf_cmake.md](esp_idf_cmake.md) | ESP-IDF 组件式 CMake（对照 esp32s3）/ ESP-IDF component-style CMake (against esp32s3) |
| [driver_guide.md](driver_guide.md) | DTS / `DRIVER_REGISTER` |
| [peripherals.md](peripherals.md) | 外设 compatible / ioctl 一览 / Peripheral compatible / ioctl overview |
| [usb_tusb_port.md](usb_tusb_port.md) | TinyUSB 板级契约 / TinyUSB board-level contract |
| [amp.md](amp.md) | 双核 AMP / Dual-core AMP |
| [osal_switching.md](osal_switching.md) | OSAL 后端切换 / OSAL backend switching |

### 编码与运行时 / Coding & Runtime

| 文档 / Document | 说明 / Description |
| :--- | :--- |
| [coding_style.md](coding_style.md) | 语言规范：命名与格式（app 以下强规定 / app 层建议；enforced below `app/`, recommended in `app/`） |
| [service_spec.md](service_spec.md) | 应用层允许/禁止 / Application-layer do's and don'ts |
| [runtime_services.md](runtime_services.md) | EventBus / VIRQ / SYSTEM_C·CPP / 缓冲 / EventBus / VIRQ / SYSTEM_C·CPP / buffers |
| [can_hook.md](can_hook.md) | CAN 协议超集钩子 / CAN protocol superset hooks |
| [fast_path.md](fast_path.md) | ISR / 热路径红线 / ISR / hot-path red lines |
| [api_compatibility.md](api_compatibility.md) | API 稳定面 / API stability surface |

### 调试与历史 / Debugging & History

| 文档 / Document | 说明 / Description |
| :--- | :--- |
| [debug_monitor.md](debug_monitor.md) | 日志、生成物、clangd / Logging, generated artifacts, clangd |
| [keil_integration.md](keil_integration.md) | Keil / IDE |
| [problem_summary.md](problem_summary.md) | 历史问题轴 / Historical problem timeline |

### 规划与索引 / Planning & Index

| 文档 / Document | 说明 / Description |
| :--- | :--- |
| [file_index.md](file_index.md) | 源码导航 / Source navigation |
| [roadmap.md](roadmap.md) | 路线图 / Roadmap |
| [todolist.md](todolist.md) | 待办 / TODO list |

工具链说明见 [tools/README.md](../tools/README.md)。贡献规约见 [CONTRIBUTING.md](../CONTRIBUTING.md)。
Toolchain notes live in [tools/README.md](../tools/README.md). Contribution rules live in [CONTRIBUTING.md](../CONTRIBUTING.md).

---

## 文档写作约定 / Documentation Conventions

每篇专题尽量包含：
Each topic document should try to include:

1. **标题 + 一句话摘要 / Title + one-sentence summary**
2. **读者 / 前置知识 / Audience / prerequisites**
3. **目录（长文）/ Table of contents (for long documents)**
4. **正文（表格与可复制命令优先）/ Body (tables and copy-paste commands first)**
5. **相关文档（文末链接）/ Related documents (links at the end)**

路径与符号一律用反引号；错误码写 `VFS_ERR_*` 全名。新文档放到 `docs/`；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与法律文件。
Paths and symbols are always wrapped in backticks; error codes are written with full `VFS_ERR_*` names. New documents go into `docs/`; the root keeps only `README` / `CHANGELOG` / `CONTRIBUTING` and the legal files.

---

## 相关文档 / Related Documents

- [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md) · [ecosystem.md](ecosystem.md)
