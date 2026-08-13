# Todo List

> Current in-progress / pending engineering tasks. Planning: [roadmap.md](roadmap.md); known issues: [problem_summary.md](problem_summary.md). Status: Todo / In Progress / Pending Verification / Done.

| Item | Content |
| :--- | :--- |
| **Audience** | Picking up / tracking tasks |
| **Related** | [roadmap.md](roadmap.md) · [problem_summary.md](problem_summary.md) |

---

## 1. Todo

| ID | Task | Link | Priority |
| :--- | :--- | :--- | :---: |
| T1 | `err_section` separate segment in default linker scripts | P1 | High |
| T2 | bare-metal C++ stack under `system` task manager | P2 | High |
| T3 | dtc-lite pin a single `BOARD_DTSI_DIR` | P3 | High |
| T4 | VFS pool-reset auto re-bind | P4 | Medium |
| T5 | `ide/stubs/` regenerate via CMake configure hook | P9 | Low |

---

## 2. In Progress

| ID | Task | Link | Progress |
| :--- | :--- | :--- | :---: |
| T6 | add GPIO / SPI / I2C HAL variants | — | 30% |
| T7 | drop `LOG_*` symbols after logging off | P6 | 60% |

---

## 3. Pending Verification

| ID | Task | Link | Blocked by |
| :--- | :--- | :--- | :--- |
| T8 | `build_size.py --format=baseline` seed baseline on first run | P8 | needs platform env |
| T9 | verify `hal/amp` excluded by default on single-core | P5 | needs single-core board |

---

## 4. Done

| ID | Task | Link |
| :--- | :--- | :--- |
| T10 | docs bilingual split | — |
| T11 | wire up `compiler_compat_poison.h` | — |

---

## Related Docs

- [roadmap.md](roadmap.md) · [problem_summary.md](problem_summary.md)
