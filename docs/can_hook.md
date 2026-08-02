# CAN Hook（协议超集钩子 / Protocol Superset Hooks）

> Classic CAN 的**弱符号钩子**：VFS 开闭读写一律经过；无强符号时等于透传。
> **Weak-symbol hooks** on Classic CAN: every VFS open/close/read/write passes through; without strong symbols it's pass-through.
> **不是**第二条 CAN 总线，也**不是** DTS 硬件配置面。
> It is **not** a second CAN bus, and **not** a DTS hardware-config surface.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 要做过滤 / 改写 / 网关逻辑的人 / Filter, rewrite & gateway logic authors |
| **前置 / Prereq** | [peripherals.md](peripherals.md) CAN 节 / section |
| **相关 / Related** | `can_hook/can_hook.h` · `vfs/can` |

---

## 目录 / Contents

1. [模型 / Model](#1-模型--model)
2. [API](#2-api)
3. [返回值约定 / Return Conventions](#3-返回值约定--return-conventions)
4. [怎么覆盖 / Overriding](#4-怎么覆盖--overriding)
5. [不要做什么 / Don'ts](#5-不要做什么--donts)

---

## 1. 模型 / Model

```text
device_open/close/read/write/ioctl(CAN)
  → vfs-can
  → can_hook_on_* / pre_tx / filter_match / …
       ├─ 弱默认：透传 / 全匹配 / weak default: pass-through / match-all
       └─ 平台强符号：过滤、改 ID、记账、拒发 … / strong symbols: filter, ID rewrite, accounting, reject …
  → can_bus_* → hal_can_*
```

- 默认（仅 weak）：行为 = 普通 Classic CAN。
  Default (weak only): behaves like plain Classic CAN.
- 强符号：同一路径叠加协议超集，**无需**改 VFS 调用方式。
  Strong symbols stack a protocol superset on the same path, **without** changing VFS call sites.

---

## 2. API

| 符号 / Symbol | 时机 / When |
| :--- | :--- |
| `can_hook_on_open` | 设备 open 成功路径 / successful open path |
| `can_hook_on_close` | close |
| `can_hook_pre_tx` | 发送前（可改 `can_frame`）/ before TX (may edit `can_frame`) |
| `can_hook_post_tx` | 发送后（含底层返回值）/ after TX (incl. the driver return) |
| `can_hook_filter_match` | 接收过滤；不匹配则上层看不到帧 / RX filtering; unmatched frames stay hidden |
| `can_hook_on_rx` | 匹配后、交给读路径前 / after match, before the read path |
| `can_hook_on_err` | 错误路径 / error path |

声明见 `can_hook/can_hook.h`；帧类型用 `hal_can.h` 的 `struct can_frame`。
Declared in `can_hook/can_hook.h`; frames use `struct can_frame` from `hal_can.h`.

---

## 3. 返回值约定 / Return Conventions

与仓库其它层一致：成功 `VFS_OK`（或钩子约定的 0）；失败 `VFS_ERR_*`。
Consistent with the rest of the repo: `VFS_OK` (or hook-defined 0) on success; `VFS_ERR_*` on failure.

| 场景 / Scenario | 建议 / Guidance |
| :--- | :--- |
| `pre_tx` 拒绝发送 / reject TX | 返回错误，VFS 不再下发 / return an error; VFS stops the TX |
| `filter_match` 丢弃 / drop | 返回「不匹配」语义（以实现为准，勿与硬件滤波混淆）/ return "no-match" (per implementation; don't confuse with HW filtering) |
| `on_rx` 改写帧 / rewrite | 改 `frame` 后返回 OK / edit `frame`, return OK |

具体非 0 码以 `can_hook.c` weak 实现与 `vfs-can.c` 调用点为准。
Exact non-zero codes follow the `can_hook.c` weak implementation and `vfs-can.c` call sites.

---

## 4. 怎么覆盖 / Overriding

在**平台工程**提供同名强符号并链入固件，例如 `platform/can_hook_gateway.c`：
Provide a same-name strong symbol in the **platform project**, e.g. `platform/can_hook_gateway.c`:

```c
int can_hook_pre_tx(struct device* pdev, struct can_frame* frame)
{
    /* 例：改写 ID 或拒绝 / e.g. rewrite ID or reject */
    (void)pdev;
    (void)frame;
    return VFS_OK;
}
```

不要把钩子实现放进中间件公共默认（除非做通用库并显式 Kconfig）。
Don't put hook implementations into the middleware defaults (unless a generic lib behind explicit Kconfig).

---

## 5. 不要做什么 / Don'ts

- 在钩子里 `printf`、拿大锁、阻塞等待（尤其 RX/TX 热路径）— 见 [fast_path.md](fast_path.md)。
  No `printf`, big locks, or blocking waits in hooks (especially RX/TX hot paths) — see [fast_path.md](fast_path.md).
- 用钩子代替 DTS 里的比特率 / 管脚 / 邮箱配置。
  Don't use hooks to replace DTS baud-rate / pin / mailbox configuration.
- 假设钩子能看到「另一条物理总线」— 仍是同一 `hal_can` 控制器。
  Don't assume the hook sees "another physical bus" — it is still the same `hal_can` controller.

---

## 相关文档 / Related Documents

- [peripherals.md](peripherals.md) · [service_spec.md](service_spec.md) · [fast_path.md](fast_path.md)
