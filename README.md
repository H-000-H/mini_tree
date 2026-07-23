# mini_tree: Pure Generic & Architecture-Isolated Embedded Middleware

> 平台无关嵌入式中间件 — Linux 风格设备树 · 编译期 probe · 弱符号 HAL · 虚拟中断。  
> **本仓库不绑定厂商 SDK**；板级 `DTS/DTSI` 与 `hal_*_<soc>.c` 由平台工程提供。

| 项目 | 说明 |
| :--- | :--- |
| 许可证 | [Apache-2.0](LICENSE)（见各文件 SPDX；第三方见 [`NOTICE`](NOTICE) / `lib/`） |
| 构建 | CMake 静态库 `mini_tree` |
| IDE | 打开**仓库根** + clangd（`compile_flags.txt` / `ide/stubs`） |

---

## 这份文档怎么读

| 你想… | 去看 |
| :--- | :--- |
| 5 分钟搞清是什么、文档地图 | 下文「适用场景 / 文档索引」或 [docs/README.md](docs/README.md) · [docs/overview.html](docs/overview.html) |
| 动手接入 | [docs/getting_started.md](docs/getting_started.md) |
| 弄懂分层与启动 | [docs/architecture.md](docs/architecture.md) · [docs/usage.md](docs/usage.md) |
| 移植一块板 | [docs/porting_guide.md](docs/porting_guide.md) · [docs/driver_guide.md](docs/driver_guide.md) |
| 写业务代码 | [docs/service_spec.md](docs/service_spec.md) · [docs/peripherals.md](docs/peripherals.md) · [docs/fast_path.md](docs/fast_path.md) |
| 查符号 / 文件 | [docs/file_index.md](docs/file_index.md) |

---

## 适用场景

| 底层环境 | 推荐 | mini_tree 做什么 |
| :--- | :---: | :--- |
| 裸机 | ✓ | `CONFIG_OSAL_NULL` + `xtask`、设备树 Probe、EventBus、BufferPool、安全回路 |
| FreeRTOS | ✓ | 在内核之上提供统一设备模型与 VFS/Bus/HAL |
| RT-Thread | ✓ | OSAL 垫片；驱动仍走本仓库，不混用两套设备模型 |
| ESP-IDF / Cube 等 | 平台侧 | 中间件**不** `#include` 厂商 HAL；平台提供实现与 DTS |

---

## 集成内核 / 可选依赖

| 组件 | 路径 | 版本 | 何时编入 |
| :--- | :--- | :--- | :--- |
| FreeRTOS | `lib/freeRTOS` | Kernel V11.3.0 | `CONFIG_OSAL_FREERTOS` |
| RT-Thread | `lib/rtthread` | v5.3.0 | `CONFIG_OSAL_RTTHREAD` |
| TinyUSB | `lib/tinyusb` | 随仓库附带 | USB 场景；板级 `usb_tusb_port` |
| ETL | `cmake/etl.cmake` | 拉取/本地 | `SYSTEM_CPP` 等 C++ 路径 |
| 裸机调度 | `time_slice/task` | — | `CONFIG_OSAL_NULL` |

---

## 架构一览

```
Application  ──device_* / ioctl──►  board/  ──DRIVER_REGISTER──►  vfs/
                                                          │
                                                          ▼
                                                     bus/ ──► hal/(weak) ──► 平台 HAL / 厂商 SDK

横向支撑: core · osal · interrupt · system_c|cpp · can_hook · tools(dtc-lite, genconfig)
```

要点（详见 [docs/architecture.md](docs/architecture.md)）：

1. **硬件直投**：DTSI 里的厂商宏经 cpp 展开后写入配置结构体，HAL 不做二次 enum 映射。  
2. **编译期 probe**：`DRIVER_REGISTER` + `dtc-lite` 生成静态表，运行期无 `strcmp`。  
3. **南向隔离**：Bus 头对上层 `poison` `hal_*`；HAL 头无厂商 typedef。  

---

## 文档索引

根目录只放入口与开源惯例文件；专题一律在 [`docs/`](docs/README.md)。工具说明在 [`tools/README.md`](tools/README.md)。

### 入门

| 文档 | 内容 |
| :--- | :--- |
| [docs/usage.md](docs/usage.md) | 术语表 + 阅读路线 |
| [docs/overview.html](docs/overview.html) | 视觉总览（磨玻璃 / 架构动画 / 章节跳转） |
| [docs/getting_started.md](docs/getting_started.md) | 依赖、配置、CMake 集成、点火时序 |
| [docs/faq.md](docs/faq.md) | 常见问题 |

### 架构与移植

| 文档 | 内容 |
| :--- | :--- |
| [docs/architecture.md](docs/architecture.md) | 分层、数据流、安全、配置 |
| [docs/design_decisions.md](docs/design_decisions.md) | 架构决策与作者偏好取舍 |
| [docs/references.md](docs/references.md) | 外部对照（ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt） |
| [docs/porting_guide.md](docs/porting_guide.md) | 平台 HAL / DTS 移植清单 |
| [docs/esp_idf_cmake.md](docs/esp_idf_cmake.md) | ESP-IDF 特殊 CMake 集成 |
| [docs/driver_guide.md](docs/driver_guide.md) | 设备树契约与 `DRIVER_REGISTER` |
| [docs/peripherals.md](docs/peripherals.md) | 外设 compatible / ioctl 一览 |
| [docs/usb_tusb_port.md](docs/usb_tusb_port.md) | TinyUSB 板级 `usb_tusb_*` 契约 |
| [docs/amp.md](docs/amp.md) | 双核 AMP |
| [docs/osal_switching.md](docs/osal_switching.md) | 切换 RTOS 后端注意点 |

### 编码与兼容

| 文档 | 内容 |
| :--- | :--- |
| [docs/service_spec.md](docs/service_spec.md) | 应用/服务层允许与禁止 |
| [docs/runtime_services.md](docs/runtime_services.md) | EventBus / VIRQ / SYSTEM_C·CPP |
| [docs/can_hook.md](docs/can_hook.md) | CAN 协议超集钩子 |
| [docs/fast_path.md](docs/fast_path.md) | 硬实时 / ISR 红线 |
| [docs/api_compatibility.md](docs/api_compatibility.md) | 稳定面与非保证项 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献与 PR 规约 |

### 调试与工具

| 文档 | 内容 |
| :--- | :--- |
| [docs/debug_monitor.md](docs/debug_monitor.md) | 日志、生成物、clangd |
| [docs/keil_integration.md](docs/keil_integration.md) | IDE 立场：不推荐 Keil；推荐现代工具 |
| [docs/problem_summary.md](docs/problem_summary.md) | 历史问题排查轴 |
| [tools/README.md](tools/README.md) | dtc-lite / genconfig / scrubber |

### 规划与索引

| 文档 | 内容 |
| :--- | :--- |
| [docs/file_index.md](docs/file_index.md) | 目录与关键文件索引 |
| [docs/roadmap.md](docs/roadmap.md) · [docs/todolist.md](docs/todolist.md) | 规划与待办 |
| [CHANGELOG.md](CHANGELOG.md) | 变更记录 |
| [LICENSE](LICENSE) · [NOTICE](NOTICE) | 许可证全文 / 第三方归属 |

---

## 文档写作约定（本仓库）

每篇专题尽量包含：

1. **标题 + 一句话摘要**  
2. **读者 / 前置知识**  
3. **目录**（长文）  
4. **正文**（表格与可复制命令优先）  
5. **相关文档**（文末链接）  

路径与符号一律用 `` `反引号` ``；错误码写 `VFS_ERR_*` 全名。  
新文档放到 `docs/`；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与法律文件。

---

## License

全文见 [LICENSE](LICENSE)（Apache License 2.0）。源文件 SPDX 头与之对应。  
第三方归属见 [NOTICE](NOTICE)；组件原文许可证在 `lib/`。架构决策见 [docs/design_decisions.md](docs/design_decisions.md)。
