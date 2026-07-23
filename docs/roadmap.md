# 项目路线图

> 中间件 shelf 的阶段目标与非目标。

| 项 | 内容 |
| :--- | :--- |
| **相关** | [todolist.md](todolist.md) · [CHANGELOG.md](../CHANGELOG.md) · [design_decisions.md](design_decisions.md) |

---

## 当前阶段

| 状态 | 项 |
| :---: | :--- |
| ✓ | 分层 board → vfs → bus → hal(weak) |
| ✓ | dtc-lite 编译期 probe |
| ✓ | OSAL 三后端 + SYSTEM_C/CPP |
| ✓ | 多外设矩阵（含 can/usb/i2s 等） |
| ✓ | clangd 无 SDK 可解析（stubs + compile_flags） |
| ✓ | 文档树：根惯例文件 + `docs/` 专题（无 examples / board Wiki） |
| ✓ | USB / 外设 ioctl / AMP / can_hook / 运行时服务等缺口文档 |
| → | 与平台工程 DTS/API 持续同步 |
| → | CI：占位 DTS 下仅编译 smoke |
| → | dt-bindings / 契约测试加强 |

---

## 参考实现

异构多核仓库中各 `platform/*/mini_tree`：提供 HAL、完整 dtsi、`VENDOR_INC_DIRS`，验证具体 SoC。

本仓**不**内嵌 SoC 专有默认 DTS。

---

## 非目标

- 在本仓维护 Keil/Cube 工程为第一公民（含 uvprojx 生成器；远古脚本思路已废弃、不支持）  
- 完整复刻 Linux 内核驱动核心  
- 保证跨 major 的稳定 ABI（以源码集成为主）  

---

## 相关文档

- [todolist.md](todolist.md) · [architecture.md](architecture.md) · [README.md](README.md)
