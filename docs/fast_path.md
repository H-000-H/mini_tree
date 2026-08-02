# 红线区 / Red-Line Zone — 硬实时 Fast Path

> ISR、GPIO 翻转、DMA 完成回调等路径上**绝对不要做**的事；以及推荐做法。
> What is **absolutely forbidden** on ISR, GPIO-toggle, DMA-complete paths — and what to do instead.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 写驱动 / 写控制环的人 / Driver & control-loop writers |
| **前置 / Prereq** | [service_spec.md](service_spec.md) · [architecture.md](architecture.md) §6 |
| **相关 / Related** | [debug_monitor.md](debug_monitor.md) |

---

## 目录 / Contents

1. [什么算 Fast Path / What Is Fast Path](#1-什么算-fast-path--what-is-fast-path)
2. [硬性禁止 / Hard Forbiddens](#2-硬性禁止--hard-forbiddens)
3. [推荐模式 / Recommended Pattern](#3-推荐模式--recommended-pattern)
4. [GPIO 快路径 / GPIO Fast Path](#4-gpio-快路径--gpio-fast-path)
5. [自检表 / Self-Check List](#5-自检表--self-check-list)

---

## 1. 什么算 Fast Path / What Is Fast Path

| 场景 / Scenario | 说明 / Notes |
| :--- | :--- |
| ISR 上半部 / Top-half | `interrupt` top_half、外设 IRQ handler |
| DMA / 传输完成回调 / Transfer-complete callbacks | 常在中断上下文 / usually in interrupt context |
| `hal_gpio_fast_*` | 设计为零分支热路径 / designed as zero-branch hot path |
| 控制环高频率轮询 / High-frequency control loops | 若周期 &lt; 百微秒级，按热路径约束 / treat as hot path below ~100 µs periods |

冷路径（probe、deinit、错误恢复、用户配置 ioctl）**可以**做完整校验与日志。
Cold paths (probe, deinit, error recovery, user-config ioctls) **may** do full checks and logging.

---

## 2. 硬性禁止 / Hard Forbiddens

| 禁止 / Forbidden | 原因 / Why |
| :--- | :--- |
| `printf` / `SYS_LOGI` 刷屏 / spam | 阻塞、不可重入、拖长关中断时间 / blocking, non-reentrant, stretches IRQ-off time |
| `malloc` / 新删 / new-delete | 不确定时延；可能 poison / unbounded latency; possibly poisoned |
| 拿 mutex / 睡 / Mutex or sleep | 死锁或非法 ISR 调用 / deadlock or illegal ISR call |
| `strcmp`、遍历设备树、查找 device / device-tree walks, device lookup | 时延抖动 / latency jitter |
| 无界 for 循环等待硬件 / Unbounded HW polls | 应超时或改 DMA/中断 / use timeouts or DMA/interrupts |
| 绕过 bus `poison` 调 HAL / Calling HAL past the bus poison | 破坏分层，难审查 / breaks layering, hard to review |

`interrupt.h` 亦明确：ISR 内禁止 printf / 上锁 / 长时间阻塞。
`interrupt.h` also states: no printf / locking / long blocking inside ISRs.

---

## 3. 推荐模式 / Recommended Pattern

```text
IRQ
  → 清硬件标志 / clear HW flags
  → 可选：拷贝少量寄存器快照到静态/池缓冲 / optionally snapshot registers into static/pooled buffers
  → interrupt 提交下半部 或 osal 队列给任务 / submit a bottom half or queue to a task
  → 返回 / return

任务/主循环 / Task or main loop
  → 处理协议、日志、device_ioctl、状态机 / protocol, logging, device_ioctl, state machines
```

需要「尽量快又要可移植」时：业务只调 `hal_gpio_fast_*` 或经 VFS 文档标明的 fast ioctl，仍避免在其中打日志。
When you need "as fast as possible and portable": call only `hal_gpio_fast_*` or VFS-documented fast ioctls — still no logging inside.

---

## 4. GPIO 快路径 / GPIO Fast Path

中间件提供 / The middleware provides:

- `hal_gpio_fast_set_level`
- `hal_gpio_fast_get_level`
- `hal_gpio_fast_toggle`

平台实现应：**无查表、无分支策略选择**（头注释约定）。配置/改模式走慢路径 `hal_gpio_set_*`。
Platform implementations must be **table-free and branch-free** (per the header contract). Configuration/mode changes go through the slow path `hal_gpio_set_*`.

---

## 5. 自检表 / Self-Check List

- [ ] 上半部代码行数短、无调用 OS 阻塞 API / short top-half, no OS blocking APIs
- [ ] 日志只在任务或下半部 / logging only in tasks or bottom halves
- [ ] 热路径无动态分配 / no dynamic allocation on hot paths
- [ ] 超时路径返回 `VFS_ERR_TIMEOUT` 而非死等 / timeout paths return `VFS_ERR_TIMEOUT`, never spin forever

---

## 相关文档 / Related Documents

- [service_spec.md](service_spec.md) · [architecture.md](architecture.md)
- [debug_monitor.md](debug_monitor.md)
