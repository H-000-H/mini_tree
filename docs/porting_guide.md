# 硬件移植指南 / Hardware Porting Guide

> 把 mini_tree 接到具体 MCU：DTS、HAL 强符号、中断与安全、链接与验收。
> Porting mini_tree to a concrete MCU: DTS, HAL strong symbols, interrupts & safety, linking & acceptance.

| 项 / Item | 内容 / Description |
| :--- | :--- |
| **读者 / Audience** | 板级 / BSP 工程师<br>Board / BSP engineers |
| **前置 / Prereq.** | [getting_started.md](getting_started.md) |
| **相关 / Related** | [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) |

---

## 目录 / Table of Contents

1. [移植目标 / Porting Goals](#1-移植目标-porting-goals)
2. [步骤总览 / Step Overview](#2-步骤总览-step-overview)
3. [DTS / DTSI](#3-dts-dtsi)
4. [HAL 强符号 / HAL Strong Symbols](#4-hal-强符号-hal-strong-symbols)
5. [USB / TinyUSB](#5-usb-tinyusb)
6. [中断与安全 / Interrupts and Safety](#6-中断与安全-interrupts-and-safety)
7. [链接与段 / Linking and Sections](#7-链接与段-linking-and-sections)
8. [验收与禁止事项 / Acceptance and Forbidden Items](#8-验收与禁止事项-acceptance-and-forbidden-items)

---

## 1. 移植目标 / Porting Goals

完成后应满足：

The port is complete when the following hold:

1. 平台工程能配置、生成、链接（通用 CMake：`add_subdirectory`；**ESP-IDF** 见 [esp_idf_cmake.md](esp_idf_cmake.md)）。
   The platform project can configure, generate, and link (generic CMake: `add_subdirectory`; **ESP-IDF**: see [esp_idf_cmake.md](esp_idf_cmake.md)).
2. 每个用到的外设：DTS 节点 `status = "okay"` + 对应 `hal_*_<soc>.c` 已覆盖 weak。
   Every peripheral in use has a DTS node with `status = "okay"` and a matching `hal_*_<soc>.c` overriding the weak stub.
3. `board_driver_probe_all` 对关键外设返回成功或可接受的 WARNING。
   `board_driver_probe_all` returns success or an acceptable WARNING for the critical peripherals.
4. 业务只通过 `device_*` 访问硬件。
   Business code touches hardware only through the `device_*` API.

产品驱动共 37 个，位于 `drivers/<chip>/{include,src}`，全部经 `DRIVER_REGISTER` 注册、dtc-lite 编译期 probe（compatible 在生成期绑定，运行期不再 `strcmp`）。

There are 37 product drivers under `drivers/<chip>/{include,src}`, all registered with `DRIVER_REGISTER` and probed at compile time by dtc-lite (the `compatible` string is bound at generation time — no runtime `strcmp`).

### 一份 mini 配多 MCU（已支持）/ One mini, Many MCUs (Supported)

中间件（shelf / `mini_tree`）保持 **纯架构**：占位 DTS、weak HAL、通用 `dt-bindings`、VFS/bus/drivers。
The middleware (shelf / `mini_tree`) stays **pure architecture**: placeholder DTS, weak HAL, generic `dt-bindings`, and VFS/bus/drivers.

每个板工程自带：

Each board project brings its own:

| 板侧 / Board side | 作用 / Role |
| :--- | :--- |
| `board_port.cmake`（或 `MINI_TREE_BOARD_PORT`） | 注入 `BOARD_DTS` / `BOARD_DTSI_DIR` / 芯片 dtc `-I/-D` / 树外扫描<br>Injects `BOARD_DTS` / `BOARD_DTSI_DIR` / chip dtc `-I/-D` / out-of-tree scan dirs |
| `board_<soc>/{dts,dtsi}` | 真实设备树（**不进**中间件）<br>Real device trees (**not** part of the middleware) |
| `hal_<soc>/` | HAL 强符号<br>HAL strong symbols |
| （可选）`driver_ws2812` 等厂商例外 | 不进通用 drivers<br>Kept out of the generic `drivers/` tree |

同一物理 `mini_tree` 可用 symlink/子模块挂到多块板；**按板分别构建**（每板一份 `build/` + 生成表），不是单次链接塞多套 SoC。

The same physical `mini_tree` can be attached to multiple boards via symlink/submodule; **build per board** (each board gets its own `build/` plus generated tables) — one link step never mixes multiple SoCs.

---

## 2. 步骤总览 / Step Overview

| # | 动作 / Action | 产出 / Output |
| :---: | :--- | :--- |
| 1 | 选定 OSAL / SYSTEM（`.config`）<br>Pick the OSAL / SYSTEM backend (`.config`) | `config.h` |
| 2 | 编写 board dts/dtsi<br>Write board dts/dtsi | `BOARD_DTS` 指向真实入口<br>`BOARD_DTS` points at the real entry |
| 3 | 配置 `VENDOR_INC_DIRS`<br>Configure `VENDOR_INC_DIRS` | dtsi 宏可展开<br>dtsi macros expand |
| 4 | 实现并链接 HAL `.c`<br>Implement and link HAL `.c` | 强符号覆盖<br>Strong symbols override the weak stubs |
| 5 | （可选）`usb_tusb_port`<br>(Optional) `usb_tusb_port` | USB 通路<br>USB path |
| 6 | 接中断 / safety<br>Wire interrupts / safety | 可进 `safe_state`<br>Can enter `safe_state` |
| 7 | 点火 + 冒烟<br>Boot + smoke test | UART/GPIO/… |

---

## 3. DTS / DTSI

1. 不要改中间件占位 `board.dts` 当正式板级文件；在**平台树**维护正式 DTS。
   Don't repurpose the middleware placeholder `board.dts` as the real board file; maintain the real DTS in the **platform tree**.
2. 每个外设节点：`compatible` 必须与仓库内 `DRIVER_REGISTER` 字符串一致（见 [driver_guide.md](driver_guide.md) §4）。
   Every peripheral node's `compatible` must match a `DRIVER_REGISTER` string in the repo (see [driver_guide.md](driver_guide.md) §4).
3. 引脚/时钟/DMA/位时序等属性用**厂商宏**；确保 dtc-lite 能 `#include` 到定义它们的头。
   Use **vendor macros** for pin/clock/DMA/bit-timing properties; make sure dtc-lite can `#include` the headers that define them.
4. `status = "disabled"` 的节点不会进入有效 probe 集（按生成逻辑）。
   Nodes with `status = "disabled"` are excluded from the effective probe set (per the generator).
5. `chosen`（如调度 tick 定时器）写入后会出现在 `board_handles.h` / `CHOSEN_*`。
   A `chosen` entry (e.g. the scheduler tick timer) shows up in `board_handles.h` / `CHOSEN_*`.

> 占位 `board/dts/board.dts` 仅含 `compatible = "mini-tree,placeholder"` 根节点：能编过、生成空表，但**无任何板级节点**；正式板级必须由 `BOARD_DTS` 注入。

> The placeholder `board/dts/board.dts` only has a `compatible = "mini-tree,placeholder"` root node: it compiles and generates empty tables, but carries **no board nodes**; a real board must inject `BOARD_DTS`.

---

## 4. HAL 强符号 / HAL Strong Symbols

| 规则 / Rule | 说明 / Description |
| :--- | :--- |
| 签名 / Signature | 严格匹配 `hal/<periph>/hal_<periph>.h`<br>Match `hal/<periph>/hal_<periph>.h` exactly |
| 覆盖 / Override | 平台 `.c` 与中间件 weak stub **同名函数**；链接时强符号胜出<br>Platform `.c` defines **same-named functions** as the middleware weak stubs; the strong symbol wins at link time |
| 头文件 / Headers | 厂商头**只**出现在平台 `.c`，不要改中间件 `.h` 去 include<br>Vendor headers appear **only** in platform `.c`; don't edit middleware `.h` to include them |
| 返回值 / Return value | `int` + `VFS_ERR_*`；禁止 `void` 业务 API<br>`int` + `VFS_ERR_*`; no `void` business APIs |
| 配置 / Config | 从 `pdev`/`host` 上已填好的 cfg 读字段，勿再解析 DTS<br>Read fields from the cfg already filled on `pdev`/`host`; don't re-parse DTS |

建议每外设一个文件：`hal_gpio_<soc>.c`、`hal_uart_<soc>.c`、…
Prefer one file per peripheral: `hal_gpio_<soc>.c`, `hal_uart_<soc>.c`, …

特殊：`hal_usb` 实现文件需 `#define HAL_USB_IMPL` 再包含头（头内有 poison）。
Special case: the `hal_usb` implementation must `#define HAL_USB_IMPL` before including the header (it carries a poison).

平台 HAL/驱动代码遵循 `.clang-format`（Allman、单语句去括号、4 空格、100 列）与分层 `.clang-tidy`（`readability-identifier-naming` 强制小写命名，如 `hal_*` / `x_task` / `list_node` / `k_tag`）；app 层为建议，app 以下为强规定。

Platform HAL/driver code follows `.clang-format` (Allman braces, no braces on single statements, 4-space indent, 100 columns) and the layered `.clang-tidy` (`readability-identifier-naming` enforces lowercase names such as `hal_*` / `x_task` / `list_node` / `k_tag`); it is recommended at `app` and mandatory below `app`.

---

## 5. USB / TinyUSB

完整契约（API 表、生命周期、验收）见 **[usb_tusb_port.md](usb_tusb_port.md)**。摘要：

The full contract (API table, lifecycle, acceptance) lives in **[usb_tusb_port.md](usb_tusb_port.md)**. Summary:

- TinyUSB 经 `cmake/tinyusb.cmake` **配置期 FetchContent** 拉取（local-or-fetch，不再 vendor 于 `lib/`）；板级粘合头**不要**同时暴露 TinyUSB osal 与 mini_tree osal 冲突符号。
  TinyUSB is pulled via **config-time FetchContent** (`cmake/tinyusb.cmake`, local-or-fetch; no longer vendored under `lib/`); board glue headers must **not** expose both TinyUSB osal and mini_tree osal conflicting symbols.
- 契约头在中间件 `bus/usb/usb_tusb_port.h`（含 ECM 帧回调）；平台实现全部符号，`bus/usb` 只经此调用。
  The contract header is `bus/usb/usb_tusb_port.h` in the middleware (incl. the ECM frame callback); the platform implements every symbol and `bus/usb` only calls through it.
- 外设 compatible / ioctl：[peripherals.md](peripherals.md)。
  Peripheral compatibles / ioctls: [peripherals.md](peripherals.md).

**可裁剪（软编码）**：USB 通路由 `mini_tree/.config` 的 `CONFIG_USB` 控制（缺省启用；置 `# CONFIG_USB is not set` 则 `cmake/tinyusb.cmake` 不拉取，`bus/usb`、`vfs/usb`、`hal/usb` 与 dtc-lite 扫描全部不编入）。

**Trim (soft-coded)**: the USB path is controlled by `CONFIG_USB` in `mini_tree/.config` (enabled by default; setting `# CONFIG_USB is not set` skips the `cmake/tinyusb.cmake` fetch and excludes `bus/usb`, `vfs/usb`, `hal/usb`, and the dtc-lite scan entirely).

裁剪后板级无需提供 `usb_tusb_port`；IDE 解析不受影响（`compile_flags.txt` + `ide/stubs` 与构建解耦）。

When trimmed, the board does not need to provide `usb_tusb_port`; IDE resolution is unaffected (`compile_flags.txt` + `ide/stubs` are decoupled from the build).

---

## 6. 中断与安全 / Interrupts and Safety

| 项 / Item | 建议 / Recommendation |
| :--- | :--- |
| VIRQ | 平台 ISR → 上半部 → 下半部；见 [runtime_services.md](runtime_services.md)<br>Platform ISR → top half → bottom half; see [runtime_services.md](runtime_services.md) |
| 上半部 / Top half | 只做清标志 + submit；重活下半部<br>Only clear flags + submit; heavy work goes to the bottom half |
| `hal_platform_safety` / `hal_amp` | 安全策略 + 多核见 [amp.md](amp.md)<br>Safety policy + multi-core: see [amp.md](amp.md) |
| shutdown 回调 / Shutdown callback | 仅在 probe 阶段 `board_safety_register_shutdown`<br>Register via `board_safety_register_shutdown` only during probe |
| CAN 协议扩展 / CAN protocol ext. | 弱钩子 [can_hook.md](can_hook.md)，勿改 DTS 当协议层<br>Weak hooks [can_hook.md](can_hook.md); don't abuse DTS as a protocol layer |

---

## 7. 链接与段 / Linking and Sections

- 加入 `error_symbols.ld` 中 `ERR_SECTION_BASE` 的意图（或平台等价 `PROVIDE`）。
  Include the `ERR_SECTION_BASE` section intent from `error_symbols.ld` (or a platform-equivalent `PROVIDE`).
- 确认 C++ 若启用：按工程要求 `-fno-rtti` / `-fno-exceptions`（根 CMake 在 `SYSTEM_CPP` 时有示例）。
  If C++ is enabled, confirm `-fno-rtti` / `-fno-exceptions` per project requirements (the root CMake shows an example under `SYSTEM_CPP`).
- FreeRTOS/RT-Thread：堆、钩子、SysTick 端口在平台侧完备。
  FreeRTOS/RT-Thread: the heap, hooks, and the SysTick port live on the platform side.

---

## 8. 验收与禁止事项 / Acceptance and Forbidden Items

### 验收 / Acceptance

- [ ] 任意 `hal_*` 抽测不再永远 `VFS_ERR_NOTSUPP`
- [ ] No `hal_*` sample probe returns `VFS_ERR_NOTSUPP` forever
- [ ] `device_find` 找得到关键 label/compatible
- [ ] `device_find` locates the key label/compatible
- [ ] 复位多次稳定
- [ ] Stable across multiple resets
- [ ] clangd 在中间件根目录无系统性缺头
- [ ] clangd reports no systematic missing headers at the middleware root

### 禁止 / Forbidden

- 在 `vfs/` / `bus/` 里调用 LL/Cube/ESP API
- Calling LL/Cube/ESP APIs from `vfs/` / `bus/`
- 为图省事去掉 bus 头上的 `poison`
- Removing the `poison` on bus headers for convenience
- 把 SoC 专用 dtsi 提交进中间件默认树冒充通用
- Committing SoC-specific dtsi into the middleware default tree as if generic

---

## 相关文档 / Related Docs

- [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [faq.md](faq.md) · [problem_summary.md](problem_summary.md)
- [architecture.md](architecture.md)
