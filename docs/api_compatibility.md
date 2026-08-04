# API 兼容性声明 / API Compatibility Statement

> 哪些接口意图稳定，哪些会随 DTS/Kconfig 变化，哪些明确不兼容。
> Which interfaces are intended to be stable, which vary with DTS/Kconfig, and which are explicitly incompatible.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 平台集成与版本升级负责人 / Platform integrators & release owners |
| **相关 / Related** | [CONTRIBUTING.md](../CONTRIBUTING.md) · [architecture.md](architecture.md) |

---

## 稳定面（意图保持源码级兼容）/ Stable Surface (Source-Compatible by Intent)

| API / 契约 / Contract | 说明 / Notes |
| :--- | :--- |
| `device_*` 与 `file_operations` | 应用主入口 / primary app entry |
| `DRIVER_REGISTER(name, compat, probe, remove)` 形态 / shape | 宏参数顺序与生成符号规则 / macro arg order & symbol rules |
| `status.h` 中 `VFS_OK` / `VFS_ERR_*` 语义 / semantics | 数值可能随 errno 映射，语义保持 / values may map through errno; semantics hold |
| `osal.h` 公共函数集 / public function set | 三后端共同表面 / common surface across three backends |
| `osal_null.h` 的 C++ 重载 `osal_task_create`（裸机专属）<br>bare-metal-only C++ overload in `osal_null.h` | 仅 `CONFIG_OSAL_NULL` + `CONFIG_OSAL_NULL_TASK_CPP` + `__cplusplus`；`period` 为周期 ms、`param1` 为 `x_task*` TCB<br>Only with `CONFIG_OSAL_NULL` + `CONFIG_OSAL_NULL_TASK_CPP` + `__cplusplus`; `period` in ms, `param1` is `x_task*` TCB |
| HAL **函数名**与配置结构体**字段名** / HAL function & config-field names | 平台按头文件实现 / platforms implement per header |

---

## 可能变更（不保证二进制 / 数值稳定）/ May Change (No Binary/Value Stability)

| 项 / Item | 说明 / Notes |
| :--- | :--- |
| `device_id_t` / `DEV_ID_*` | 随板级 DTS 变 / board-DTS dependent |
| `DTC_GEN_*` | 随 dtsi 聚合变 / dtsi-aggregation dependent |
| Kconfig 新选项默认值 / new-option defaults | 可能改变裁剪结果 / may change trimming |
| Bus/VFS 内部池与私有结构 / internal pools & private structs | 不对外保证布局 / layout not guaranteed |
| weak stub 行为 / weak-stub behavior | 仅返回 `NOTSUPP` 等，可加日志 / returns `NOTSUPP` etc.; may log |

---

## 明确非兼容 / 非支持 / Explicitly Incompatible / Unsupported

| 项 / Item | 说明 / Notes |
| :--- | :--- |
| 在公共头 `#include` 厂商 HAL typedef | 禁止 / forbidden |
| 业务直接调用 `hal_*`（无 bus IMPL）| 刻意 poison / deliberately poisoned |
| ARMCC v5 工具链 / toolchain | 不支持 / unsupported |
| 保证跨 major 的 ABI 稳定性 / cross-major ABI stability | 以源码集成 + Git 锁定为准 / source integration + Git pinning |

---

## 版本策略 / Versioning

- 以 Git 提交 / tag / 平台 submodule 指针锁定 / Pin via Git commits, tags, or platform submodule pointers.
- 升级中间件时：重跑 genconfig + dtc-lite，全量重编，跑 probe 与关键外设冒烟。
  On upgrade: re-run genconfig + dtc-lite, full rebuild, and probe + key-peripheral smoke tests.

---

## 相关文档 / Related Documents

- [porting_guide.md](porting_guide.md) · [CHANGELOG.md](../CHANGELOG.md)
