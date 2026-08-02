# 贡献指南 / Contribution Guide

> 如何改中间件、如何提 PR、本地要准备什么。  
> How to modify the middleware, how to submit a PR, and what to prepare locally.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 贡献者 / Contributors |
| **相关 / Related** | [docs/api_compatibility.md](docs/api_compatibility.md) · [docs/driver_guide.md](docs/driver_guide.md) |

---

## 开发原则 / Development Principles

1. **不向中间件公共头引入厂商 SDK 依赖**（OSAL 后端 `#if` 路径除外）。  
   **No vendor SDK dependencies in public middleware headers** (except OSAL backend `#if` paths).
2. 新外设顺序：**HAL 头 + weak .c →（可选）bus → vfs → `DRIVER_REGISTER` → 文档/契约**。  
   New peripheral order: **HAL header + weak .c → (optional) bus → vfs → `DRIVER_REGISTER` → docs/contract**.
3. 错误码统一 `status.h`；对外 API 禁止 `void` 成功/失败。  
   Error codes are unified in `status.h`; public APIs must not use `void` to signal success/failure.
4. 新建源文件首行必须携带 `/* SPDX-License-Identifier: Apache-2.0 */`（C）或 `/** SPDX-License-Identifier: Apache-2.0 ...`（头文件 Doxygen 块）。  
   New source files must start with `/* SPDX-License-Identifier: Apache-2.0 */` (C) or `/** SPDX-License-Identifier: Apache-2.0 ...` (Doxygen block in headers).
5. 文档与代码同 PR：至少更新 [docs/file_index.md](docs/file_index.md) 或对应 `docs/*`；新开源积木更新 [docs/ecosystem.md](docs/ecosystem.md) + [NOTICE](NOTICE)（含版本、版权人、许可证）。  
   Docs and code go in the same PR: at least update [docs/file_index.md](docs/file_index.md) or the corresponding `docs/*`; new open-source components also update [docs/ecosystem.md](docs/ecosystem.md) + [NOTICE](NOTICE) (version, copyright holder, license).
6. 遵守 [docs/fast_path.md](docs/fast_path.md) 与分层 poison。  
   Follow [docs/fast_path.md](docs/fast_path.md) and the layered poison.
7. **语言规范**：`.clang-format`（Allman 大括号、单语句 if/for/while 去括号、4 空格缩进、100 列、指针靠左）+ 分层 `.clang-tidy`（根 = 内核区，全小写无前缀；`app/`、`system_cpp/` = Google 区，类型 PascalCase + `s_`/`g_`/`k_` 前缀）；app 层为建议、app 以下为强规定；命名由 `.clang-tidy` 强制、格式由 `.clang-format` 强制。详见 [docs/coding_style.md](docs/coding_style.md)。  
   **Coding style**: `.clang-format` (Allman braces, no braces around single-statement if/for/while, 4-space indent, 100 columns, pointer-on-left) plus layered `.clang-tidy` (root = kernel zone, all-lowercase without prefixes; `app/`, `system_cpp/` = Google zone, PascalCase types + `s_`/`g_`/`k_` prefixes); advisory in `app/`, mandatory below `app/`; naming enforced by `.clang-tidy`, formatting by `.clang-format`. See [docs/coding_style.md](docs/coding_style.md).

---

## 环境搭建 / Environment Setup

| 组件 / Component | 说明 / Notes |
| :--- | :--- |
| CMake + Ninja/Make | 构建 / Build |
| Python3 + `lark` | dtc-lite |
| 可选 `kconfiglib` | menuconfig |
| clangd | 打开仓库根；用根 `compile_flags.txt` / Open the repo root; use the root `compile_flags.txt` |

```bash
pip install lark
# 可选: pip install kconfiglib
```

---

## PR 规约 / PR Guidelines

| 要求 / Requirement | 说明 / Notes |
| :--- | :--- |
| 说明影响层 / State the affected layer | hal / bus / vfs / board / osal / tools / docs |
| DTS 契约变更 / DTS contract changes | 同步 `docs/driver_guide.md` / Update `docs/driver_guide.md` |
| 文档位置 / Doc placement | 新专题进 `docs/`；勿在根目录再堆手册 / New topics go to `docs/`; do not pile more manuals in the root |
| 不提交 / Do not commit | 密钥、本机绝对路径、子目录 `compile_flags.txt`、SoC 专用 dtsi 冒充默认板 / Secrets, machine-local absolute paths, per-directory `compile_flags.txt`, SoC-specific dtsi masquerading as the default board |
| 测试说明 / Testing notes | 至少：生成物是否更新、相关外设是否编过 / At minimum: whether generated artifacts were updated and whether the affected peripherals compile |

---

## 文档规格 / Documentation Spec

新专题进 `docs/`；规格与目录见 [docs/README.md](docs/README.md)。  
New topics go into `docs/`; the spec and index live in [docs/README.md](docs/README.md).  
新增开源积木须同步 [docs/ecosystem.md](docs/ecosystem.md) 与 [NOTICE](NOTICE)。  
New open-source components must also sync [docs/ecosystem.md](docs/ecosystem.md) and [NOTICE](NOTICE).

---

## 相关文档 / Related Documents

- [docs/roadmap.md](docs/roadmap.md) · [docs/todolist.md](docs/todolist.md) · [docs/design_decisions.md](docs/design_decisions.md) · [docs/ecosystem.md](docs/ecosystem.md)
