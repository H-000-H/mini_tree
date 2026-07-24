# USB TinyUSB 板级契约（`usb_tusb_port`）

> 平台必须提供的 TinyUSB 粘合层：符号、生命周期、与 `bus/usb` 的调用关系。  
> 中间件**不**内嵌具体 MCU 的 TinyUSB port；IDE 占位见 `ide/stubs/usb_tusb_port.h`。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 做 USB 移植的板级工程师 |
| **前置** | [porting_guide.md](porting_guide.md) · [peripherals.md](peripherals.md) |
| **相关** | [driver_guide.md](driver_guide.md) · TinyUSB 在 `lib/tinyusb` · [ecosystem.md](ecosystem.md) |

---

## 目录

1. [为什么单独一层](#1-为什么单独一层)
2. [必须实现的 API](#2-必须实现的-api)
3. [生命周期与调用方](#3-生命周期与调用方)
4. [平台交付物](#4-平台交付物)
5. [DTS / 驱动侧](#5-dts--驱动侧)
6. [验收清单](#6-验收清单)

---

## 1. 为什么单独一层

```text
应用 device_* 
  → vfs/usb 
  → bus/usb (usb_bus_*) 
       ├─→ hal_usb_*          （时钟/管脚/PHY 等基建，weak + 平台强符号）
       └─→ usb_tusb_*         （本契约：TinyUSB 协议栈入口）
              └─→ tusb_* / 板级 dcd
```

- `bus/usb` **只**通过本契约调 TinyUSB，避免把 `tusb.h` 泄漏进中间件公共头。  
- OSAL 与 TinyUSB 内部 OS 适配隔离：port 内自行对接 FreeRTOS / 裸机等。  
- ESP32 等已有 USB 栈的平台：仍实现同名薄封装，或提供兼容桩。

---

## 2. 必须实现的 API

头文件名：`usb_tusb_port.h`（平台树常见路径：`board/tusb/` 或等价；需在 include 路径中）。

| 符号 | 语义 |
| :--- | :--- |
| `bool usb_tusb_init(uint8_t rhport)` | 初始化 TinyUSB（含 `tusb_init` / 描述符 / 板级 dcd）；失败返回 `false` |
| `void usb_tusb_task(void)` | 推进 TinyUSB 事件（等价周期性 `tud_task` / host task） |
| `void usb_tusb_int_handler(uint8_t rhport)` | USB 外设 ISR 入口，转给 TinyUSB / dcd |
| `bool usb_tusb_cdc_connected(void)` | CDC ACM 是否枚举/连接 |
| `uint32_t usb_tusb_cdc_write(const void* buf, uint32_t len)` | CDC 写，返回实际字节 |
| `void usb_tusb_cdc_write_flush(void)` | CDC 写刷出 |
| `uint32_t usb_tusb_cdc_available(void)` | CDC 可读字节 |
| `uint32_t usb_tusb_cdc_read(void* buf, uint32_t len)` | CDC 读 |
| `bool usb_tusb_hid_ready(void)` | HID 是否可发 report |
| `bool usb_tusb_hid_report(uint8_t report_id, const void* report, uint16_t len)` | 发 HID report |

未用到的 class 可做成「恒失败 / 空实现」，但**符号必须存在**（`bus/usb` 会链接）。  
ECM 等若走其它辅助符号，以当前 `bus/usb/usb_bus.c` 为准；扩展时保持「port 薄、bus 厚」。

---

## 3. 生命周期与调用方

| 时机 | 谁调用 | 说明 |
| :--- | :--- | :--- |
| Host probe / init | `usb_bus_host_init` → `usb_tusb_init(rhport)` | `rhport` 来自 HAL/DTS 直投配置 |
| 主循环或 USB 任务 | `usb_bus_task` → `usb_tusb_task` | 须周期调用，否则枚举/收发停滞 |
| USB IRQ | 平台 ISR → `usb_tusb_int_handler`（或经 bus 注册的转发） | ISR 内禁止阻塞与日志刷屏 |
| CDC/HID I/O | `usb_bus_*` 读写路径 | 见 `bus/usb/usb_bus.c` |

建议：

1. `usb_tusb_task` 放在优先级适中的任务或 `mini_tree_system_loop` 旁路，**不要**只在 ISR 里跑。  
2. 与 `CONFIG_OSAL_*` 一致：有 RTOS 时用任务；裸机则在 loop 里调用。  
3. 描述符、端点、字符串留在平台 port，不进中间件。

---

## 4. 平台交付物

| 交付 | 说明 |
| :--- | :--- |
| `usb_tusb_port.h` + `.c`（或 `.cpp`） | 实现上表 API |
| TinyUSB 板级文件 | `dcd_*.c`、`usb_descriptors.c` 等（按 TinyUSB 惯例） |
| CMake | 把头路径加入固件目标；链 TinyUSB 源或 `lib/tinyusb` 子集 |
| `hal_usb_*` 强符号 | 时钟、GPIO、PHY、IRQ 号等基建 |

中间件侧：`lib/tinyusb` 可选编入；**port 与描述符仍属平台**。

---

## 5. DTS / 驱动侧

| compatible（本仓） | 角色 |
| :--- | :--- |
| `usb-otg-host` | USB host / 控制器节点 |
| `heterogeneous,usb-cdc-acm` | CDC 客户端 |
| `heterogeneous,usb-cdc-ecm` | ECM 客户端 |
| `heterogeneous,usb-hid` | HID 客户端 |

VFS ioctl：`USB_CMD_SET_XFER_MODE` / `GET_XFER_MODE`（见 [peripherals.md](peripherals.md)）。  
属性：`reg`、时钟、IRQ、DMA 等经 DTSI **直投**进 `hal_usb_bus_config`。

---

## 6. 验收清单

- [ ] 固件能链接：无 undefined reference to `usb_tusb_*`  
- [ ] Host probe 后 `usb_tusb_init` 成功  
- [ ] 周期 `usb_bus_task` / `usb_tusb_task` 后主机能枚举（或 device 模式被识别）  
- [ ] CDC：`device_write`/`read` 或对应 ioctl 路径通  
- [ ] ISR 路径短；热路径无 `printf`（见 [fast_path.md](fast_path.md)）  
- [ ] clangd：有真实头或继续用 `ide/stubs/usb_tusb_port.h`  

---

## 相关文档

- [porting_guide.md](porting_guide.md) · [peripherals.md](peripherals.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md)  
- [faq.md](faq.md) · [todolist.md](todolist.md)
