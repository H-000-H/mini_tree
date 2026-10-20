# Coding Style

> This project is simply **Linux kernel coding style**: the root `.clang-format` is the single source of truth
> (it is the kernel's official configuration). One command formats everything — `bash use_clang_format.sh`;
> your IDE runs the same file on save, so **never hand-tune formatting**.

| Item | Value | At a glance |
| :--- | :--- | :--- |
| Indent | Tab, width 8 | one level = one Tab; column alignment via tabs, padded with spaces |
| Column limit | 100 | wrap when exceeded, continuation indent 8 |
| Braces | function `{` on its own line; `if/for/while` keep `{` inline | `BreakBeforeBraces: Linux` |
| Single statements | no `{}`, never crammed onto one line | `if (err)` then `return -1;` |
| Pointers | stick to the name | `char *p`, `void (*fn)(int)` |
| Consecutive decls / assignments | aligned | `node` / `ops` line up |
| Trailing comments | aligned, one leading space | `/**< ... */` |
| `extern "C"` | not indented | `extern "C" {` |

## Naming (not clang-format's job — enforced by clang-tidy)

`readability-identifier-naming` enforces it: uniform lowercase `snake_case`, the only uppercase is macros.

| Category | Convention | Example |
| :--- | :--- | :--- |
| functions / variables / types | lowercase | `device_open`, `struct device`, `device_id_t` |
| C++ classes | `PascalCase` | `SystemCmd` |
| macros / enum values | UPPERCASE | `MINI_ERR_INVAL`, `DEV_ID_UART0` |
| prefixes | **none** (kernel style — no Google `s_`/`g_`/`k_`); functions carry the module name | `client_pool_ctrl`, `nrf24l01_ioctl` |

Recommended at the `app` layer, mandatory below it.

## Headers

- One `.h` per `.c` (except purely internal modules), always `#pragma once`
- Include order: own `.h` → system/library → project; `compiler_compat_poison.h` **last, always**
- No raw `.c`-to-`.c` includes
- Dangerous APIs (malloc / printf / strcpy / memcpy …) are poisoned by default; opt out with `ALLOW_HEAP_ALLOC` / `ALLOW_STDIO_OUTPUT`. Full list in `core/include/compiler_compat_poison.h`

## Logging

- `MT_LOG_ERROR/WARN/INFO` at system level; `MT_DRV_LOG_*` at driver level (see [debug_monitor.md](debug_monitor.md))
- Hot paths only `LOGD`/`LOGV`; do not spam INFO

## Related

- [getting_started.md](getting_started.md) (clangd) · [driver_guide.md](driver_guide.md) · [faq.md](faq.md)
