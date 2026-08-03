# 项目路线图 / Project Roadmap

> 中间件 shelf 的阶段目标与非目标。
> Stage goals and non-goals for the middleware shelf.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **相关 / Related** | [todolist.md](todolist.md) · [CHANGELOG.md](../CHANGELOG.md) · [design_decisions.md](design_decisions.md) |

---

## 当前阶段 / Current Stage

| 状态 / Status | 项 / Item |
| :---: | :--- |
| ✓ | 分层 board → vfs → bus → hal(weak 空实现) / layered board → vfs → bus → hal (weak empty impls) |
| ✓ | dtc-lite 编译期 probe（`DRIVER_REGISTER` 扫描） / dtc-lite compile-time probe (`DRIVER_REGISTER` scan) |
| ✓ | 产品驱动 37 个：`drivers/<chip>/{include,src}` / 37 product drivers: `drivers/<chip>/{include,src}` |
| ✓ | 通用 CMake + ESP-IDF 组件路径：`MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR` 注入；占位 `board.dts` 无板级节点亦可编过 / generic CMake + ESP-IDF component path: `MINI_TREE_BOARD_PORT` / `BOARD_DTS` / `BOARD_DTSI_DIR` injection; placeholder `board.dts` builds with no board nodes |
| ✓ | OSAL 三后端 + SYSTEM_C/CPP / OSAL three backends + SYSTEM_C/CPP |
| ✓ | 多外设矩阵（含 can/usb/i2s 等） / multi-peripheral matrix (incl. can/usb/i2s…) |
| ✓ | clangd 无 SDK 可解析（stubs + compile_flags） / clangd parses without SDK (stubs + compile_flags) |
| ✓ | 文档树：根惯例文件 + `docs/` 专题（无 examples / board Wiki）/ doc tree: root convention files + `docs/` topics (no examples / board Wiki) |
| ✓ | USB / 外设 ioctl / AMP / can_hook / 运行时服务等缺口文档 / gap docs: USB / peripheral ioctl / AMP / can_hook / runtime services |
| ✓ | 开源积木：vendor 仅 FreeRTOS / RT-Thread / ETL；TinyUSB / lwIP / cJSON 与其余积木均链接期 FetchContent（`ecosystem.md` + `dep_fetch`）/ OSS bricks: only FreeRTOS / RT-Thread / ETL vendored; all others (incl. TinyUSB / lwIP / cJSON) fetched at link time |
| ✓ | 命名统一（kTag→`k_tag`、struct Event→`struct event`、MiniTree→`mini_tree`、xTask→`x_task`、ListNode→`list_node` 等）+ 分层 `.clang-tidy`（app 层建议、app 以下强规定）/ naming unification (kTag→`k_tag`, struct Event→`struct event`, MiniTree→`mini_tree`, xTask→`x_task`, ListNode→`list_node`, …) + layered `.clang-tidy` (suggested at app layer, enforced below) |
| → | 与平台工程 DTS/API 持续同步 / keep syncing DTS/API with platform projects |
| → | CI：占位 DTS 下仅编译 smoke / CI: compile-only smoke under placeholder DTS |
| → | dt-bindings / 契约测试加强 / strengthen dt-bindings / contract tests |

---

## 参考实现 / Reference Implementations

异构多核仓库中各 `platform/*/mini_tree`：提供 HAL、完整 dtsi、`VENDOR_INC_DIRS`，验证具体 SoC。
Each `platform/*/mini_tree` in the heterogeneous multicore repo provides HAL, full dtsi, and `VENDOR_INC_DIRS`, validating a concrete SoC.

本仓**不**内嵌 SoC 专有默认 DTS。
This repo does **not** embed SoC-specific default DTS.

---

## 非目标 / Non-Goals

- 在本仓维护 Keil/Cube 工程为第一公民（含 uvprojx 生成器；远古脚本思路已废弃、不支持）/ maintaining Keil/Cube projects as first-class citizens here (incl. the uvprojx generator; the legacy script approach is deprecated and unsupported)
- 完整复刻 Linux 内核驱动核心 / fully replicating the Linux kernel driver core
- 保证跨 major 的稳定 ABI（以源码集成为主）/ guaranteeing a stable ABI across majors (source-level integration is the norm)

---

## 相关文档 / Related Docs

- [todolist.md](todolist.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) · [README.md](README.md)
