# 红线区 / Red-Line Zone — 硬实时 Fast Path

> ISR、GPIO 翻转、DMA 完成回调等路径上**绝对不要做**的事；以及推荐做法。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 写驱动 / 写控制环的人 |
| **前置** | [service_spec.md](service_spec.md) · [architecture.md](architecture.md) §6 |
| **相关** | [debug_monitor.md](debug_monitor.md) |

---

## 目录 / Contents

1. [什么算 Fast Path](#1-什么算-fast-path)
2. [硬性禁止](#2-硬性禁止)
3. [推荐模式](#3-推荐模式)
4. [GPIO 快路径](#4-gpio-快路径)
5. [自检表](#5-自检表)

---

## 1. 什么算 Fast Path

| 场景 | 说明 |
| :--- | :--- |
| ISR 上半部 | `interrupt` top_half、外设 IRQ handler |
| DMA / 传输完成回调 | 常在中断上下文 |
| `hal_gpio_fast_*` | 设计为零分支热路径 |
| 控制环高频率轮询 | 若周期 < 百微秒级，按热路径约束 |

冷路径（probe、deinit、错误恢复、用户配置 ioctl）**可以**做完整校验与日志。

---

## 2. 硬性禁止

| 禁止 | 原因 |
| :--- | :--- |
| `printf` / `SYS_LOGI` 刷屏 | 阻塞、不可重入、拖长关中断时间 |
| `malloc` / 新删 | 不确定时延；可能 poison |
| 拿 mutex / 睡 | 死锁或非法 ISR 调用 |
| `strcmp`、遍历设备树、查找 device | 时延抖动 |
| 无界 for 循环等待硬件 | 应超时或改 DMA/中断 |
| 绕过 bus `poison` 调 HAL | 破坏分层，难审查 |

`interrupt.h` 亦明确：ISR 内禁止 printf / 上锁 / 长时间阻塞。

---

## 3. 推荐模式

```text
IRQ
  → 清硬件标志
  → 可选：拷贝少量寄存器快照到静态/池缓冲
  → interrupt 提交下半部 或 osal 队列给任务
  → 返回

任务/主循环
  → 处理协议、日志、device_ioctl、状态机
```

需要「尽量快又要可移植」时：业务只调 `hal_gpio_fast_*` 或经 VFS 文档标明的 fast ioctl，仍避免在其中打日志。

---

## 4. GPIO 快路径

中间件提供：

- `hal_gpio_fast_set_level`
- `hal_gpio_fast_get_level`
- `hal_gpio_fast_toggle`

平台实现应：**无查表、无分支策略选择**（头注释约定）。配置/改模式走慢路径 `hal_gpio_set_*`。

---

## 5. 自检表

- [ ] 上半部代码行数短、无调用 OS 阻塞 API
- [ ] 日志只在任务或下半部
- [ ] 热路径无动态分配
- [ ] 超时路径返回 `MINI_ERR_TIMEOUT` 而非死等

---

## 相关文档

- [service_spec.md](service_spec.md) · [architecture.md](architecture.md)
- [debug_monitor.md](debug_monitor.md)
