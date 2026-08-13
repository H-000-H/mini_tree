# CAN Hook (Protocol Superset Hooks)

> **Weak-symbol hooks** on Classic CAN: every VFS open/close/read/write passes through; without strong symbols it's pass-through.
> It is **not** a second CAN bus, and **not** a DTS hardware-config surface.

| Item | Content |
| :--- | :--- |
| **Audience** | Filter, rewrite & gateway logic authors |
| **Prereq** | [peripherals.md](peripherals.md) CAN section |
| **Related** | `can_hook/can_hook.h` · `vfs/can` |

---

## Contents

1. [Model](#1-model)
2. [API](#2-api)
3. [Return Conventions](#3-return-conventions)
4. [Overriding](#4-overriding)
5. [Don'ts](#5-donts)

---

## 1. Model

```text
device_open/close/read/write/ioctl(CAN)
  → vfs-can
  → can_hook_on_* / pre_tx / filter_match / …
       ├─ weak default: pass-through / match-all
       └─ platform strong symbols: filter, ID rewrite, accounting, reject …
  → can_bus_* → hal_can_*
```

- Default (weak only): behaves like plain Classic CAN.
- Strong symbols stack a protocol superset on the same path, **without** changing VFS call sites.

---

## 2. API

| Symbol | When |
| :--- | :--- |
| `can_hook_on_open` | successful open path |
| `can_hook_on_close` | close |
| `can_hook_pre_tx` | before TX (may edit `can_frame`) |
| `can_hook_post_tx` | after TX (incl. the driver return) |
| `can_hook_filter_match` | RX filtering; unmatched frames stay hidden |
| `can_hook_on_rx` | after match, before the read path |
| `can_hook_on_err` | error path |

Declared in `can_hook/can_hook.h`; frames use `struct can_frame` from `hal_can.h`.

---

## 3. Return Conventions

Consistent with the rest of the repo: `VFS_OK` (or hook-defined 0) on success; `VFS_ERR_*` on failure.

| Scenario | Guidance |
| :--- | :--- |
| reject TX (`pre_tx`) | return an error; VFS stops the TX |
| drop (`filter_match`) | return "no-match" (per implementation; don't confuse with HW filtering) |
| rewrite (`on_rx`) | edit `frame`, return OK |

Exact non-zero codes follow the `can_hook.c` weak implementation and `vfs-can.c` call sites.

---

## 4. Overriding

Provide a same-name strong symbol in the **platform project**, e.g. `platform/can_hook_gateway.c`:

```c
int can_hook_pre_tx(struct device* pdev, struct can_frame* frame)
{
    /* e.g. rewrite ID or reject */
    (void)pdev;
    (void)frame;
    return VFS_OK;
}
```

Don't put hook implementations into the middleware defaults (unless a generic lib behind explicit Kconfig).

---

## 5. Don'ts

- No `printf`, big locks, or blocking waits in hooks (especially RX/TX hot paths) — see [fast_path.md](fast_path.md).
- Don't use hooks to replace DTS baud-rate / pin / mailbox configuration.
- Don't assume the hook sees "another physical bus" — it is still the same `hal_can` controller.

---

## Related Documents

- [peripherals.md](peripherals.md) · [service_spec.md](service_spec.md) · [fast_path.md](fast_path.md)
