# 编码风格

> 自动化优先：格式与命名交给 clang-format / clang-tidy；人工只盯「规范里没有的」。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 要改中间件代码、或做 IDE 跳转的人 |
| **前置** | [getting_started.md](getting_started.md)（clangd 章节） |
| **相关** | [architecture.md](architecture.md) · [faq.md](faq.md) |

---

## 目录

1. [自动化优先](#1-自动化优先)
2. [clang-format](#2-clang-format)
3. [clang-tidy](#3-clang-tidy)
4. [命名](#4-命名)
5. [头文件 / 包含](#5-头文件-包含)
6. [日志](#6-日志)
7. [危险 API 与 poison](#7-危险-api-与-poison)

---

## 1. 自动化优先

先从根 `compile_flags.txt` 找 clangd（否则 IDE 只有残缺索引）：

- `Clangd: Restart language server`
- 改了宏 / 配置 → 重跑 genconfig（见 [getting_started.md](getting_started.md)）

格式与命名：**交给工具**，不要手改。

---

## 2. clang-format

仓库根 `.clang-format`：

| 项 | 值 |
| :--- | :--- |
| BasedOnStyle | LLVM |
| 大括号 | Allman（换行） |
| 单语句去括号 | `true`（即单语句 `if/for/while` 也去掉 `{}`，见上例） |
| 缩进 | 4 空格 |
| 列宽 | 200 |
| 指针/引用对齐 | 左对齐（`int *p`） |

平台工程若想自定义，复制一份改 `ColumnLimit`，但 **不要** 破坏 Allman / 4 空格主干约定。

---

## 3. clang-tidy

仓库根 `.clang-tidy`：

| 项 | 值 |
| :--- | :--- |
| 检查集 | `bugprone-*`、`clang-analyzer-*`、`modernize-*`、`performance-*`、`readability-*` |
| 命名强制 | 小写（`x_task`、`x_scheduler`、`list_node`、`k_tag`、`struct event`、`mini_tree::` …） |

命名违规在 tidy 阶段报，不是编译错——CI 会拦。

---

## 4. 命名

统一小写（`readability-identifier-naming` 强制）：

| 类别 | 约定 |
| :--- | :--- |
| 函数 / 变量 | `snake_case` |
| 类型（`struct/typedef`） | 小写（`device`、`hal_can_config`） |
| 命名空间（`system_cpp`） | `mini_tree::` |
| 宏 / 枚举值 | 大写 `SNAKE_CASE`（`DEV_ID_UART0`、`VFS_ERR_*`） |

`app` 层为建议，app 以下为强规定。

---

## 5. 头文件 / 包含

- 每个 `.c` 对应一个 `.h`（除非纯内部模块）
- 头用 `#pragma once`（不用旧式 `#ifndef` 守卫）
- 包含顺序：自己的 `.h` → 系统/库 → 项目内
- 禁止裸 `.c` 互 `include`

---

## 6. 日志

- `SYS_LOGI/W/E` 系统级；`DRV_LOG*` 驱动级（见 [debug_monitor.md](debug_monitor.md)）
- 热路径只 `LOGD`/`LOGV`，勿刷 INFO
- 不把日志当调试断点塞满

---

## 7. 危险 API 与 poison

`core/include/compiler_compat_poison.h` 通过 `#pragma GCC poison` 禁用危险 API（完整列表见该头）；并非由某个独立 Kconfig 符号门控，而是**默认生效**，仅在定义了豁免宏时才放行：

| 豁免宏 | 放行的 poison |
| :--- | :--- |
| `ALLOW_HEAP_ALLOC` | `malloc` / `calloc` / `realloc` / `free`（以及 C++ `new` / `delete`） |
| `ALLOW_STDIO_OUTPUT` | `printf` / `fprintf` / `sprintf` / `vprintf` 等 stdio 输出 |

常用被 poison 的 API 与替代：

| 被 poison 的 API | 替代 |
| :--- | :--- |
| `malloc` / `free` / `calloc` / `realloc` | 静态池 / `bufferpool` / `kalloc` |
| `printf` / `fprintf` / `sprintf` | `SYS_LOG*` |
| 裸 `memcpy` / `memset` / `memmove` | `safe_mem*` 或显式长度校验 |
| `strcpy` / `strcat` / `strdup` / `strndup` | `safe_str*` |
| 文件 IO：`fopen` / `fclose` / `fread` / `fwrite` / `fseek` / `tmpfile` / `popen` / `gets` … | 走中间件/板级提供的 IO 接口 |

poison 只在受控翻译单元生效；HAL 强符号、平台裸机部分可能通过 `ALLOW_*` 宏豁免。

---

## 相关文档

- [getting_started.md](getting_started.md) · [faq.md](faq.md) · [debug_monitor.md](debug_monitor.md) · [service_spec.md](service_spec.md)
