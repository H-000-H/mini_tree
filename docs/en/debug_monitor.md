# Debugging & Monitoring

> Logging, build-artifact inspection, disassembly, clangd, and a suggested debug order.

| Item | Content |
| :--- | :--- |
| **Audience** | Anyone tracking down boot / probe / I/O issues |
| **Related** | [faq.md](faq.md) · [problem_summary.md](problem_summary.md) · [tools_guide.md](../tools_guide.md) |

---

## Contents

1. [Logging](#1-logging)
2. [Inspecting Build Artifacts](#2-inspecting-build-artifacts)
3. [Disassembly](#3-disassembly)
4. [clangd](#4-clangd)
5. [Suggested Debug Order](#5-suggested-debug-order)

---

## 1. Logging

| API | Header | Notes |
| :--- | :--- | :--- |
| `SYS_LOGI/W/E` | `system_log.h` | System level; backend selected by Kconfig |
| `DRV_LOG*` | same (via production_log) | Driver path; watch the pool and performance |

Backends:

- `CONFIG_SYS_LOG_USE_PRINTF`
- `CONFIG_SYS_LOG_USE_OSAL`
- (Optional) ESP paths compile only under their macros

Do not spam INFO on hot paths; see [fast_path.md](fast_path.md).

---

## 2. Inspecting Build Artifacts

In the build directory (name varies per project), verify:

| File | What to Check |
| :--- | :--- |
| `config.h` | Whether the OSAL/SYSTEM/LOG macros are correct |
| `board_nodes.h` | `DEV_ID_COUNT`, each `DEV_ID_*`, chosen |
| `dt_config_gen.h` | `DTC_GEN_COUNT_*`, clock capacities |
| `board_probe.c` | Whether your new `board_driver_probe_*` is included |

Without a build, use `ide/stubs/*` to align symbol names, but the **ID counts are placeholders** — trust the generated artifacts.

---

## 3. Disassembly

With `CONFIG_BUILD_DISASM=y` and `cmake/disasm.cmake` (enabled by the platform project) a `.lst` is generated, to check:

- Whether weak empty HAL stubs are still linked in
- Whether ISRs accidentally drag in heavyweight functions

---

## 4. clangd

| Item | Correct Practice |
| :--- | :--- |
| Workspace root | The mini_tree repo root |
| Compilation database | Root `compile_flags.txt`; in an ESP project, `idf.py build` produces `build/compile_commands.json` for indexing |
| Forbidden | Putting another `compile_flags.txt` in a subdirectory |
| Stub headers | `ide/stubs/` |
| After config changes | `Clangd: Restart language server` |

---

## 5. Suggested Debug Order

1. `config.h` macros
2. Whether dtc-lite was re-run and compatibles match
3. Link symbols: use `nm`/`objdump` to see where `hal_*` resolves
4. Return value and logs of `board_driver_probe_all`
5. Single-peripheral read/write
6. Interrupt / WDT / reset scenarios

---

## Related Documents

- [faq.md](faq.md) · [device_tree_porting.md](device_tree_porting.md)
- [architecture.md](architecture.md)
