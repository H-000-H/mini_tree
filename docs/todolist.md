# 待优化

> 可执行的改进项。完成后移到 [CHANGELOG.md](../CHANGELOG.md)，并在 [roadmap.md](roadmap.md) 勾选对应阶段。

| 项 | 内容 |
| :--- | :--- |
| **相关** | [roadmap.md](roadmap.md) · [CONTRIBUTING.md](../CONTRIBUTING.md) |

---

## 文档 / 工程体验

- [ ] 平台 DTS compatible 与仓库 `DRIVER_REGISTER` 的自动一致性检查  
- [ ] `ide/stubs` 与真实 genconfig/dtc 输出的定期同步脚本  
- [ ] 文档内锚点链接的 CI 检查（可选）  
- [x] USB / 外设 ioctl / AMP / can_hook / EventBus 等缺口文档（见 `docs/*`）  

---

## 代码

- [ ] 统一各 bus 的 `*_BUS_IMPL` / poison 宏命名  
- [ ] HAL 注释进一步减少厂商字面（保留「直投」语义）  
- [ ] `usb_tusb_port` 最小契约的宿主无关测试桩  
- [ ] AMP/`CPU_CORES=2` **平台侧**可运行样例（文档见 [amp.md](amp.md)）  

---

## 构建 / CI

- [ ] 无厂商 SDK 时：配置 + dtc-lite(占位 DTS) + 编译 `mini_tree` smoke  
- [ ] `CONFIG_BUILD_DISASM` 在某一参考平台的启用示例  

---

## 相关文档

- [CHANGELOG.md](../CHANGELOG.md) · [faq.md](faq.md)
