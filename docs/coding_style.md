# 语言规范 / Coding Style（命名与格式 / Naming & Formatting）

> 统一中间件及上层 C/C++ 的命名与格式，风格基准 = Linux 内核 + Google C++。
> Unified naming & formatting for the middleware and upper C/C++, based on Linux kernel + Google C++ styles.
> **app 层以下为强规定**（工具链强制）；**app 层及以上仅为建议**（新代码尽量遵守，不阻塞提交）。
> **Mandatory below the app layer** (tool-enforced); **advisory in the app layer and above** (follow when possible; never blocks commits).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 中间件开发者、应用开发者 / Middleware & app developers |
| **相关 / Related** | [architecture.md](architecture.md) · [service_spec.md](service_spec.md) · [../CONTRIBUTING.md](../CONTRIBUTING.md) |

---

## 0. 分层与强制程度 / Layers & Enforcement

| 区域 / Zone | 范围 / Scope | 命名体系 / Naming | 强制程度 / Enforcement |
| :--- | :--- | :--- | :--- |
| **app 层 / app layer** | `app/` | Google C++ 体系 + §2.2 前缀规则 / Google + §2.2 prefixes | **建议 / Advisory** |
| **app 以下 · C / Below app · C** | `core/` `board/` `osal/` `hal/` `bus/` `vfs/` `drivers/` `algorithm/` `interrupt/` `can_hook/` `time_slice/` `system_c/` 等 | Linux 内核风格（§2.1）/ Linux kernel (§2.1) | **强规定 / Mandatory** |
| **app 以下 · C++ / Below app · C++** | `system_cpp/` 等 `.cpp`/`.hpp` | Google C++ 体系 + §2.2 前缀规则 / Google + §2.2 prefixes | **强规定 / Mandatory** |

- **强规定 / Mandatory**：提交前必须通过 §4 的 clang-format / clang-tidy 检查（CI 可设门禁）。
  Must pass the §4 clang-format / clang-tidy checks before committing (CI may gate).
- **建议 / Advisory**：app 层走 Google 风格并保留 §2.2 前缀，便于与中间件对齐；不强制卡 CI。
  The app layer follows Google style with §2.2 prefixes for middleware alignment; not CI-gated.

---

## 1. 格式规则 / Formatting Rules（全项目统一，app 以下强规定 / global; mandatory below app）

| 规则 / Rule | 取值 / Value | 说明 / Notes |
| :--- | :--- | :--- |
| 大括号 / Braces | **Allman** | 函数/控制块左大括号独占一行 / opening brace on its own line |
| 单语句 if/for/while / Single-statement | **不加括号 / No braces** | `if (x) return y;` 或两行式均合法，禁止 `{ }` 包裹单语句 / either one-line or two-line, never wrap a single statement in `{}` |
| 缩进 / Indent | 4 空格 / spaces | 不用 Tab / no tabs |
| 列宽 / Column limit | 100 | 超长折行 / wrap over-long lines |
| 短函数 / Short functions | **允许一行化 / one-line allowed** | `AllowShortFunctionsOnASingleLine: All`：能在一行放下的函数不允许多行 / functions that fit must stay on one line |
| 指针/引用 / Pointers & refs | 靠左 / left | `char* p` |
| include | 字母排序（`compiler_compat_poison.h` 固定最后）/ sorted (`compiler_compat_poison.h` pinned last) | `SortIncludes` + `IncludeCategories` |

实现见根 [`.clang-format`](../.clang-format)（需 clang-format ≥ 15）。
Implemented by the root [`.clang-format`](../.clang-format) (requires clang-format ≥ 15).

---

## 2. 命名规则 / Naming Rules

> **指针显式化 / Explicit pointers**：指针变量/参数一律以 `p` 前缀标明，如 `struct device* pdev`（不写裸 `dev`）。两区通用。
> Pointer variables/parameters carry a `p` prefix to make the pointer explicit, e.g. `struct device* pdev` (never bare `dev`). Applies to both zones.

### 2.1 内核区 / Kernel Zone（app 以下 · C，强规定 / below app · C, mandatory）

Linux 内核风格：**除宏外全小写，不加前缀**。
Linux kernel style: **all-lowercase except macros, no prefixes**.

| 类别 / Kind | 规则 / Rule | 示例 / Example |
| :--- | :--- | :--- |
| 函数 / Functions | 小写，模块名前缀 / lowercase, module prefix | `aht20_read` / `event_bus_post` |
| 变量 / 常量 / Variables & constants | 小写（snake_case），无 `s_`/`g_`/`k_` 要求 / lowercase, no prefix requirement | `s_bus` / `g_system_os_initialized` |
| struct / union / enum / typedef | 小写 / lowercase | `struct event_bus` / `event_callback_t` |
| 宏（`#define`）/ Macros | 全大写 / UPPER_CASE | `K_TAG` / `CONFIG_*` |

### 2.2 Google 区 / Google Zone（app 层 + `.cpp`/`.hpp`，app 以下强规定 / app 层建议）

| 类别 / Kind | 规则 / Rule | 示例 / Example |
| :--- | :--- | :--- |
| class / struct / typedef / using 别名 / aliases | PascalCase | `class SystemCmd` / `CmdFn` |
| namespace | 小写 / lowercase | `mini_tree` |
| 枚举值 / Enumerators | 大写 / UPPER_CASE | `DEVICE_STATUS_READY` |
| 函数 / 变量 / Functions & variables | 小写 + 作用域前缀 / lowercase + scope prefixes | `system_pre_os_init` / `s_bus` |
| 常量（含 `static const`）/ Constants | `k_` 前缀 + 小写 / `k_` prefix + lowercase | `k_max_cmd_name_len` / `k_aht20_map` |
| 宏（`#define`）/ Macros | 全大写 / UPPER_CASE | `K_QUEUE_LEN` |

前缀约定（Google 区）/ Prefixes (Google zone)：`s_` = 文件内 static 符号（不含 const）/ file-local static (not const)；`g_` = 全局变量 / globals；`k_` = 常量（含 `static const`）/ constants (incl. `static const`).
> 说明：clang-tidy 无法区分文件作用域 `static` 与 `extern` 全局（均归 GlobalVariable），该处仅强制小写，`s_`/`g_` 前缀由 code review 把关。
> Note: clang-tidy cannot tell file-scope `static` from `extern` globals (both classify as GlobalVariable), so only lowercase is enforced there; `s_`/`g_` prefixes are guarded by code review.

### 2.3 例外 / Exceptions（两区共用 / shared）

以下内核风格内置宏 / API 名豁免大小写规则（保持原样）：
The following kernel-style built-in macros/APIs are exempt (kept as-is):

- 宏 / Macros：`container_of` / `container_of_const` / `offsetof` / `typeof_member` / `likely` / `unlikely` / `pre_execution` / `__XXX_H__` 头文件卫士 / header guards
- 函数式 API（static inline 实现）/ function-style APIs: `IS_ERR` / `PTR_ERR` / `ERR_PTR` / `IS_ERR_OR_NULL` / `COMPAT_*`

---

## 3. 分层配置文件 / Layered Config Files

| 目录 / Dir | 文件 / File | 作用 / Role |
| :--- | :--- | :--- |
| 根 / Root | [`.clang-format`](../.clang-format) | 格式（全局）/ formatting (global) |
| 根 / Root | [`.clang-tidy`](../.clang-tidy) | 内核区命名 / kernel-zone naming |
| `app/` | [`.clang-tidy`](../app/.clang-tidy) | Google 区命名（建议）/ Google-zone naming (advisory) |
| `system_cpp/` | [`.clang-tidy`](../system_cpp/.clang-tidy) | Google 区命名（强规定）/ Google-zone naming (mandatory) |
| 根 / Root | [`.clang-format-ignore`](../.clang-format-ignore) | 格式化排除 `lib/` / exclude `lib/` from formatting |

---

## 4. 检查命令 / Check Commands

```bash
# 格式（app 以下提交前必过）/ formatting (mandatory below app)
clang-format --style=file --dry-run <file>       # 检查 / check
clang-format --style=file -i <file>              # 应用 / apply

# 命名 / naming
clang-tidy -p . <file.c>                          # C 源 / C source
clang-tidy -p . <file.h>                          # C 头 / C header
clang-tidy -p . <file.hpp> --extra-arg=-std=gnu++17  # C++ 头 / C++ header
```

`compile_commands.json` 由 `tools/gen_compile_db.py` 生成（含头文件条目）。clangd 会在编辑器中直接标注命名违规。
`tools/gen_compile_db.py` generates `compile_commands.json` (including header entries). clangd flags naming violations directly in the editor.

---

## 相关文档 / Related Documents

- [architecture.md](architecture.md) · [service_spec.md](service_spec.md)（应用层允许/禁止 / app-layer allow/forbid）· [fast_path.md](fast_path.md)（热路径红线 / hot-path red lines）
- [../CONTRIBUTING.md](../CONTRIBUTING.md)（贡献规约 / contribution rules）· [../CHANGELOG.md](../CHANGELOG.md)
