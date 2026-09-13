# API Compatibility Statement

> Which interfaces are intended to be stable, which vary with DTS/Kconfig, and which are explicitly incompatible.

| Item | Content |
| :--- | :--- |
| **Audience** | Platform integrators & release owners |
| **Related** | [CONTRIBUTING.md](../CONTRIBUTING.md) · [architecture.md](architecture.md) |

---

## Stable Surface (Source-Compatible by Intent)

| API / Contract | Notes |
| :--- | :--- |
| `device_*` & `file_operations` | primary app entry |
| `DRIVER_REGISTER(name, compat, probe, remove)` shape | macro arg order & symbol rules |
| `status.h` `MINI_OK` / `MINI_ERR_*` semantics and the `mt_err_t` type name | self-owned numbering (0 = success / negative = error), **numerically stable across toolchains**; both the numbers and the type name are the contract |
| `mini_backend.h` public function set | common surface across four backends |
| `mini_backend.h` C++ overload `mini_task_create` (bare-metal only) | only `CONFIG_OS_BARE` + `CONFIG_XTASK_PREEMPT` + `__cplusplus`; **cooperative** (`CONFIG_XTASK_COOP`): `period` in ms, `param1` is `x_task*` TCB; **preemptive** (`CONFIG_XTASK_PREEMPT`): 3rd arg reinterpreted as `priority` (higher = more urgent) |
| HAL function & config-field names | platforms implement per header |

---

## Error Code Numbering & Namespace Boundaries

`status.h` owns its numbering and does **not** include `<errno.h>` (the previous version aliased `-EINVAL` etc., so values drifted with the libc). Sectors:

| Range | Owner | Notes |
| :--- | :--- | :--- |
| `0` | success | `MINI_OK` |
| `-1 .. -63` | common / stack | all `MINI_ERR_*` in `status.h`; `-28 .. -63` reserved for growth |
| `-64 .. -511` | subsystem (32 codes per slot, 14 slots) | 0 net / 1 fs / 2 ota / 3 log / 4 system / 5 driver / 6..13 reserved |

- Magnitude cap `MINI_ERR_MAX` (`511`) — `0` plus 511 error codes = 512 slots; `ERR_PTR` encodes as `ERR_SECTION_BASE + magnitude`, which still fits the address range reserved by `error_symbols.ld`, so the linker script is unchanged.
- Private codes come from `MINI_ERR_SUBSYS(base, idx)` (`base` = `MINI_ERR_SUBSYS_*_BASE`, `idx` 0..31; a larger index spills into the next slot) or from `MINI_ERR_BUILD(mag)` directly; `MINI_ERR_SECTOR_OF()` / `MINI_ERR_IS_SUBSYS()` / `MINI_ERR_IS_DRIVER()` classify them and `MINI_ERR_SUBSYS_SLOT_OF()` returns the slot number.
- Driver/board no longer owns a dedicated range: it is slot 5 (`-224 .. -255`) of the subsystem sector, which `MINI_ERR_IS_DRIVER()` detects; it stays mutually exclusive with `MINI_ERR_IS_SUBSYS()`.
- `MINI_ERR_TO_STR()` yields log-friendly strings (`core/src/status.c`; dropped entirely by the linker when unreferenced).
- **Type name `mt_err_t`**: functions that return an error code declare their return type as `mt_err_t`, while **parameter types stay `int`** (in C++ enum → int is implicit but int → enum needs an explicit cast, so parameters are not turned into enums). `MINI_ERR_SECTOR_OF()` returns `mt_err_sector_t`.
- **Deliberate `int` exceptions** (not pure error-code returns; changing them would break callback compatibility): `file_operations.write` / `.read` use the mixed "bytes transferred or negative error" contract; `interrupt_top_half_t` (VIRQ top half) returns the `MINI_IRQ_ENTRY_BOTTOM/NOBOTTOM` flag; bus host ops `.role` returns MASTER/SLAVE; and `NET_*` / `BUFF_*` / `MINI_LOG_ERR_*` plus third-party contracts such as coreMQTT/lwIP callbacks.

**Namespaces are never mixed; translate explicitly at each boundary:**

| Namespace | Owner | Notes |
| :--- | :--- | :--- |
| `MINI_OK` / `MINI_ERR_*` | `core/include/status.h` | the single shared namespace inside the stack |
| `MINI_OS_ERR_*` | `lib/mini-os` | bit-for-bit identical to `MINI_ERR_*` for the shared meanings (conversion is a no-op); kernel-private codes live on the mini-os slot (`-256..-287`) |
| `NET_OK` / `NET_ERR_*` | `net/port/net_error.h` | private to the net wrapper, negative-errno semantics; translated at the wrapper boundary |
| `BUFF_*` | `algorithm/buffer/buffer.h` | private to the buffer library, wraps errno |
| `MINI_LOG_ERR_*` | `mini-log/inc/log_err.h` | on the log slot (`-160..-191`); self-contained vendor, hard-coded values |
| `ERR_*` | `lib/mini-ota/bootutil/inc/err.h` | on the ota slot (`-128..-159`); self-contained vendor, hard-coded values |

⚠ Codes from different namespaces must **not** be compared by value (e.g. `NET_ERR_NOSPC` and `MINI_ERR_NOSPC` now differ numerically).

---

## May Change (No Binary/Value Stability)

| Item | Notes |
| :--- | :--- |
| `device_id_t` / `DEV_ID_*` | board-DTS dependent |
| `DTC_GEN_*` | dtsi-aggregation dependent |
| new-option defaults | may change trimming |
| internal pools & private structs | layout not guaranteed |
| weak-stub behavior | returns `NOTSUPP` etc.; may log |

---

## Explicitly Incompatible / Unsupported

| Item | Notes |
| :--- | :--- |
| `#include` vendor HAL typedef in a public header | forbidden |
| business calling `hal_*` (no bus IMPL) | deliberately poisoned |
| ARMCC v5 toolchain | unsupported |
| cross-major ABI stability | source integration + Git pinning |

---

## Versioning

- Pin via Git commits, tags, or platform submodule pointers.
- On upgrade: re-run genconfig + dtc-lite, full rebuild, and probe + key-peripheral smoke tests.

---

## Related Documents

- [device_tree_porting.md](device_tree_porting.md) · [CHANGELOG.md](../CHANGELOG.md)
