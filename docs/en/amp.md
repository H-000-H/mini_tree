# Heterogeneous Multi-Core AMP

> **AMP is part of the optional bricks — use it on demand**: single-core is a complete baseline for development; enable dual-core/hetero only when needed, and assemble the secondary-core image & shared-memory layout yourself.
>
> How `CPU_CORES` and `hal_cpu_*` (in `hal/amp`) fit together.
> **The full secondary-core image and shared-memory layout are provided by the platform project**; this repo only defines the HAL contract and backend spinlock behavior differences.

| Item | Content |
| :--- | :--- |
| **Audience** | Dual-core & AMP board engineers |
| **Prereq** | [getting_started.md](getting_started.md) Kconfig · [device_tree_porting.md](device_tree_porting.md) |
| **Related** | [backend_switching.md](backend_switching.md) · [architecture.md](architecture.md) · [runtime_services.md](runtime_services.md) |

---

## Contents

1. [Brick Positioning](#1-brick-positioning)
2. [Kconfig](#2-kconfig)
3. [HAL Contract](#3-hal-contract)
4. [Recommended Topology](#4-recommended-topology)
5. [the unified interface & Synchronization](#5-backend-synchronization)
6. [Safety](#6-safety)
7. [Acceptance](#7-acceptance)

---

## 1. Brick Positioning

AMP is an **optional brick** (same family as the safety modules, see [runtime_services.md](runtime_services.md) §5): **off by default, used on demand** — without it, mini_tree stays fully functional.

| Item | Notes |
| :--- | :--- |
| Default | single-core (`CONFIG_CPU_CORES=1`); device model / VFS / 统一接口 / EventBus all work as usual |
| Enable | set `CONFIG_CPU_CORES=2` when you actually need it |
| Prerequisites | platform supplies: secondary-core image, shared-memory layout, inter-core IPC; this repo only defines the HAL contract & the unified interface behavior |
| Reference | [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) (mini_tree's companion platform example) |
| Impact of not enabling | **none** — single-core is a complete, usable baseline |

> **AMP is part of the brick family — use it on demand**: skip it and mini_tree stays complete; adopt it and the secondary core & shared memory are yours to assemble.

---

## 2. Kconfig

| Symbol | Meaning |
| :--- | :--- |
| `CONFIG_CPU_CORES` | `1` single-core (default); `2` dual-core AMP |
| Derived behavior | with `CPU_CORES>1` the bare-metal mutex automatically uses atomic CAS; single-core falls back to IRQ-lock + plain access (**no separate switch** — derived from `CPU_CORES`) |

Single-core: no secondary startup needed; `hal_cpu_emergency_stop_all_cores` mostly disables local interrupts.
Dual-core: the platform must implement the secondary entry and a clear shared-resource protocol.

> **ARMv6 note**: M0/M0+ (ARMv6-M) has no LDREX/STREX, so `MINI_ATOMIC_CAS` degrades to "IRQ-lock + read-modify-write" (`MINI_ATOMIC_IRQ_SOFT_ATOMIC=1` in `core/include/compiler_compat.h`) — **core-local only**, i.e. no hardware guarantee for a cross-core mutex. Building such a target with `CPU_CORES=2` is rejected by a compile-time guard in `core/src/mini_backend_bare.c`; define `MINI_AMP_NO_ATOMIC_OK` to opt out when you know no lock is shared across cores.

---

## 3. HAL Contract

Header: `hal/amp/hal_amp.h` (the internal guard keeps the legacy `HAL_CPU_H` name).

| API | Use |
| :--- | :--- |
| `hal_cpu_secondary_startup` | Start/release the secondary core (platform) |
| `hal_cpu_baremetal_entry` | Bare-metal entry on the secondary core (called by its image) |
| `hal_cpu_get_id` | Current core ID |
| `hal_cpu_emergency_stop_all_cores` | Emergency stop: disable IRQs on single-core; must also suspend the peer on dual-core |
| `hal_is_in_isr` / `hal_irq_*` | ISR detection & NVIC (Cortex-M inline; overridden on RISC-V etc.) |

Middleware weak stubs default to no-ops; **real hardware needs platform strong symbols** (at least `emergency_stop` and `get_id` behaving sensibly).

---

## 4. Recommended Topology

Common convention (adjustable per SoC; docs & the unified interface assume this):

| Core | Role |
| :--- | :--- |
| Core 0 | Runs the RTOS + mini_tree main stack (probe, VFS, business) |
| Core 1 | Bare-metal or light loop: fast path, dedicated peripherals; communicates via shared memory / IPC |

Suggested boot order:

1. Core 0: clocks and required shared-region init.
2. Core 0: `mini_tree_pre_os_init` → … → scheduler.
3. Call `hal_cpu_secondary_startup()` at the agreed point.
4. Core 1: enters its own loop from `hal_cpu_baremetal_entry` or the reset vector.

This repo does **not** ship an inter-core message protocol; use EventBus (local core only) + a platform shared queue, or a custom IPC.

---

## 5. the unified interface & Synchronization

- Under `CONFIG_OS_BARE`, `CPU_CORES>1` (AMP) drives mutex-like primitives to **atomic CAS** (`MINI_ATOMIC_CAS`); single-core falls back to IRQ-lock + plain access. On OS backends the secondary core has no kernel scheduler (`core/src/mini_backend_freertos.c` logs a warning and falls tasks requesting Core 1 back to Core 0), so AMP is in practice a bare-metal-backend feature.
- Spinlock: `MINI_OS_SPINLOCK` vs `ATOMIC` — prefer atomic for multi-core shared data, see [backend_switching.md](backend_switching.md).
- **Never** assume `device_*` locks on the other core are visible to you; cross-core traffic goes through explicit shared objects.

---

## 6. Safety

On fault, all hazard-capable outputs must be stopped: `safe_state` / `hal_cpu_emergency_stop_all_cores` / `hal_platform_safety`.
On dual-core, implement "suspend the peer" or an equivalent silent state — not just `cpsid` on the local core.

> These safety capabilities are likewise **optional bricks** (see [runtime_services.md](runtime_services.md) §5): recommended, but optional for single-core core development.

---

## 7. Acceptance

- [ ] secondary core runs a minimal loop at `CPU_CORES=2`
- [ ] no data races on shared regions (or an atomic/lock protocol)
- [ ] both cores reach a safe output state on injected faults
- [ ] single-core fallback: boots fine without the secondary core

Platform sample projects (link scripts, secondary images) live outside this shelf; see [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) (mini_tree's companion platform example).

---

## Related Documents

- [device_tree_porting.md](device_tree_porting.md) · [backend_switching.md](backend_switching.md) · [design_decisions.md](design_decisions.md)
- [runtime_services.md](runtime_services.md) · [todolist.md](todolist.md) (AMP sample tracking)
