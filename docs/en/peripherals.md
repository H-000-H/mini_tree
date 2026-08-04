# Peripherals & Device Matrix

> Inventory of hardware abstraction layers (HAL / VFS / bus) provided by the middleware, and how they relate to the `board/` device model and dtc-lite compile-time probes. Adding a peripheral requires a matching HAL + device-tree binding — see [driver_guide.md](driver_guide.md).

| Item | Content |
| :--- | :--- |
| **Audience** | Selecting peripherals / writing drivers |
| **Related** | [architecture.md](architecture.md) (§2.1 coverage) · [driver_guide.md](driver_guide.md) |

---

## 1. Device-Tree Bindings & Compile-Time Probes

All peripherals are matched at compile time via dtc-lite using device-tree nodes; the framework's `board_driver_probe_all` registers a driver only when a matching `compatible` exists in the device tree. No node = no driver compiled. See [driver_guide.md](driver_guide.md) §2.

| Category | Binding path `dt-bindings/` | Description |
| :--- | :--- | :--- |
| GPIO | `dt-bindings/gpio.h` | generic macros for pin / polarity |
| SPI | `dt-bindings/spi.h` | generic macros for frame format / clock polarity |
| UART | `dt-bindings/uart.h` | generic macros for baud / flow control |
| TIM | `dt-bindings/tim.h` | generic macros for timer prescaler / channel |

---

## 2. HAL Matrix

| Peripheral | HAL path | Backend / Deps |
| :--- | :--- | :--- |
| GPIO | `hal/gpio/hal_gpio.{h,c}` | `hal/amp` (on multicore) |
| UART | `hal/uart/hal_uart.{h,c}` | depends on `bus/uart` |
| SPI | `hal/spi/hal_spi.{h,c}` | depends on `bus/spi` |
| I2C | `hal/i2c/hal_i2c.{h,c}` | depends on `bus/i2c` |
| TIM | `hal/tim/hal_tim.{h,c}` | generic timer |
| ADC | `hal/adc/hal_adc.{h,c}` | exposed via `vfs/adc` |
| WDT | `hal/wdt/hal_wdt.{h,c}` | WDT group of `system` |
| Storage | `hal/storage/hal_storage.{h,c}` | cooperates with `hal/amp` / `hal/flash` |
| AMP | `hal/amp/hal_amp.{h,c}` | multicore messaging / boot |

> All HALs ship weak empty implementations (`hal/hal_if_dummy.c`) so it builds without a board.

---

## 3. VFS Matrix

| Peripheral | VFS path | Deps |
| :--- | :--- | :--- |
| UART device | `vfs/uart/vfs-uart.{c,h}` | `hal/uart` + `bus/uart` |
| ADC device | `vfs/adc/vfs-adc.c` | `hal/adc` |
| SPI slave | `vfs/spi/vfs-spi.{c,h}` | `hal/spi` + `bus/spi` |
| I2C slave | `vfs/i2c/vfs-i2c.{c,h}` | `hal/i2c` + `bus/i2c` |
| USB device/host | `vfs/usb/vfs-usb.{c,h}` | `lib/tinyusb` (Fetch brick) |

> VFS-layer drivers (`vfs/`) need an explicit re-bind after the pool resets — see [driver_guide.md](driver_guide.md) §7.

---

## 4. Bus Matrix

| Bus | Bus path | Controller abstraction |
| :--- | :--- | :--- |
| UART | `bus/uart/uart_bus.{c,h}` | controller table + port registration |
| SPI | `bus/spi/spi_bus.{c,h}` | controller table + chip select |
| I2C | `bus/i2c/i2c_bus.{c,h}` | controller table + address |

---

## 5. Adding a New Peripheral

1. Add `dt-bindings/<name>.h` parameter macros (middleware-generic).
2. Write `hal/<name>/hal_<name>.{h,c}` (incl. weak empty implementation).
3. Write `vfs/<name>/vfs-<name>.{c,h}` (if exposed via VFS).
4. Write the `drivers/<chip>/` product driver (`DRIVER_REGISTER` + dtc-lite probe).
5. Add the node in the board `dtsi/` (or set `BOARD_DTSI_DIR` for the platform).
6. Run `dtc-lite` to verify the compile-time probe hits.

See [driver_guide.md](driver_guide.md) for details.

---

## Related Docs

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [file_index.md](file_index.md)
