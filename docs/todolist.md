# 待优化 / To-Do List

> 可执行的改进项。完成后移到 [CHANGELOG.md](../CHANGELOG.md)，并在 [roadmap.md](roadmap.md) 勾选对应阶段。
> Actionable improvements. When done, move them to [CHANGELOG.md](../CHANGELOG.md) and tick the matching stage in [roadmap.md](roadmap.md).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **相关 / Related** | [roadmap.md](roadmap.md) · [CONTRIBUTING.md](../CONTRIBUTING.md) |

---

## 文档 / 工程体验 / Docs & Developer Experience

- [ ] 平台 DTS compatible 与仓库 `DRIVER_REGISTER` 的自动一致性检查 / automated consistency check between platform DTS `compatible` and in-repo `DRIVER_REGISTER`
- [ ] `ide/stubs` 与真实 genconfig/dtc 输出的定期同步脚本 / periodic script to sync `ide/stubs` with real genconfig/dtc output
- [ ] 文档内锚点链接的 CI 检查（可选）/ CI check for in-doc anchor links (optional)
- [x] USB / 外设 ioctl / AMP / can_hook / EventBus 等缺口文档（见 `docs/*`）/ gap docs for USB / peripheral ioctl / AMP / can_hook / EventBus (see `docs/*`)
- [x] 开源积木文档与 hybrid Fetch 策略对齐（基础设施 vendor；其余 Fetch）/ OSS brick docs aligned with the hybrid Fetch policy (infrastructure vendored; the rest fetched)
- [x] 旧命名统一入文档与代码：kTag→`k_tag`、struct Event→`struct event`、MiniTree→`mini_tree`、xTask→`x_task`、ListNode→`list_node`、Fifo_Data_type→`fifo_data_type` 等 / legacy naming unified across docs and code: kTag→`k_tag`, struct Event→`struct event`, MiniTree→`mini_tree`, xTask→`x_task`, ListNode→`list_node`, Fifo_Data_type→`fifo_data_type`, etc.

---

## 代码 / Code

- [ ] 统一各 bus 的 `*_BUS_IMPL` / poison 宏命名 / unify `*_BUS_IMPL` / poison macro naming across buses
- [ ] HAL 注释进一步减少厂商字面（保留「直投」语义）/ further reduce vendor-specific wording in HAL comments (keeping the "direct pass-through" semantics)
- [ ] `usb_tusb_port` 最小契约的宿主无关测试桩 / host-independent test stub for the `usb_tusb_port` minimal contract
- [ ] AMP/`CPU_CORES=2` **平台侧**可运行样例（文档见 [amp.md](amp.md)）/ runnable **platform-side** sample for AMP/`CPU_CORES=2` (docs: [amp.md](amp.md))

---

## 构建 / CI / Build & CI

- [ ] 无厂商 SDK 时：配置 + dtc-lite(占位 DTS) + 编译 `mini_tree` smoke / without a vendor SDK: configure + dtc-lite (placeholder DTS) + compile `mini_tree` smoke
- [ ] `CONFIG_BUILD_DISASM` 在某一参考平台的启用示例 / sample enabling `CONFIG_BUILD_DISASM` on a reference platform

---

## 相关文档 / Related Docs

- [CHANGELOG.md](../CHANGELOG.md) · [faq.md](faq.md)
