# 语言规范（命名与格式）

> 统一中间件及上层 C/C++ 的命名与格式，风格基准 = Linux 内核 + Google C++。
> **app 层以下为强规定**（工具链强制）；**app 层及以上仅为建议**（新代码尽量遵守，不阻塞提交）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 中间件开发者、应用开发者 |
| **相关** | [architecture.md](architecture.md) · [service_spec.md](service_spec.md) · [../CONTRIBUTING.md](../CONTRIBUTING.md) |

---

## 0. 分层与强制程度

| 区域 | 范围 | 命名体系 | 强制程度 |
| :--- | :--- | :--- | :--- |
| **app 层** | `app/` | Google C++ 体系 + §2.2 前缀规则 | **建议** |
| **app 以下 · C** | `core/` `board/` `osal/` `hal/` `bus/` `vfs/` `drivers/` `algorithm/` `interrupt/` `can_hook/` `time_slice/` `system_c/` 等 | Linux 内核风格（§2.1） | **强规定** |
| **app 以下 · C++** | `system_cpp/` 等 `.cpp`/`.hpp` | Google C++ 体系 + §2.2 前缀规则 | **强规定** |

- **强规定**：提交前必须通过 §4 的 clang-format / clang-tidy 检查（CI 可设门禁）。
- **建议**：app 层走 Google 风格并保留 §2.2 前缀，便于与中间件对齐；不强制卡 CI。

---

## 1. 格式规则（全项目统一，app 以下强规定）

| 规则 | 取值 | 说明 |
| :--- | :--- | :--- |
| 大括号 | **Allman** | 函数/控制块左大括号独占一行 |
| 单语句 if/for/while | **不加括号** | `if (x) return y;` / 换行两行式均合法，禁止 `{ }` 包裹单语句 |
| 缩进 | 4 空格 | 不用 Tab |
| 列宽 | 100 | 超长折行 |
| 指针/引用 | 靠左 | `char* p` |
| include | 字母排序 | `SortIncludes` |

实现见根 [`.clang-format`](../.clang-format)（需 clang-format ≥ 15）。

---

## 2. 命名规则

### 2.1 内核区（app 以下 · C，强规定）

Linux 内核风格：**除宏外全小写，不加前缀**。

| 类别 | 规则 | 示例 |
| :--- | :--- | :--- |
| 函数 | 小写，模块名前缀 | `aht20_read` / `event_bus_post` |
| 变量 / 常量 | 小写（snake_case），无 `s_`/`g_`/`k_` 要求 | `s_bus` / `g_system_os_initialized` |
| struct / union / enum / typedef | 小写 | `struct event_bus` / `event_callback_t` |
| 宏（`#define`） | 全大写 | `K_TAG` / `CONFIG_*` |

### 2.2 Google 区（app 层 + `.cpp`/`.hpp`，app 以下强规定 / app 层建议）

| 类别 | 规则 | 示例 |
| :--- | :--- | :--- |
| class / struct / typedef / using 别名 | PascalCase | `class SystemCmd` / `CmdFn` |
| namespace | 小写 | `mini_tree` |
| 枚举值 | 大写 | `DEVICE_STATUS_READY` |
| 函数 / 变量 | 小写 + 作用域前缀 | `system_pre_os_init` / `s_bus` |
| 常量（含 `static const`） | `k_` 前缀 + 小写 | `k_max_cmd_name_len` / `k_aht20_map` |
| 宏（`#define`） | 全大写 | `K_QUEUE_LEN` |

前缀约定（Google 区）：`s_` = 文件内 static 符号（不含 const）；`g_` = 全局变量；`k_` = 常量（含 `static const`）。
> 说明：clang-tidy 无法区分文件作用域 `static` 与 `extern` 全局（均归 GlobalVariable），该处仅强制小写，`s_`/`g_` 前缀由 code review 把关。

### 2.3 例外（两区共用，保持内核风格原样）

以下内核风格内置宏 / API 名豁免大小写规则：

- 宏：`container_of` / `container_of_const` / `offsetof` / `typeof_member` / `likely` / `unlikely` / `pre_execution` / `__XXX_H__` 头文件卫士
- 函数式 API（static inline 实现）：`IS_ERR` / `PTR_ERR` / `ERR_PTR` / `IS_ERR_OR_NULL` / `COMPAT_*`

---

## 3. 分层配置文件

| 目录 | 文件 | 作用 |
| :--- | :--- | :--- |
| 根 | [`.clang-format`](../.clang-format) | 格式（全局） |
| 根 | [`.clang-tidy`](../.clang-tidy) | 内核区命名 |
| `app/` | [`.clang-tidy`](../app/.clang-tidy) | Google 区命名（建议） |
| `system_cpp/` | [`.clang-tidy`](../system_cpp/.clang-tidy) | Google 区命名（强规定） |
| 根 | [`.clang-format-ignore`](../.clang-format-ignore) | 格式化排除 `lib/` |

---

## 4. 检查命令

```bash
# 格式（app 以下提交前必过）
clang-format --style=file --dry-run <file>       # 检查
clang-format --style=file -i <file>              # 应用

# 命名
clang-tidy -p . <file.c>                          # C 源
clang-tidy -p . <file.h>                          # C 头
clang-tidy -p . <file.hpp> --extra-arg=-std=gnu++17  # C++ 头
```

`compile_commands.json` 由 `tools/gen_compile_db.py` 生成（含头文件条目）。clangd 会在编辑器中直接标注命名违规。

---

## 相关文档

- [architecture.md](architecture.md) · [service_spec.md](service_spec.md)（应用层允许/禁止）· [fast_path.md](fast_path.md)（热路径红线）
- [../CONTRIBUTING.md](../CONTRIBUTING.md)（贡献规约）· [../CHANGELOG.md](../CHANGELOG.md)
