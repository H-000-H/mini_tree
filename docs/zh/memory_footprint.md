# 内存与 flash 基准

> 编译产物大小与段分布（工具链相关）。生产构建务必打开 `CONFIG_SYS_LOG_LEVEL=0`（关日志）/ `CONFIG_BUILD_SIZE=1`（编大小）/ `CONFIG_BUILD_SIZE_REPORT=1`（出报告）；`CONFIG_BUILD_NO_LTO=0`（开 LTO，强烈推荐）。
>
> 报告脚本：`tools/build_size.py`。段布局用 `--format=html` 看分布，或 `--format=baseline` 看基线对照。详见 [getting_started.md](getting_started.md) §4.3。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 优化体积 / 评估成本 |
| **相关** | [getting_started.md](getting_started.md)（构建与度量）· [design_decisions.md](design_decisions.md)（裁剪偏好） |

---

## 1. 段布局

| 段 | 说明 |
| :--- | :--- |
| `text` | 代码与只读常数 |
| `rodata` | 只读常数 |
| `data` | 已初始化全局变量 |
| `bss` | 未初始化全局变量（不占 flash） |
| `err_section` | 错误符号表（`ERR_SECTION_BASE`）；`CONFIG_ERR_SECTION=1` 时单独放置——独立 ROM 区或 RAM 镜像 |
| `*.noinit` | WDT/RTC 等不被初始化的 RAM |
| `.log_*` | 日志注册表（关日志后移除） |

> WDT 与 `safe_state` 同属 `system`，在 `CONFIG_WDT=1` / `CONFIG_SAFE_STATE=1` 时编入；烧录后与中断、`err_section` 需保证物理不被覆盖。

---

## 2. 控制项

| Kconfig | 作用 |
| :--- | :--- |
| `CONFIG_SYS_LOG_LEVEL` | 0 = 关日志（默认编译保留） |
| `CONFIG_BUILD_SIZE` | 1 = 编大小（去掉调试信息） |
| `CONFIG_BUILD_SIZE_REPORT` | 1 = 出报告 |
| `CONFIG_BUILD_NO_LTO` | 0 = 开 LTO（推荐默认） |
| `CONFIG_ERR_SECTION` | 1 = 错误符号单独段（见 §1 表） |
| `CONFIG_WDT` / `CONFIG_SAFE_STATE` | WDT / safe_state 编入（见 §1 表） |

---

## 3. 基准

> 单位 KiB。示例数字，随重构与新积木会变化（周期性重测）。

| 配置 | text | rodata | data | bss | flash 合计 | 说明 |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| 最小（仅 `osal` + 框架） | 6.2 | 1.1 | 0.3 | 2.4 | 7.6 | `CONFIG_OSAL_NULL` + 空板 |
| + 设备模型 | 11.8 | 2.0 | 0.6 | 4.1 | 13.8 | `board/` 全部 |
| + 一个 VFS 设备（uart） | 15.3 | 2.6 | 0.8 | 5.2 | 18.7 | `vfs/uart` |
| + FreeRTOS 后端 | 19.1 | 3.3 | 1.1 | 6.9 | 23.5 | `CONFIG_OSAL_FREERTOS` |
| + WDT + safe_state | 20.4 | 3.5 | 1.2 | 7.3 | 24.9 | `CONFIG_WDT` + `CONFIG_SAFE_STATE` |

> 上表为 GCC `-Os` + LTO 估算。开日志（`CONFIG_SYS_LOG_LEVEL>0`）各档增 ~3–8 KiB `rodata`/`text`；关日志最划算。

---

## 4. 裁剪建议

1. 关日志（`CONFIG_SYS_LOG_LEVEL=0`）——单条 `LOG_*` 宏即占空间，关掉省最多。
2. 开 LTO（`CONFIG_BUILD_NO_LTO=0`）——链接期合并重复、去死代码。
3. 仅选 `CONFIG_OSAL_NULL` 后端（裸机）时最省，但需自己实现调度。
4. 不要编入不用的 VFS / HAL：依赖由 CMake 源集合决定，未引用即不进二进制。
5. `err_section` 仅在确有独立 ROM 区 / 诊断需求时开 `CONFIG_ERR_SECTION=1`。

---

## 相关文档

- [getting_started.md](getting_started.md) · [design_decisions.md](design_decisions.md)
