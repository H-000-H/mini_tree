# mini_tree 文档顶层摘要

> 分散文档集中处理后的双语导航。本文是**摘要**而非全文索引：每篇只给核心关键点与优先级，细节见交叉引用链接。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 所有人（尤其首次接入者） |
| **相关** | [docs/cn/README.md](README.md)（全量索引） · [README.md](../README.md)（仓库入口） |

---

## 0. 项目一句话

- **中文**：平台无关的嵌入式中间件，采用 Linux 风格设备树与驱动模型，统一裸机 / FreeRTOS / RT-Thread 上的外设访问；不绑定任何厂商 SDK。

关键术语保留原文：`Device Tree (DTS/DTSI)`、`DRIVER_REGISTER`、`dtc-lite`、`OSAL (NULL/FREERTOS/RTTHREAD)`、`VFS`、`BUS`、`HAL`、`EventBus`、`VIRQ`。

---

## 1. 按主题分类

### 1.1 入门与概念

| 文档 | 中文标题 / 关键点 | 优先级 | 链接 |
| :--- | :--- | :---: | :--- |
| `usage.md` | 术语表 + 阅读路线 | P2 | [cn](usage.md) |
| `getting_started.md` | 依赖（CMake ≥ 3.16、`lark`、`kconfiglib`）、Kconfig 双轨、CMake 集成、两段式点火、clangd | **P0** | [cn](getting_started.md) |
| `faq.md` | 常见问题（生成物重跑、`lark` 安装等） | P2 | [cn](faq.md) |
| `architecture.md` | 分层 `app→board→vfs→bus→hal(weak)→vendor`、数据流、启动时序 | **P0** | [cn](architecture.md) |
| `patterns.md` | 关键机制解剖：pre_execution 注册链 / 两段式点火 / 编译期 probe 表 / xtask 调度 / VIRQ 上下半部 / SPSC 无锁通道 / dev_lifecycle / 非阻塞状态机 | P1 | [cn](patterns.md) |
| `ecosystem.md` | 积木型链接：`lib/` 仅 vendor FreeRTOS/RT-Thread/ETL；TinyUSB/lwIP/cJSON 为 **config-time** FetchContent，其余为 link-time；已接入 22 个开源库版本清单 | **P0** | [cn](ecosystem.md) |

### 1.2 平台移植

| 文档 | 中文标题 / 关键点 | 优先级 | 链接 |
| :--- | :--- | :---: | :--- |
| `device_tree_porting.md` | **设备树移植大长文（集中版，含完整示例代码）**：dtc-lite 流水线、节点模板、板级 DTS、生成物详解、驱动对接、CMake 注入（`BOARD_DTS`/`BOARD_DTSI_DIR`/`MINI_TREE_BOARD_PORT`）、验证、排坑。实际移植通常只需改 DTS + HAL | **P0** | [cn](device_tree_porting.md) |
| `esp_idf_cmake.md` | ESP-IDF 组件式 CMake（对照 esp32s3）、Kconfig 双轨、验收清单 | **P0**（ESP 路径） | [cn](esp_idf_cmake.md) |
| `driver_guide.md` | DTS 布局、`dtc-lite` 流水线、`DRIVER_REGISTER`、compatible 属性、`board_*` 运行期 API、remove 生命周期 | **P0** | [cn](driver_guide.md) |
| `peripherals.md` | 外设 compatible / ioctl 一览 | P1 | [cn](peripherals.md) |
| `usb_tusb_port.md` | TinyUSB 板级契约（`usb_tusb_port`） | P1（USB） | [cn](usb_tusb_port.md) |
| `amp.md` | 双核 AMP（异构多核） | P2 | [cn](amp.md) |
| `osal_switching.md` | OSAL 后端切换（NULL/FREERTOS/RTTHREAD；优先级语义随后端变化） | P1 | [cn](osal_switching.md) |
| `esp_idf_notes.md` | ESP 修复记录 + ESP 特殊性 + 依赖策略 | P1（ESP） | [cn](esp_idf_notes.md) |

### 1.3 应用编写与编码

| 文档 | 中文标题 / 关键点 | 优先级 | 链接 |
| :--- | :--- | :---: | :--- |
| `service_spec.md` | 应用层允许/禁止；`device_find` 返回 `ERR_PTR` 须用 `IS_ERR` 判错；两段式启动挂载 | **P0** | [cn](service_spec.md) |
| `app_cpp_guide.md` | 应用层 C++ 限制（ETL 容器、编码分档、禁则） | P1（C++） | [cn](app_cpp_guide.md) |
| `coding_style.md` | `.clang-format`（LLVM/Allman/单语句去括号/`PointerAlignment: Left`/100 列）+ 分层 `.clang-tidy` + `compiler_compat_poison.h`（默认生效，靠 `ALLOW_*` 豁免） | **P0** | [cn](coding_style.md) |
| `runtime_services.md` | EventBus / VIRQ / SYSTEM_C·CPP / BufferPool | P1 | [cn](runtime_services.md) |
| `fast_path.md` | ISR / 热路径红线（禁 printf/mutex/malloc/长逻辑） | **P0**（驱动） | [cn](fast_path.md) |
| `can_hook.md` | CAN 协议超集钩子 | P2 | [cn](can_hook.md) |
| `memory_footprint.md` | 内存/flash 基准（flash 合计；与 CHANGELOG 的 RAM 下限口径不同）+ 裁剪开关 | P2 | [cn](memory_footprint.md) |
| `api_compatibility.md` | API 稳定面（稳定性承诺） | P2 | [cn](api_compatibility.md) |

### 1.4 调试、设计与历史

| 文档 | 中文标题 / 关键点 | 优先级 | 链接 |
| :--- | :--- | :---: | :--- |
| `debug_monitor.md` | 日志（`SYS_LOG*`/`DRV_LOG*`）、生成物、`compile_commands.json`、clangd | P1 | [cn](debug_monitor.md) |
| `keil_integration.md` | Keil Studio 支持 / 经典 µVision 不推荐 | P2（IDE） | [cn](keil_integration.md) |
| `design_decisions.md` | 仍生效的设计决策与作者偏好 | P1 | [cn](design_decisions.md) |
| `references.md` | 外部对照（ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt） | P2 | [cn](references.md) |
| `problem_summary.md` | 历史问题轴 | P2 | [cn](problem_summary.md) |

### 1.5 规划、索引与工具链

| 文档 | 中文标题 / 关键点 | 优先级 | 链接 |
| :--- | :--- | :---: | :--- |
| `file_index.md` | 源码导航（目录→模块映射） | P1 | [cn](file_index.md) |
| `roadmap.md` | 路线图 | P2 | [cn](roadmap.md) |
| `todolist.md` | 待办 | P2 | [cn](todolist.md) |
| `tools_guide.md` | `dtc-lite` / `genconfig` / `menuconfig` / `gen_compile_db` / scrubber stub 用法；`dtc-lite` 支持 `-I`/`-D` | **P0**（工具链） | [cn](tools_guide.md) |
| `board_linux_vs_device_model.md` | 通用机制：mini_tree 设备模型 vs Linux 对照 | P2 | [cn](board_linux_vs_device_model.md) |

---

## 2. 按优先级速查

- **P0（必须读）**：`getting_started.md` · `architecture.md` · `ecosystem.md` · `device_tree_porting.md` · `esp_idf_cmake.md`（ESP）· `driver_guide.md` · `service_spec.md` · `coding_style.md` · `fast_path.md` · `tools_guide.md`
- **P1（按需）**：`patterns.md` · `peripherals.md` · `usb_tusb_port.md` · `osal_switching.md` · `app_cpp_guide.md` · `runtime_services.md` · `debug_monitor.md` · `design_decisions.md` · `file_index.md`
- **P2（深入）**：`usage.md` · `faq.md` · `amp.md` · `can_hook.md` · `memory_footprint.md` · `api_compatibility.md` · `keil_integration.md` · `references.md` · `problem_summary.md` · `roadmap.md` · `todolist.md` · `board_linux_vs_device_model.md`

---

## 3. 关键事实速记

| 主题 | 中文 |
| :--- | :--- |
| 产品驱动 | 37 个，在 `drivers/<chip>/{include,src}`，GLOB 扫描；唯一树外例外 `driver_ws2812`（WHOLE_ARCHIVE） |
| OSAL 后端 | `CONFIG_OSAL_NULL`（裸机协作，默认）/ `FREERTOS`（v11.3.0）/ `RTTHREAD`（v5.3.0） |
| 目标架构 | Cortex-M0/M0+/M3/M4F/M7 · RISC-V 32-bit · 双核 AMP |
| 外设覆盖 | 总线层 6（SPI/I2C/I2S/UART/CAN/USB）· 无总线层 7（GPIO/ADC/DAC/TIM/RTC/IWDG/WWDG）· HAL-Only：AMP/Storage/Platform Safety/**SDIO（预留 reserved）** |
| 错误码 | `VFS_OK=0`；`VFS_ERR_*`（全名，见 `status.h`）；`device_find` 失败返回 `ERR_PTR` 而非 `NULL` |
| 构建 | CMake ≥ 3.16；`lark`（dtc-lite）；内置 `kconfiglib` 14.1.0；`ETL` 随仓 vendor 且默认链入 |

---

## 4. 文档写作约定

- 每篇专题含：标题+摘要、读者/前置、目录（长文）、正文（表格与命令优先）、相关文档链接。
- 路径与符号用反引号；错误码写 `VFS_ERR_*` 全名；技术术语保留英文原文（如 `Device Tree`、`OSAL`、`VFS`）。
- 新文档放 `docs/cn/` 与 `docs/en/` 双语；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING` 与法律文件。

---

## 相关文档

- [docs/cn/README.md](README.md)（全量索引） · [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md)
