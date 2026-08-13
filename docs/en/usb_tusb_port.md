# USB (TinyUSB) Porting Guide

> Steps to bring TinyUSB (`lib/tinyusb`, a Fetch brick) into the middleware's board port. Involves: `vfs/usb`, device-tree bindings, `dtsi` nodes. Core conventions: [peripherals.md](peripherals.md) and [device_tree_porting.md](device_tree_porting.md).

| Item | Content |
| :--- | :--- |
| **Audience** | USB / board porting |
| **Related** | [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) |

---

## 1. Prerequisites

- TinyUSB comes via the **ESP-IDF component** (`esp_tinyusb` / registry, declared in `idf_component.yml`); not vendored under `lib/`.
- The board `dtsi/` provides the USB controller node (see `board/dtsi/example-soc.dtsi`).
- An `CONFIG_OSAL_*` backend is selected (USB interrupts need the OSAL interrupt wrapper).

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

## 4. Interrupts & OSAL

USB interrupts go through the VIRQ wrapper in `interrupt/interrupt.{c,h}` and then to the OSAL interrupt; under bare metal (`CONFIG_OSAL_NULL`) they are dispatched by the `time_slice` scheduler. See [osal_switching.md](osal_switching.md).

---

## 5. Validation

1. dtc-lite probe hits (the `drivers/<chip>/` USB driver registers).
2. Build `mini_tree` with `vfs/usb`, no undefined symbols.
3. Wire the platform linker script; confirm the USB descriptor section is placed.
4. Test enumeration (device mode) or attach (host mode) in practice.

---

## Related Docs

- [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) · [osal_switching.md](osal_switching.md)
