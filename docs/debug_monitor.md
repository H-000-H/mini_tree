# 调试与监控 / Debugging & Monitoring

> 日志、生成物检查、反汇编、clangd，以及建议的调试顺序。
> Logging, build-artifact inspection, disassembly, clangd, and a suggested debug order.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 定位启动 / probe / I/O 问题的人 / Anyone tracking down boot / probe / I/O issues |
| **相关 / Related** | [faq.md](faq.md) · [problem_summary.md](problem_summary.md) · [tools/README.md](../tools/README.md) |

---

## 目录 / Contents

1. [日志 / Logging](#1-日志-logging)
2. [查看生成物 / Inspecting Build Artifacts](#2-查看生成物-inspecting-build-artifacts)
3. [反汇编 / Disassembly](#3-反汇编-disassembly)
4. [clangd / clangd](#4-clangd-clangd)
5. [建议调试顺序 / Suggested Debug Order](#5-建议调试顺序-suggested-debug-order)

---

## 1. 日志 / Logging

| API | 头 / Header | 说明 / Notes |
| :--- | :--- | :--- |
| `SYS_LOGI/W/E` | `system_log.h` | 系统级；后端由 Kconfig 选择 / System level; backend selected by Kconfig |
| `DRV_LOG*` | 同左（经 production_log）/ same (via production_log) | 驱动路径；注意池与性能 / Driver path; watch the pool and performance |

后端 / Backends：

- `CONFIG_SYS_LOG_USE_PRINTF`
- `CONFIG_SYS_LOG_USE_OSAL`
- （可选 / Optional）ESP 路径仅在对应宏下编译 / ESP paths compile only under their macros

热路径不要刷 INFO；见 [fast_path.md](fast_path.md)。
Do not spam INFO on hot paths; see [fast_path.md](fast_path.md).

---

## 2. 查看生成物 / Inspecting Build Artifacts

构建目录（名称因工程而异）中确认：
In the build directory (name varies per project), verify:

| 文件 / File | 看什么 / What to Check |
| :--- | :--- |
| `config.h` | OSAL/SYSTEM/LOG 宏是否正确 / Whether the OSAL/SYSTEM/LOG macros are correct |
| `board_nodes.h` | `DEV_ID_COUNT`、各 `DEV_ID_*`、chosen |
| `dt_config_gen.h` | `DTC_GEN_COUNT_*`、时钟容量 / clock capacities |
| `board_probe.c` | 是否包含你新写的 `board_driver_probe_*` / Whether your new `board_driver_probe_*` is included |

IDE 无构建时用 `ide/stubs/*` 对齐符号名，但 **ID 数量是占位**，以生成物为准。
Without a build, use `ide/stubs/*` to align symbol names, but the **ID counts are placeholders** — trust the generated artifacts.

---

## 3. 反汇编 / Disassembly

`CONFIG_BUILD_DISASM=y` 时配合 `cmake/disasm.cmake`（平台工程启用）生成 `.lst`，用于审查：
With `CONFIG_BUILD_DISASM=y` and `cmake/disasm.cmake` (enabled by the platform project) a `.lst` is generated, to check:

- 是否仍链到 weak 空 HAL / Whether weak empty HAL stubs are still linked in
- ISR 是否意外拉入重量级函数 / Whether ISRs accidentally drag in heavyweight functions

---

## 4. clangd / clangd

| 项 / Item | 正确做法 / Correct Practice |
| :--- | :--- |
| 工作区根 / Workspace root | mini_tree 仓库根 / The mini_tree repo root |
| 编译数据库 / Compilation database | 根 `compile_flags.txt`；或运行 `tools/gen_compile_db.py` 生成 `compile_commands.json`（含头文件条目，便于 clang-tidy 单独检查头文件）/ Root `compile_flags.txt`; or run `tools/gen_compile_db.py` to generate `compile_commands.json` (includes header entries, so clang-tidy can check headers standalone) |
| 禁止 / Forbidden | 子目录再放 `compile_flags.txt` / Putting another `compile_flags.txt` in a subdirectory |
| 占位头 / Stub headers | `ide/stubs/` |
| 改配置后 / After config changes | `Clangd: Restart language server` |

---

## 5. 建议调试顺序 / Suggested Debug Order

1. `config.h` 宏 / macros
2. dtc-lite 是否重跑、compatible 是否匹配 / Whether dtc-lite was re-run and compatibles match
3. 链接符号：`nm`/`objdump` 看 `hal_*` 归属 / Link symbols: use `nm`/`objdump` to see where `hal_*` resolves
4. `board_driver_probe_all` 返回与日志 / Return value and logs of `board_driver_probe_all`
5. 单外设读写 / Single-peripheral read/write
6. 中断 / WDT / 复位场景 / Interrupt / WDT / reset scenarios

---

## 相关文档 / Related Documents

- [faq.md](faq.md) · [porting_guide.md](porting_guide.md)
- [architecture.md](architecture.md)
