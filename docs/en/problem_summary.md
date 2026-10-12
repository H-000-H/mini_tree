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
| P2 | Stack ownership for bare-metal C++ tasks under `CONFIG_XTASK_PREEMPT` | unclear who allocates; possible overflow | platform manually specifies in `（C++ 封装已移除）` for now; long-term belongs to `system` task manager |
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
| P9 | `ide/stubs/` drifts from real headers | clangd false positives | re-run CMake configure to regenerate |

---

## 4. Resolved

| ID | Issue | Root Cause | Impact | Fix |
| :--- | :--- | :--- | :--- | :--- |
| P12 | Preemptive scheduler (xtask_preempt) TIM path non-functional | See detailed analysis below | TIM7 IRQ never fires → falls through to SysTick fallback → tick_count stays at 0 → tasks never expire → LED does not blink | Add `interrupt_hw_enable()` in `xtask_preempt.c`; bridge `hal_systick_irq_handler()` in `interrupt_stm32.c` SysTick_Handler |
| P13 | mini-os hangs in `svc_handler` right at boot | `mini_os_psp_set` / `mini_os_set_control` lack `bx lr`, so control falls through into the immediately following `svc_handler`; that function calls the C dispatcher with `bl` without preserving `EXC_RETURN`, so `bx lr` jumps back to itself | Permanent loop before the scheduler starts; no serial output at all | Add `bx lr` to both functions in `port.S`; add `push {r1, lr}` / `pop {r1, lr}` in `svc_handler` |
| P14 | All serial logging lost under the mini-os backend | `mini_os_mutex_lock` **rejects unconditionally** when there is no thread context (not even a free lock is granted), yet early-boot logging must go through `device_lock` | `_write` silently drops every log line; console stays empty | In `mini_backend_mini_os.c`, let lock/unlock through during the boot phase (`mini_os_thread_current() == NULL`), matching the FreeRTOS / bare-metal backends |
| P15 | A delayed task never wakes up | The time-wheel expiry path in `mini_os_systick_handler` only puts the thread back on the ready list and never triggers PendSV; Cortex-M exception return does not schedule | 819 ticks produced only 5 context switches; the task sat in `delay` forever (LED blinked once) | Append `mini_os_schedule_yield_isr()` at the end of `mini_os_systick_handler`, symmetric with the yield in `mini_os_schedule_delay` |
| P16 | First PendSV stacks its exception frame out of range | `mini_os_schedule_start` switched `CONTROL.SPSEL` to PSP first, then set PSP to the "no thread" marker `0`; the moment interrupts unmasked, the hardware stacked the frame on PSP | Frame landed at `0xFFFFFFE0~0xFFFFFFFC` (Vendor_SYS); silently dropped on STM32F4, possible BusFault elsewhere | Stop touching CONTROL at startup (stay on MSP); `pendsv_handler` now declares "return to Thread mode on PSP" via `EXC_RETURN` bit2, and the hardware restores SPSEL |
| P17 | Infinite loop in `mini_os_mutex_propagate` when tasks contend for a lock | An application task stack overflowed and corrupted the `hold_list` of an adjacent TCB in the same heap; the PI propagation walks that list, back-derives a bogus mutex whose `wait_list.next` is 0, and the inner loop condition never becomes false | ~111k invalid address accesses, then the system stalls | Raise the application task stack 512 → 2048 B (TCB and stack being heap-adjacent is by design; sizing the stack is the correct fix) |

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

### Notes: Bringing Up the mini-os Backend (P13–P17)

The five issues surfaced **one after another**: each fix revealed the next. All were located with Renode function-entry hooks (`AddHook` hit counting) plus disassembly cross-checks, and all are deterministic — none of them intermittent.

| Phase | Symptom | Breakpoint | Evidence |
| :--- | :--- | :--- | :--- |
| Boot | PC stuck at `0x80202a2`, no output | the `bx lr` inside `svc_handler` | the instruction before it is `bl`, so LR was already clobbered |
| Boot | same (after P13) | `hal_uart_write` hit 0 times | `console_dev_get()` returns NULL inside `_write` |
| Runtime | LED toggles only once | `device_ioctl` hit 1 time | 819 ticks vs only 5 `schedule_switch` calls |
| Runtime | ~111k invalid accesses | inner loop of `mini_os_mutex_propagate` | `[0+0x2C]` / `[0+0x0]` read repeatedly, node always 0 |

**Takeaways**:

- **Verify the return instruction of every port routine**: functions that merely write one register (`mini_os_psp_set` / `mini_os_set_control`) are easy to leave without `bx lr`, and control then falls through into whatever function comes next in the file;
- **Every exception entry must preserve `EXC_RETURN` itself**: any handler that does `bl` into C code has to stash LR first;
- **An ISR that wakes a thread must explicitly raise PendSV**: Cortex-M exception return does not schedule on its own. `mini_os_schedule_yield_isr()` is the single exit for this contract, and new wake paths must not omit it;
- **The backend shim is where semantics get reconciled**: the same `device_lock` is grantable from FreeRTOS (free lock succeeds) and bare-metal (spin until acquired), but mini-os demands a thread context — such gaps belong in `mini_backend_*.c`, not leaked to callers;
- **Size thread stacks against the deepest call chain**: TCB and stack are heap-adjacent, so an overflow corrupts a neighbour's list nodes and shows up as an unrelated kernel crash.

---

## 5. Worked Around (Won't Fix)

| ID | Issue | Handling |
| :--- | :--- | :--- |
| P10 | classic Keil µVision integration | not provided / not followed up in this repo, see [keil_integration.md](keil_integration.md) |
| P11 | ARMCC v5 compilation | unsupported; ARMCLANG (AC6) only |

---

## Related Docs

- [todolist.md](todolist.md) · [roadmap.md](roadmap.md) · [faq.md](faq.md)
