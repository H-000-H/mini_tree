# USB TinyUSB 板级契约（`usb_tusb_port`）/ USB TinyUSB Board Contract (`usb_tusb_port`)

> 平台必须提供的 TinyUSB 粘合层：符号、生命周期、与 `bus/usb` 的调用关系。
> The TinyUSB glue layer every platform must provide: symbols, lifecycle, and its call relationship with `bus/usb`.
>
> 契约头在中间件（`bus/usb/usb_tusb_port.h`），平台树**只实现符号**，无需复制头。
> The contract header lives in the middleware (`bus/usb/usb_tusb_port.h`); the platform tree **implements symbols only** — do not copy the header.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 做 USB 移植的板级工程师 / Board engineers porting USB |
| **前置 / Prerequisites** | [porting_guide.md](porting_guide.md) · [peripherals.md](peripherals.md) |
| **相关 / Related** | [driver_guide.md](driver_guide.md) · TinyUSB 在 `lib/tinyusb` · [ecosystem.md](ecosystem.md) |

---

## 目录 / Contents

1. [为什么单独一层 / Why a Separate Layer](#1-为什么单独一层-why-a-separate-layer)
2. [必须实现的 API / Required API](#2-必须实现的-api-required-api)
3. [生命周期与调用方 / Lifecycle and Callers](#3-生命周期与调用方-lifecycle-and-callers)
4. [平台交付物 / Platform Deliverables](#4-平台交付物-platform-deliverables)
5. [DTS / 驱动侧 / DTS and Driver Side](#5-dts-驱动侧-dts-and-driver-side)
6. [验收清单 / Acceptance Checklist](#6-验收清单-acceptance-checklist)

---

## 1. 为什么单独一层 / Why a Separate Layer

```text
应用 device_* 
  → vfs/usb 
  → bus/usb (usb_bus_*) 
       ├─→ hal_usb_*          （时钟/管脚/PHY 等基建，weak + 平台强符号）
       │                        (clocks/pins/PHY infrastructure; weak + platform strong symbols)
       └─→ usb_tusb_*         （本契约：TinyUSB 协议栈入口）
                               (this contract: TinyUSB stack entry)
              └─→ tusb_* / 板级 dcd
```
（调用链：应用 → vfs → bus → hal/port 两层。/ Call chain: app → vfs → bus → hal/port two layers.）

- `bus/usb` **只**通过本契约调 TinyUSB，避免把 `tusb.h` 泄漏进中间件公共头。
  `bus/usb` calls TinyUSB **only** through this contract, keeping `tusb.h` out of the middleware public headers.
- OSAL 与 TinyUSB 内部 OS 适配隔离：port 内自行对接 FreeRTOS / 裸机等。
  OSAL and TinyUSB's internal OS adaptation stay isolated: the port bridges FreeRTOS / bare metal etc. by itself.
- ESP32 等已有 USB 栈的平台：仍实现同名薄封装，或提供兼容桩。
  Platforms with their own USB stack (e.g. ESP32): still implement the same-named thin wrappers, or provide compatible stubs.

---

## 2. 必须实现的 API / Required API

头文件由中间件提供：`bus/usb/usb_tusb_port.h`（已在 `bus/usb` include 路径）。板级**只实现符号**；不要在平台树另放同名头，避免双份漂移。
The header is provided by the middleware: `bus/usb/usb_tusb_port.h` (already on the `bus/usb` include path). The board **implements symbols only**; do not place a same-named header in the platform tree to avoid duplication drift.

| 符号 / Symbol | 语义 / Semantics |
| :--- | :--- |
| `bool usb_tusb_init(uint8_t rhport)` | 初始化 TinyUSB（含 `tusb_init` / 描述符 / 板级 dcd）；失败返回 `false` / Initialize TinyUSB (incl. `tusb_init` / descriptors / board dcd); return `false` on failure |
| `void usb_tusb_task(void)` | 推进 TinyUSB 事件（等价周期性 `tud_task` / host task）/ Pump TinyUSB events (equivalent to periodic `tud_task` / host task) |
| `void usb_tusb_int_handler(uint8_t rhport)` | USB 外设 ISR 入口，转给 TinyUSB / dcd / USB peripheral ISR entry, forwarded to TinyUSB / dcd |
| `bool usb_tusb_cdc_connected(void)` | CDC ACM 是否枚举/连接 / Whether CDC ACM is enumerated/connected |
| `uint32_t usb_tusb_cdc_write(const void* buf, uint32_t len)` | CDC 写，返回实际字节 / CDC write, returns bytes actually written |
| `void usb_tusb_cdc_write_flush(void)` | CDC 写刷出 / Flush CDC writes |
| `uint32_t usb_tusb_cdc_available(void)` | CDC 可读字节 / CDC bytes available to read |
| `uint32_t usb_tusb_cdc_read(void* buf, uint32_t len)` | CDC 读 / CDC read |
| `bool usb_tusb_hid_ready(void)` | HID 是否可发 report / Whether HID can send a report |
| `bool usb_tusb_hid_report(uint8_t report_id, const void* report, uint16_t len)` | 发 HID report / Send an HID report |

未用到的 class 可做成「恒失败 / 空实现」，但**符号必须存在**（`bus/usb` 会链接）。
Unused classes may be "always-fail / empty implementations", but the **symbols must exist** (`bus/usb` links against them).

ECM 网络帧数据面 `usb_net_frame_push_tx/pop_rx` 同属契约头，板级实现（如 `usb_net_cb.c`）。
The ECM network-frame data plane `usb_net_frame_push_tx/pop_rx` is also part of the contract header, implemented at board level (e.g. `usb_net_cb.c`).

扩展时保持「port 薄、bus 厚」。
Keep "thin port, thick bus" when extending.

---

## 3. 生命周期与调用方 / Lifecycle and Callers

| 时机 / When | 谁调用 / Who Calls | 说明 / Notes |
| :--- | :--- | :--- |
| Host probe / init | `usb_bus_host_init` → `usb_tusb_init(rhport)` | `rhport` 来自 HAL/DTS 直投配置 / `rhport` comes from HAL/DTS direct-inject config |
| 主循环或 USB 任务 / Main loop or USB task | `usb_bus_task` → `usb_tusb_task` | 须周期调用，否则枚举/收发停滞 / Must be called periodically, otherwise enumeration/transfers stall |
| USB IRQ | 平台 ISR → `usb_tusb_int_handler`（或经 bus 注册的转发）/ platform ISR → `usb_tusb_int_handler` (or via bus-registered forwarding) | ISR 内禁止阻塞与日志刷屏 / No blocking or log spam inside the ISR |
| CDC/HID I/O | `usb_bus_*` 读写路径 / read-write paths | 见 `bus/usb/usb_bus.c` / See `bus/usb/usb_bus.c` |

建议 / Recommendations：

1. `usb_tusb_task` 放在优先级适中的任务或 `mini_tree_system_loop` 旁路，**不要**只在 ISR 里跑。
   Put `usb_tusb_task` in a moderately-prioritized task or beside `mini_tree_system_loop`; **do not** run it only in the ISR.
2. 与 `CONFIG_OSAL_*` 一致：有 RTOS 时用任务；裸机则在 loop 里调用。
   Match `CONFIG_OSAL_*`: use a task with an RTOS; call it in the loop on bare metal.
3. 描述符、端点、字符串留在平台 port，不进中间件。
   Keep descriptors, endpoints, and strings in the platform port, not in the middleware.

---

## 4. 平台交付物 / Platform Deliverables

| 交付 / Deliverable | 说明 / Details |
| :--- | :--- |
| `usb_tusb_port.c`（或 `.cpp`） | 实现上表 API（契约头在中间件 `bus/usb/`，**勿另放同名头**）/ Implements the API above (contract header in middleware `bus/usb/`; **do not add a same-named header**) |
| TinyUSB 板级文件 / TinyUSB board files | `dcd_*.c`、`usb_descriptors.c` 等（按 TinyUSB 惯例）/ `dcd_*.c`, `usb_descriptors.c`, etc. (per TinyUSB conventions) |
| CMake | 链 TinyUSB 源或 `lib/tinyusb` 子集 / Link TinyUSB sources or a `lib/tinyusb` subset |
| `hal_usb_*` 强符号 / strong symbols | 时钟、GPIO、PHY、IRQ 号等基建 / Clocks, GPIO, PHY, IRQ numbers, etc. |

中间件侧：`lib/tinyusb` 可选编入；**port 与描述符仍属平台**。
Middleware side: `lib/tinyusb` is optionally compiled in; **the port and descriptors still belong to the platform**.

---

## 5. DTS / 驱动侧 / DTS and Driver Side

| compatible（本仓 / this repo） | 角色 / Role |
| :--- | :--- |
| `usb-otg-host` | USB host / 控制器节点 / host/controller node |
| `heterogeneous,usb-cdc-acm` | CDC 客户端 / CDC client |
| `heterogeneous,usb-cdc-ecm` | ECM 客户端 / ECM client |
| `heterogeneous,usb-hid` | HID 客户端 / HID client |

VFS ioctl：`USB_CMD_SET_XFER_MODE` / `GET_XFER_MODE`（见 [peripherals.md](peripherals.md)）。
VFS ioctl: `USB_CMD_SET_XFER_MODE` / `GET_XFER_MODE` (see [peripherals.md](peripherals.md)).

属性：`reg`、时钟、IRQ、DMA 等经 DTSI **直投**进 `hal_usb_bus_config`。
Properties: `reg`, clocks, IRQ, DMA, etc. are **direct-injected** from the DTSI into `hal_usb_bus_config`.

---

## 6. 验收清单 / Acceptance Checklist

- [ ] 固件能链接：无 undefined reference to `usb_tusb_*` / Firmware links: no undefined reference to `usb_tusb_*`
- [ ] Host probe 后 `usb_tusb_init` 成功 / `usb_tusb_init` succeeds after host probe
- [ ] 周期 `usb_bus_task` / `usb_tusb_task` 后主机能枚举（或 device 模式被识别）/ Host enumerates after periodic `usb_bus_task` / `usb_tusb_task` (or device mode is recognized)
- [ ] CDC：`device_write`/`read` 或对应 ioctl 路径通 / CDC: `device_write`/`read` or the matching ioctl path works
- [ ] ISR 路径短；热路径无 `printf`（见 [fast_path.md](fast_path.md)）/ Short ISR path; no `printf` on hot paths (see [fast_path.md](fast_path.md))
- [ ] clangd：有真实头或继续用 `ide/stubs/usb_tusb_port.h` / clangd: use the real header or keep using `ide/stubs/usb_tusb_port.h`

---

## 相关文档 / Related Documents

- [porting_guide.md](porting_guide.md) · [peripherals.md](peripherals.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md)
- [faq.md](faq.md) · [todolist.md](todolist.md)
