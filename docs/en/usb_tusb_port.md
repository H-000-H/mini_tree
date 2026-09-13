# USB (TinyUSB) Porting Guide

> Steps to bring TinyUSB (`lib/tinyusb`, a Fetch brick) into the middleware's board port. Involves: `vfs/usb`, device-tree bindings, `dtsi` nodes. Core conventions: [peripherals.md](peripherals.md) and [device_tree_porting.md](device_tree_porting.md).

| Item | Content |
| :--- | :--- |
| **Audience** | USB / board porting |
| **Related** | [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) |

---

## 1. Prerequisites

- TinyUSB is pulled via FetchContent (`mini_tree_link_tinyusb`, see [ecosystem.md](ecosystem.md)); not vendored under `lib/`.
- The board `dtsi/` provides the USB controller node (see `board/dtsi/example-soc.dtsi`).
- An `CONFIG_ESP_*` backend is selected (the USB ISR exit requests a context switch via the unified interface's `mini_yield_from_isr()`).
- `CONFIG_USB_TUSB_MCU` must be set explicitly to the target's `OPT_MCU_*` (STM32F4 = `304`, STM32H7 = `306`, ESP32-S3 = `901`, RP2040 = `1100`); the default `0` means "unconfigured" and `board/include/tusb_config.h` aborts the build with `#error`.
- `CONFIG_USB_TUSB_DCD_SRC` selects the device-controller driver source (relative to `lib/tinyusb/src`, e.g. `portable/synopsys/dwc2/dcd_dwc2.c`) — TinyUSB core sources **exclude** DCD/HCD (see the note at the top of its `src/CMakeLists.txt`), so leaving it empty compiles the stack core only and the ELF link misses `dcd_init`.
- The STM32 DWC2 driver pulls the chip header (`stm32f4xx.h`) through `dwc2_stm32.h`; it comes from the board BSP / CMSIS, not from this repo.

---

## 2. Board Port Steps

1. Add the USB controller node to the board `dtsi/` (`compatible = "mini-tree,usb"`, with IRQ number / endpoint count).
2. Write the `drivers/<chip>/` product driver (`DRIVER_REGISTER` + dtc-lite probe) implementing the `hal/usb` callbacks.
3. `vfs/usb/vfs-usb.{c,h}` exposes device/host interfaces via `vfs/usb`.
4. Platform CMake injects `BOARD_DTSI_DIR` pointing at the board dtsi.
5. Run `dtc-lite` to verify the probe hits.

---

## 3. Device-Tree Binding

| Field | Description |
| :--- | :--- |
| `compatible` | `"mini-tree,usb"` |
| `interrupts` | USB IRQ number (via VIRQ wrapper) |
| `num-endpoints` | endpoint count |
| `maximum-speed` | `high` / `full` / `low` |

> Binding macros live in `dt-bindings/usb.h` (middleware-generic).

---

## 4. Interrupts & the unified interface

USB interrupts go through the VIRQ wrapper in `interrupt/interrupt.{c,h}` and, at ISR exit, request a context switch via the unified interface's `mini_yield_from_isr()`; under bare metal (`CONFIG_OS_BARE`) they are dispatched by the `time_slice` scheduler. See [backend_switching.md](backend_switching.md).

---

## 5. Validation

1. dtc-lite probe hits (the `drivers/<chip>/` USB driver registers).
2. Build `mini_tree` with `vfs/usb`, no undefined symbols.
3. Wire the platform linker script; confirm the USB descriptor section is placed.
4. Test enumeration (device mode) or attach (host mode) in practice.

---

## Related Docs

- [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) · [backend_switching.md](backend_switching.md)
