# 文档目录

> 中间件专题文档入口。仓库根只保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与 `LICENSE` / `NOTICE`。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 所有人 |
| **相关** | [README.md](../README.md) · [SUMMARY.md](SUMMARY.md)（顶层双语摘要） · [usage.md](usage.md) |

---

## 怎么选文档

| 角色 | 建议顺序 |
| :--- | :--- |
| 第一次接入 | [README.md](README.md)（全量索引）→ [getting_started.md](getting_started.md) → [usage.md](usage.md) → [faq.md](faq.md) |
| 选开源积木 / 扩生态 | [ecosystem.md](ecosystem.md) |
| 平台移植 | [device_tree_porting.md](device_tree_porting.md) → [usb_tusb_port.md](usb_tusb_port.md) → [amp.md](amp.md) → [driver_guide.md](driver_guide.md) |
| 写应用 | [service_spec.md](service_spec.md) → [peripherals.md](peripherals.md) → [runtime_services.md](runtime_services.md) → [fast_path.md](fast_path.md) |
| 写代码 / 查命名 | [coding_style.md](coding_style.md)（语言规范：app 以下强规定，app 层建议） |
| 查设计动机 / 机制 | [design_decisions.md](design_decisions.md) · [patterns.md](patterns.md) · [references.md](references.md) · [architecture.md](architecture.md) |
| 查文件 | [file_index.md](file_index.md) |
| 合规 / 许可证 | [../NOTICE](../NOTICE)（第三方清单与合规提示）· [../LICENSE](../LICENSE)（Apache-2.0 全文）· [ecosystem.md](ecosystem.md) §6（致谢） |

---

## 全部专题

### 入门

| 文档 | 说明 |
| :--- | :--- |
| [README.md](README.md) | 全量文档索引（按主题 + 优先级组织） |
| [usage.md](usage.md) | 术语 + 阅读路线 |
| [getting_started.md](getting_started.md) | 依赖、Kconfig、CMake、点火 |
| [faq.md](faq.md) | 常见问题 |

### 架构与移植

| 文档 | 说明 |
| :--- | :--- |
| [architecture.md](architecture.md) | 分层与数据流 |
| [patterns.md](patterns.md) | 关键机制解剖：mini_pre_execution 注册链 / 两段式点火 / 编译期 probe 表 / xtask 调度 / VIRQ 上下半部 / SPSC 无锁通道 / dev_lifecycle / 非阻塞状态机 |
| [ecosystem.md](ecosystem.md) | 积木型链接：已接入开源库清单与扩展方式 |
| [design_decisions.md](design_decisions.md) | 仍生效的设计决策与作者偏好 |
| [references.md](references.md) | 外部对照：ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt |
| [device_tree_porting.md](device_tree_porting.md) | 平台移植清单 |
| [driver_guide.md](driver_guide.md) | DTS / `DRIVER_REGISTER` |
| [peripherals.md](peripherals.md) | 外设 compatible / ioctl 一览 |
| [usb_tusb_port.md](usb_tusb_port.md) | TinyUSB 板级契约 |
| [amp.md](amp.md) | 双核 AMP |
| [osal_switching.md](osal_switching.md) | OSAL 后端切换 |
| [net.md](net.md) | 网络协议栈胶水（MQTT / TCP / PPP / USB 网卡） |

### 编码与运行时

| 文档 | 说明 |
| :--- | :--- |
| [coding_style.md](coding_style.md) | 语言规范：命名与格式（app 以下强规定，app 层建议） |
| [app_cpp_guide.md](app_cpp_guide.md) | 应用层 C++ 限制与推荐（ETL 容器 / 编码分档 / 禁则） |
| [memory_footprint.md](memory_footprint.md) | 内存足迹：固定静态开销与裁剪开关 |
| [service_spec.md](service_spec.md) | 应用层允许/禁止 |
| [runtime_services.md](runtime_services.md) | EventBus / VIRQ / SYSTEM_C·CPP / 缓冲 |
| [can_hook.md](can_hook.md) | CAN 协议超集钩子 |
| [fast_path.md](fast_path.md) | ISR / 热路径红线 |
| [api_compatibility.md](api_compatibility.md) | API 稳定面 |

### 调试与历史

| 文档 | 说明 |
| :--- | :--- |
| [debug_monitor.md](debug_monitor.md) | 日志、生成物、clangd |
| [keil_integration.md](keil_integration.md) | Keil Studio（支持）/ µVision（不推荐）· IDE |
| [problem_summary.md](problem_summary.md) | 历史问题轴 |

### 规划与索引

| 文档 | 说明 |
| :--- | :--- |
| [file_index.md](file_index.md) | 源码导航 |
| [roadmap.md](roadmap.md) | 路线图 |
| [todolist.md](todolist.md) | 待办 |

工具链说明见 [tools_guide.md](tools_guide.md)。贡献规约见 [../CONTRIBUTING.md](../CONTRIBUTING.md)。

---

## 文档写作约定

每篇专题尽量包含：

1. **标题 + 一句话摘要**
2. **读者 / 前置知识**
3. **目录（长文）**
4. **正文（表格与可复制命令优先）**
5. **相关文档（文末链接）**

路径与符号一律用反引号；错误码写 `MINI_ERR_*` 全名。新文档放到 `docs/cn/` 与 `docs/en/`；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与法律文件。

---

## 相关文档

- [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md) · [ecosystem.md](ecosystem.md)
