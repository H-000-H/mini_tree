# Red-Line Zone — Hard Real-Time Fast Path

> What is **absolutely forbidden** on ISR, GPIO-toggle, DMA-complete paths — and what to do instead.

| Item | Content |
| :--- | :--- |
| **Audience** | Driver & control-loop writers |
| **Prereq** | [service_spec.md](service_spec.md) · [architecture.md](architecture.md) §6 |
| **Related** | [debug_monitor.md](debug_monitor.md) |

---

## Contents

1. [What Is Fast Path](#1-what-is-fast-path)
2. [Hard Forbiddens](#2-hard-forbiddens)
3. [Recommended Pattern](#3-recommended-pattern)
4. [GPIO Fast Path](#4-gpio-fast-path)
5. [Self-Check List](#5-self-check-list)

---

## 1. What Is Fast Path

| Scenario | Notes |
| :--- | :--- |
| ISR top-half | `interrupt` top_half, peripheral IRQ handler |
| DMA / transfer-complete callbacks | usually in interrupt context |
| `hal_gpio_fast_*` | designed as zero-branch hot path |
| High-frequency control loops | treat as hot path below ~100 µs periods |

Cold paths (probe, deinit, error recovery, user-config ioctls) **may** do full checks and logging.

---

## 2. Hard Forbiddens

| Forbidden | Why |
| :--- | :--- |
| `printf` / `SYS_LOGI` spam | blocking, non-reentrant, stretches IRQ-off time |
| `malloc` / new-delete | unbounded latency; possibly poisoned |
| Mutex or sleep | deadlock or illegal ISR call |
| `strcmp`, device-tree walks, device lookup | latency jitter |
| Unbounded HW polls | use timeouts or DMA/interrupts |
| Calling HAL past the bus `poison` | breaks layering, hard to review |

`interrupt.h` also states: no printf / locking / long blocking inside ISRs.

---

## 3. Recommended Pattern

```text
IRQ
  → clear HW flags
  → optionally snapshot registers into static/pooled buffers
  → submit a bottom half or queue to a task
  → return

Task or main loop
  → protocol, logging, device_ioctl, state machines
```

When you need "as fast as possible and portable": call only `hal_gpio_fast_*` or VFS-documented fast ioctls — still no logging inside.

---

## 4. GPIO Fast Path

The middleware provides:

- `hal_gpio_fast_set_level`
- `hal_gpio_fast_get_level`
- `hal_gpio_fast_toggle`

Platform implementations must be **table-free and branch-free** (per the header contract). Configuration/mode changes go through the slow path `hal_gpio_set_*`.

---

## 5. Self-Check List

- [ ] short top-half, no OS blocking APIs
- [ ] logging only in tasks or bottom halves
- [ ] no dynamic allocation on hot paths
- [ ] timeout paths return `VFS_ERR_TIMEOUT`, never spin forever

---

## Related Documents

- [service_spec.md](service_spec.md) · [architecture.md](architecture.md)
- [debug_monitor.md](debug_monitor.md)
