# 参考与外部资源

> 外部标准、上游仓库与工具链文档。中间件内部文档见 [file_index.md](file_index.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 深入 / 溯源 |
| **相关** | [file_index.md](file_index.md) · [ecosystem.md](ecosystem.md) |

---

## 1. 设备树

| 资源 | 说明 |
| :--- | :--- |
| 设备树规范（Devicetree Specification） | `dtc-lite` 的语义来源 |
| `dt-bindings/` 约定 | 中间件通用宏命名 |
| `board/dtsi/example-soc.dtsi` | 通用示例节点模板 |

---

## 2. RTOS 与 OSAL

| 资源 | 说明 |
| :--- | :--- |
| FreeRTOS 官方文档 | `CONFIG_OSAL_FREERTOS` 后端参考 |
| OSAL 两后端设计 | `osal/src/osal_{null,freertos}.c` |

---

## 3. 上游积木（Fetch）

| 资源 | 说明 |
| :--- | :--- |
| TinyUSB | USB 设备/主机栈（`lib/tinyusb`，Fetch） |
| lwIP | 网络栈（Fetch） |
| cJSON | JSON 解析（Fetch） |
| ETL | 嵌入式模板库（`lib/etl`，已 vendor） |
| FreeRTOS / RT-Thread | 已 vendor（见 [ecosystem.md](ecosystem.md)） |

---

## 4. 工具链

| 资源 | 说明 |
| :--- | :--- |
| ARMCLANG (AC6) | 推荐编译器（不支持 ARMCC v5） |
| clangd | 编辑 / 索引（见 [getting_started.md](getting_started.md) §7） |
| CMake / ESP-IDF | 构建系统 |
| VSCode 系（VS Code / Cursor / Qoder） | 主 IDE，配 clangd |

---

## 5. 规范与约定

| 资源 | 说明 |
| :--- | :--- |
| `.clang-format` / `.clang-tidy` | 代码风格 |
| `CONTRIBUTING.md` | 贡献流程 |
| `Kconfig` | 配置菜单 |

---

## 相关文档

- [file_index.md](file_index.md) · [ecosystem.md](ecosystem.md) · [getting_started.md](getting_started.md)
