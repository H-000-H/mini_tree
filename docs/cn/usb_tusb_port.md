# USB (TinyUSB) 端口指南

> 把 TinyUSB（`lib/tinyusb`，Fetch 积木）接进中间件的板级端口步骤。涉及：`vfs/usb`、设备树绑定、`dtsi` 节点。核心约定见 [peripherals.md](peripherals.md)、[device_tree_porting.md](device_tree_porting.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 接 USB / 板级端口 |
| **相关** | [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) |

---

## 1. 前置条件

- TinyUSB 经 FetchContent 拉取（`mini_tree_link_tinyusb`，见 [ecosystem.md](ecosystem.md)）；不在 `lib/` vendor 内。
- 板级 `dtsi/` 提供 USB 控制器节点（参考 `board/dtsi/example-soc.dtsi`）。
- `CONFIG_ESP_*` 后端已选（USB 中断在 ISR 出口经 统一接口 的 `mini_yield_from_isr()` 请求上下文切换）。
- `CONFIG_USB_TUSB_MCU` 必须显式填目标芯片的 `OPT_MCU_*`（STM32F4 = `304` / STM32H7 = `306` / ESP32-S3 = `901` / RP2040 = `1100`）；默认 `0` 即"未配置"，`board/include/tusb_config.h` 会 `#error` 中止编译。
- `CONFIG_USB_TUSB_DCD_SRC` 填设备控制器驱动源（相对 `lib/tinyusb/src`，如 `portable/synopsys/dwc2/dcd_dwc2.c`）—— TinyUSB 核心源**不含** DCD/HCD（官方 `src/CMakeLists.txt` 顶部注释），留空则只编协议栈核心、链接 ELF 时缺 `dcd_init`。
- STM32 的 DWC2 驱动经 `dwc2_stm32.h` 包含芯片头（如 `stm32f4xx.h`），由板级 BSP / CMSIS 提供；本仓不含芯片头文件。

---

## 2. 板级端口步骤

1. 板级 `dtsi/` 加 USB 控制器节点（`compatible = "mt-usb-otg-host"`，含中断号 / 端点数）。
2. 写 `drivers/<chip>/` 产品驱动（`DRIVER_REGISTER` + dtc-lite 探针），实现 `hal/usb` 回调。
3. `vfs/usb/vfs-usb.{c,h}` 经 `vfs/usb` 暴露设备/主机接口。
4. 平台 CMake 注入 `BOARD_DTSI_DIR` 指向板级 dtsi。
5. 跑 `dtc-lite` 验证探针命中。

---

## 3. 设备树绑定

| 字段 | 说明 |
| :--- | :--- |
| `compatible` | `"mt-usb-otg-host"` |
| `interrupts` | USB 中断号（经 VIRQ 封装） |
| `num-endpoints` | 端点数量 |
| `maximum-speed` | `high` / `full` / `low` |

> 绑定宏放 `dt-bindings/usb.h`（中间件通用）。

---

## 4. 中断与 统一接口

USB 中断经 `interrupt/interrupt.{c,h}` 的 VIRQ 封装后，在 ISR 出口调用 统一接口 的 `mini_yield_from_isr()` 请求上下文切换；裸机（`CONFIG_OS_BARE`）下由 `time_slice` 调度处理。详见 [backend_switching.md](backend_switching.md)。

---

## 5. 验证

1. `dtc-lite` 探针命中（`drivers/<chip>/` USB 驱动注册）。
2. 编 `mini_tree` 含 `vfs/usb`，无未定义符号。
3. 接平台链接脚本，确认 USB 描述符段放置。
4. 实测枚举（设备模式）或连接（主机模式）。

---

## 相关文档

- [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) · [backend_switching.md](backend_switching.md)
