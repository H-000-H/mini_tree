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
| `status.h` 中 `MINI_OK` / `MINI_ERR_*` 语义与 `mt_err_t` 类型名 | 自持编号（0 成功 / 负数失败），**跨工具链数值稳定**；编号与类型名本身即契约 |
| `mini_backend.h` 公共函数集 | 四后端共同表面 |
| `mini_backend.h` 的 C++ 重载 `mini_task_create`（裸机专属） | 仅 `CONFIG_OS_BARE` + `CONFIG_XTASK_PREEMPT` + `__cplusplus`；**协调式**（`CONFIG_XTASK_COOP`）时 `period` 为周期 ms、`param1` 为 `x_task*` TCB；**抢占式**（`CONFIG_XTASK_PREEMPT`）时第三参重解释为 `priority`（数值越大越优先） |
| HAL **函数名**与配置结构体**字段名** | 平台按头文件实现 |

---

## 错误码编号与命名空间边界

`status.h` **自持编号**，不依赖 `<errno.h>`（旧版为 `-EINVAL` 等 errno 别名，数值会随 libc 漂移）。分段如下：

| 区间 | 归属 | 说明 |
| :--- | :--- | :--- |
| `0` | 成功 | `MINI_OK` |
| `-1 .. -63` | 通用 / 栈层 | `status.h` 的全部 `MINI_ERR_*`；`-28 .. -63` 预留扩容 |
| `-64 .. -511` | 子体系（每片 32 码，共 14 片） | 0 net / 1 fs / 2 ota / 3 log / 4 system / 5 driver（驱动·板级）/ 6..13 预留 |

- 幅度上限 `MINI_ERR_MAX`（`511`）——`0` 成功 + 511 个错误码 = 512 个码位；`ERR_PTR` 用 `ERR_SECTION_BASE + 幅度` 编码，仍落在 `error_symbols.ld` 保留的那段地址内，链接脚本无需改动。
- 私有码用 `MINI_ERR_SUBSYS(base, idx)` 构造（`base` 取 `MINI_ERR_SUBSYS_*_BASE`，`idx` 0..31，越片即侵占下一片），或 `MINI_ERR_BUILD(mag)` 直接按幅度构造；`MINI_ERR_SECTOR_OF()` / `MINI_ERR_IS_SUBSYS()` / `MINI_ERR_IS_DRIVER()` 判归属，`MINI_ERR_SUBSYS_SLOT_OF()` 取片号。
- 驱动/板级不再独占一段：它是子体系段的第 5 片（`-224 .. -255`），`MINI_ERR_IS_DRIVER()` 判该片，与 `MINI_ERR_IS_SUBSYS()` 互斥。
- `MINI_ERR_TO_STR()` 提供日志用字符串（`core/src/status.c`，未被引用时由链接器整体丢弃）。
- **类型名 `mt_err_t`**：返回错误码的函数，其返回类型写 `mt_err_t`；**参数类型保持 `int`**（C++ 侧 enum → int 隐式可行，int → enum 需显式转换，故不把参数改成枚举）。`MINI_ERR_SECTOR_OF()` 返回 `mt_err_sector_t`。
- **保持 `int` 的例外**（不是纯错误码返回，改类型会造成回调不兼容）：`file_operations.write` / `.read` 是"已传输字节数或负数错误码"的混合契约；`interrupt_top_half_t`（VIRQ 上半部）返回 `MINI_IRQ_ENTRY_BOTTOM/NOBOTTOM` 标志；`bus` host ops 的 `.role` 返回 MASTER/SLAVE；`NET_*` / `BUFF_*` / `MINI_LOG_ERR_*` 与 coreMQTT/lwIP 回调等第三方契约。

**命名空间互不混用，跨边界必须显式翻译：**

| 命名空间 | 归属 | 备注 |
| :--- | :--- | :--- |
| `MINI_OK` / `MINI_ERR_*` | `core/include/status.h` | 栈内唯一通用命名空间 |
| `MINI_OS_ERR_*` | `lib/mini-os` | 通用语义与 `MINI_ERR_*` 逐位一致（零转换）；内核私有码落 mini-os 片（`-256..-287`） |
| `NET_OK` / `NET_ERR_*` | `net/port/net_error.h` | net 包装层私有，负 errno 语义；在包装层边界翻译 |
| `BUFF_*` | `algorithm/buffer/buffer.h` | 缓冲库私有，包装 errno |
| `MINI_LOG_ERR_*` | `mini-log/inc/log_err.h` | 落 log 片（`-160..-191`）；vendor 自包含，数值写死 |
| `ERR_*` | `lib/mini-ota/bootutil/inc/err.h` | 落 ota 片（`-128..-159`）；vendor 自包含，数值写死 |

⚠ 不同命名空间的码**不可按数值直接比较**（例如 `NET_ERR_NOSPC` 与 `MINI_ERR_NOSPC` 数值不同）。

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
