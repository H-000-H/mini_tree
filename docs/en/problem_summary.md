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

## 4. Resolved

| ID | Issue | Root Cause | Impact | Fix |
| :--- | :--- | :--- | :--- | :--- |
| P12 | Preemptive scheduler (xtask_preempt) TIM path non-functional | See detailed analysis below | TIM7 IRQ never fires → falls through to SysTick fallback → tick_count stays at 0 → tasks never expire → LED does not blink | Add `interrupt_hw_enable()` in `xtask_preempt.c`; bridge `hal_systick_irq_handler()` in `interrupt_stm32.c` SysTick_Handler |

### P12 Detailed Analysis

**Causal chain**: Cause 1 → TIM7 interrupt never fires → falls through to SysTick fallback → Cause 2 → tick_count stays at 0 → tasks never expire → LED does not blink

| # | Root Cause | Location | Why cooperative scheduler is unaffected |
| :---: | :--- | :--- | :--- |
| 1 | TIM branch missing `interrupt_hw_enable()` — VIRQ registered but NVIC never enabled, hardware IRQ never triggers | `xtask_preempt.c` `xscheduler_start()` | Cooperative `xtask_coop.c` already has these 3 lines; NVIC enabled correctly |
| 2 | SysTick fallback path: board-level strong `SysTick_Handler` only calls `HAL_IncTick()`, does not chain `hal_systick_irq_handler()` scheduler hook | `interrupt_stm32.c` | Cooperative uses TIM7 → `TIM7_IRQHandler` → VIRQ dispatch path; does not depend on SysTick hook |

**Fix details**:

1. `mini_tree/time_slice/task/xtask_preempt.c` — `xscheduler_start()` TIM branch: add NVIC enable:
   ```c
   int irqn = -1;
   int priority = 5;
   device_get_prop_int(tick_dev, "irqn", &irqn);
   device_get_prop_int(tick_dev, "nvic-priority", &priority);
   interrupt_hw_enable(irqn, (uint32_t)priority);
   ```
2. `hal/system/interrupt_stm32.c` — `SysTick_Handler`: bridge scheduler hook:
   ```c
   void SysTick_Handler(void)
   {
       HAL_IncTick();
       hal_systick_irq_handler();  /* chain to scheduler */
   }
   ```

---

## 5. Worked Around (Won't Fix)

| ID | Issue | Handling |
| :--- | :--- | :--- |
| P10 | classic Keil µVision integration | not provided / not followed up in this repo, see [keil_integration.md](keil_integration.md) |
| P11 | ARMCC v5 compilation | unsupported; ARMCLANG (AC6) only |

---

## Related Docs

- [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md)
