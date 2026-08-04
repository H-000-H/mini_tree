# Known Issues Summary

> Open / worked-around design and implementation issues in the middleware. Fix progress: [todolist.md](todolist.md); planning: [roadmap.md](roadmap.md). Severity: High / Medium / Low.

| Item | Content |
| :--- | :--- |
| **Audience** | Risk assessment / picking up a fix |
| **Related** | [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md) |

---

## 1. High Severity

| ID | Issue | Impact | Workaround / Status |
| :--- | :--- | :--- | :--- |
| P1 | `err_section` not placed separately in some linker scripts | error symbol table shares a segment with code, hard to diagnose | `CONFIG_ERR_SECTION=1` only works with a dedicated ROM area; see [memory_footprint.md](memory_footprint.md) §1 |
| P2 | Stack ownership for bare-metal C++ tasks under `CONFIG_OSAL_NULL_TASK_CPP` | unclear who allocates; possible overflow | platform manually specifies in `osal_task.cpp` for now; long-term belongs to `system` task manager |
| P3 | dtc-lite sensitive to nested dtsi `include` order | board overrides error-prone | platform pins a single `BOARD_DTSI_DIR` source |

---

## 2. Medium Severity

| ID | Issue | Impact | Workaround / Status |
| :--- | :--- | :--- | :--- |
| P4 | VFS-layer drivers need explicit re-bind after pool reset | missing bind skips `probe` | see [driver_guide.md](driver_guide.md) §7 |
| P5 | `hal/amp` multicore messaging spins on single-core boards | wastes a few cycles | single-core builds exclude `hal/amp` by default |
| P6 | `LOG_*` macros still occupy symbols after logging off | slight size bump | with `CONFIG_SYS_LOG_LEVEL=0` the compiler dead-code-eliminates most |

---

## 3. Low Severity

| ID | Issue | Impact | Workaround / Status |
| :--- | :--- | :--- | :--- |
| P7 | layered `.clang-tidy` is advisory at app layer | naming rules may be skipped | covered by review |
| P8 | `tools/build_size.py --format=baseline` warns without a baseline | no comparison on first run | run once to seed the baseline |
| P9 | `ide/stubs/` drifts from real headers | clangd false positives | re-run CMake configure to regenerate |

---

## 4. Worked Around (Won't Fix)

| ID | Issue | Handling |
| :--- | :--- | :--- |
| P10 | classic Keil µVision integration | not provided / not followed up in this repo, see [keil_integration.md](keil_integration.md) |
| P11 | ARMCC v5 compilation | unsupported; ARMCLANG (AC6) only |

---

## Related Docs

- [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md)
