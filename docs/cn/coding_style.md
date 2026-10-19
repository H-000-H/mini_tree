# 编码风格

> 本项目就是 **Linux 内核代码风格**：仓库根 `.clang-format` 是唯一事实来源（内容即内核官方配置）。
> 格式化一条命令 `bash use_clang_format.sh`；IDE 保存时同样按它执行，所以**别手改格式**。

| 项 | 值 | 一眼看出来的样子 |
| :--- | :--- | :--- |
| 缩进 | Tab，宽度 8 | 一级缩进 = 一个 Tab；纵向对齐用 Tab，尾端补空格 |
| 列宽 | 100 | 超长即折行，续行再缩进 8 |
| 括号 | 函数 `{` 换行；`if/for/while` 的 `{` 不换行 | `BreakBeforeBraces: Linux` |
| 单语句 | 不加 `{}`，也不压成一行 | `if (err)` 换行 `return -1;` |
| 指针 | 靠右 | `char *p`、`void (*fn)(int)` |
| 连续声明 / 赋值 | 对齐 | `node`、`ops` 字段竖着排齐 |
| 尾注 | 对齐，前面 1 空格 | `/**< ... */` |
| `extern "C"` | 块内不缩进 | `extern "C" {` |

## 命名（clang-format 管不了，clang-tidy 强制）

`readability-identifier-naming` 强制；全小写 `snake_case`，唯一大写是宏：

| 类别 | 约定 | 例 |
| :--- | :--- | :--- |
| 函数 / 变量 / 类型 | 小写 | `device_open`、`struct device`、`device_id_t` |
| C++ 类 | `PascalCase` | `SystemCmd` |
| 宏 / 枚举值 | 大写 | `MINI_ERR_INVAL`、`DEV_ID_UART0` |
| 前缀 | `s_` 文件内 static、`g_` 全局、`k_` 常量 | `s_uart_pool`、`k_map` |

`app` 层为建议，`app` 以下为强规定。

## 头文件

- 每个 `.c` 一个 `.h`（纯内部模块除外），一律 `#pragma once`
- 包含顺序：自己的 `.h` → 系统/库 → 项目内；`compiler_compat_poison.h` **固定最后**
- 禁止裸 `.c` 互 `include`
- 危险 API（malloc / printf / strcpy / memcpy …）由 poison 默认禁用，需 `ALLOW_HEAP_ALLOC` / `ALLOW_STDIO_OUTPUT` 豁免，完整列表见 `core/include/compiler_compat_poison.h`

## 日志

- `MT_LOG_ERROR/WARN/INFO` 系统级；`MT_DRV_LOG_*` 驱动级（见 [debug_monitor.md](debug_monitor.md)）
- 热路径只 `LOGD`/`LOGV`，勿刷 INFO

## 相关文档

- [getting_started.md](getting_started.md)（clangd 配置）· [driver_guide.md](driver_guide.md) · [faq.md](faq.md)
