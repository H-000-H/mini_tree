# CAN Hook（协议超集钩子）

> Classic CAN 的**弱符号钩子**：VFS 开闭读写一律经过；无强符号时等于透传。
> **不是**第二条 CAN 总线，也**不是** DTS 硬件配置面。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 要做过滤 / 改写 / 网关逻辑的人 |
| **前置** | [peripherals.md](peripherals.md) CAN 节 |
| **相关** | `can_hook/can_hook.h` · `vfs/can` |

---

## 目录

1. [模型](#1-模型)
2. [API](#2-api)
3. [返回值约定](#3-返回值约定)
4. [怎么覆盖](#4-怎么覆盖)
5. [不要做什么](#5-不要做什么)

---

## 1. 模型

```text
device_open/close/read/write/ioctl(CAN)
  → vfs-can
  → can_hook_on_* / pre_tx / filter_match / …
       ├─ 弱默认：透传 / 全匹配
       └─ 平台强符号：过滤、改 ID、记账、拒发 …
  → can_bus_* → hal_can_*
```

- 默认（仅 weak）：行为 = 普通 Classic CAN。
- 强符号：同一路径叠加协议超集，**无需**改 VFS 调用方式。

---

## 2. API

| 符号 | 时机 |
| :--- | :--- |
| `can_hook_on_open` | 设备 open 成功路径 |
| `can_hook_on_close` | close |
| `can_hook_pre_tx` | 发送前（可改 `can_frame`） |
| `can_hook_post_tx` | 发送后（含底层返回值） |
| `can_hook_filter_match` | 接收过滤；不匹配则上层看不到帧 |
| `can_hook_on_rx` | 匹配后、交给读路径前 |
| `can_hook_on_err` | 错误路径 |

声明见 `can_hook/can_hook.h`；帧类型用 `hal_can.h` 的 `struct can_frame`。

---

## 3. 返回值约定

与仓库其它层一致：成功 `MINI_OK`（或钩子约定的 0）；失败 `MINI_ERR_*`。

| 场景 | 建议 |
| :--- | :--- |
| `pre_tx` 拒绝发送 | 返回错误，VFS 不再下发 |
| `filter_match` 丢弃 | 返回「不匹配」语义（以实现为准，勿与硬件滤波混淆） |
| `on_rx` 改写帧 | 改 `frame` 后返回 OK |

具体非 0 码以 `can_hook.c` weak 实现与 `vfs-can.c` 调用点为准。

---

## 4. 怎么覆盖

在**平台工程**提供同名强符号并链入固件，例如 `platform/can_hook_gateway.c`：

```c
int can_hook_pre_tx(struct device* pdev, struct can_frame* frame)
{
    /* 例：改写 ID 或拒绝 */
    (void)pdev;
    (void)frame;
    return MINI_OK;
}
```

不要把钩子实现放进中间件公共默认（除非做通用库并显式 Kconfig）。

---

## 5. 不要做什么

- 在钩子里 `printf`、拿大锁、阻塞等待（尤其 RX/TX 热路径）— 见 [fast_path.md](fast_path.md)。
- 用钩子代替 DTS 里的比特率 / 管脚 / 邮箱配置。
- 假设钩子能看到「另一条物理总线」— 仍是同一 `hal_can` 控制器。

---

## 相关文档

- [peripherals.md](peripherals.md) · [service_spec.md](service_spec.md) · [fast_path.md](fast_path.md)
