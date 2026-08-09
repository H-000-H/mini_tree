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
| `status.h` `VFS_OK` / `VFS_ERR_*` semantics | values may map through errno; semantics hold |
| `osal.h` public function set | common surface across three backends |
| `osal_null.h` C++ overload `osal_task_create` (bare-metal cooperative only) | only `CONFIG_OSAL_NULL` + `CONFIG_OSAL_NULL_TASK_CPP` + `__cplusplus` + **`!CONFIG_XTASK_PREEMPT`**; `period` in ms, `param1` is `x_task*` TCB |
| HAL function & config-field names | platforms implement per header |

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
