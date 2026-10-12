# Roadmap

> Mid-term planning for the middleware. Concrete tasks and status: [todolist.md](todolist.md); known issues: [problem_summary.md](problem_summary.md). Quarters are rough windows, not commitments.

| Item | Content |
| :--- | :--- |
| **Audience** | Planning / direction assessment |
| **Related** | [todolist.md](todolist.md) · [problem_summary.md](problem_summary.md) |

---

## 1. Near Term (This Quarter)

| Direction | Content |
| :--- | :--- |
| `err_section` separate segment | push linker scripts to place it separately by default (fix P1) |
| bare-metal C++ stack ownership | move to `system` task manager (fix P2) |
| dtc-lite nested includes | pin a single `BOARD_DTSI_DIR` (fix P3) |

---

## 2. Mid Term (Next 1–2 Quarters)

| Direction | Content |
| :--- | :--- |
| VFS pool-reset auto re-bind | handle uniformly at framework level (fix P4) |
| more HAL backends | add GPIO / SPI / I2C variants as needed |
| `ide/stubs/` sync | auto-regenerate via CMake configure hook (fix P9) |

---

## 3. Long Term (3+ Quarters)

| Direction | Content |
| :--- | :--- |
| network stack integration | expose lwIP via VFS with a standardized interface |
| Rust interop | experimental `cmake/rust.cmake` integration |
| Keil Studio validation | tried it and it works; just use Keil Studio if you want Keil (see [keil_integration.md](keil_integration.md)) |

---

## 4. Not Planned

| Direction | Reason |
| :--- | :--- |
| official classic Keil µVision support | architectural mismatch; this repo does not provide or follow it up, see [keil_integration.md](keil_integration.md) |
| ARMCC v5 support | ARMCLANG (AC6) only |

---

## Related Docs

- [todolist.md](todolist.md) · [problem_summary.md](problem_summary.md) · [design_decisions.md](design_decisions.md)
