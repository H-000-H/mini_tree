# API 兼容性声明

> 哪些接口意图稳定，哪些会随 DTS/Kconfig 变化，哪些明确不兼容。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 平台集成与版本升级负责人 |
| **相关** | [CONTRIBUTING.md](../CONTRIBUTING.md) · [architecture.md](architecture.md) |

---

## 稳定面（意图保持源码级兼容）

| API / 契约 | 说明 |
| :--- | :--- |
| `device_*` 与 `file_operations` | 应用主入口 |
| `DRIVER_REGISTER(name, compat, probe, remove)` 形态 | 宏参数顺序与生成符号规则 |
| `status.h` 中 `VFS_OK` / `VFS_ERR_*` 语义 | 数值可能随 errno 映射，语义保持 |
| `osal.h` 公共函数集 | 三后端共同表面 |
| `osal_null.h` 的 C++ 重载 `osal_task_create`（裸机专属） | 仅 `CONFIG_OSAL_NULL` + `CONFIG_OSAL_NULL_TASK_CPP` + `__cplusplus`；**协调式**（`CONFIG_XTASK_COOP`）时 `period` 为周期 ms、`param1` 为 `x_task*` TCB；**抢占式**（`CONFIG_XTASK_PREEMPT`）时第三参重解释为 `priority`（数值越大越优先） |
| HAL **函数名**与配置结构体**字段名** | 平台按头文件实现 |

---

## 可能变更（不保证二进制 / 数值稳定）

| 项 | 说明 |
| :--- | :--- |
| `device_id_t` / `DEV_ID_*` | 随板级 DTS 变 |
| `DTC_GEN_*` | 随 dtsi 聚合变 |
| Kconfig 新选项默认值 | 可能改变裁剪结果 |
| Bus/VFS 内部池与私有结构 | 不对外保证布局 |
| weak stub 行为 | 仅返回 `NOTSUPP` 等，可加日志 |

---

## 明确非兼容 / 非支持

| 项 | 说明 |
| :--- | :--- |
| 在公共头 `#include` 厂商 HAL typedef | 禁止 |
| 业务直接调用 `hal_*`（无 bus IMPL） | 刻意 poison |
| ARMCC v5 工具链 | 不支持 |
| 保证跨 major 的 ABI 稳定性 | 以源码集成 + Git 锁定为准 |

---

## 版本策略

- 以 Git 提交 / tag / 平台 submodule 指针锁定。
- 升级中间件时：重跑 genconfig + dtc-lite，全量重编，跑 probe 与关键外设冒烟。

---

## 相关文档

- [device_tree_porting.md](device_tree_porting.md) · [CHANGELOG.md](../CHANGELOG.md)
