# Coding Style

> Automation first: formatting and naming go to clang-format / clang-tidy; humans only watch what the spec doesn't cover.

| Item | Content |
| :--- | :--- |
| **Audience** | People editing middleware code, or setting up IDE navigation |
| **Prereq** | [getting_started.md](getting_started.md) (clangd section) |
| **Related** | [architecture.md](architecture.md) · [faq.md](faq.md) |

---

## Contents

1. [Automation First](#1-automation-first)
2. [clang-format](#2-clang-format)
3. [clang-tidy](#3-clang-tidy)
4. [Naming](#4-naming)
5. [Headers & Includes](#5-headers-includes)
6. [Logging](#6-logging)
7. [Dangerous APIs & Poison](#7-dangerous-apis-poison)

---

## 1. Automation First

Find clangd from the root `compile_flags.txt` first (otherwise the IDE only has a partial index):

- `Clangd: Restart language server`
- After changing macros / config → re-run genconfig (see [getting_started.md](getting_started.md))

Formatting and naming: **let the tools do it** — don't hand-edit.

---

## 2. clang-format

Root `.clang-format`:

| Item | Value |
| :--- | :--- |
| BasedOnStyle | LLVM |
| Braces | Allman (next line) |
| AllowShortBlocksOnASingleLine | `false` (keep `{}` even for single statements) |
| Indent | 4 spaces |
| ColumnLimit | 100 |
| Pointer/reference alignment | right (`int* p`) |

A platform project may copy and tweak `ColumnLimit`, but must **not** break the Allman / 4-space spine.

---

## 3. clang-tidy

Root `.clang-tidy`:

| Item | Value |
| :--- | :--- |
| Checks | `bugprone-*`, `clang-analyzer-*`, `modernize-*`, `performance-*`, `readability-*` |
| Naming enforced | lowercase (`x_task`, `x_scheduler`, `list_node`, `k_tag`, `struct event`, `mini_tree::` …) |

Naming violations surface at tidy time, not compile time — CI blocks them.

---

## 4. Naming

Uniform lowercase (enforced by `readability-identifier-naming`):

| Category | Convention |
| :--- | :--- |
| functions / variables | `snake_case` |
| types (`struct/typedef`) | lowercase (`device`, `hal_can_config`) |
| namespace (`system_cpp`) | `mini_tree::` |
| macros / enum values | UPPER `SNAKE_CASE` (`DEV_ID_UART0`, `VFS_ERR_*`) |

Recommended at the `app` layer; mandatory below `app`.

---

## 5. Headers & Includes

- One `.h` per `.c` (except purely internal modules)
- Headers use `#pragma once` (no legacy `#ifndef` guards)
- Include order: own `.h` → system/library → project
- No raw `.c`-to-`.c` includes

---

## 6. Logging

- `SYS_LOGI/W/E` system level; `DRV_LOG*` driver level (see [debug_monitor.md](debug_monitor.md))
- Hot paths only `LOGD`/`LOGV`; don't spam INFO
- Don't stuff logs in as debug breakpoints

---

## 7. Dangerous APIs & Poison

`core/include/compiler_compat.h` defines `compiler_compat_poison`, gated by `CONFIG_POISON_DANGEROUS_API`:

| Poisoned API | Replacement |
| :--- | :--- |
| `malloc` / `free` | static pool / `bufferpool` / `kalloc` |
| `printf` | `SYS_LOG*` |
| bare `memcpy` / `memset` / `memmove` | `safe_mem*` or explicit length checks |
| `strcpy` / `strcat` | `safe_str*` |

Poison only applies in controlled translation units; HAL strong symbols and platform bare-metal parts may be exempt (per the macro definition).

---

## Related Documents

- [getting_started.md](getting_started.md) · [faq.md](faq.md) · [debug_monitor.md](debug_monitor.md) · [service_spec.md](service_spec.md)
