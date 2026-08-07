# Linux Device Model vs mini_tree — Comparison

> Purpose: map mini_tree's Device Tree + Driver Model onto the Linux device model, focusing on mechanism comparison rather than kernel internals. No need to read Linux source in full.
> 用途：以 Linux 设备模型为主线，说明 mini_tree 的 Device Tree + Driver Model 与之的对应关系与关键差异。重点在"机制对照"而非"内核实现细节"。

---

## 1. One Chain: User to Hardware

Both describe hardware with the same abstraction stack, top-down: "user → file → device → bus → subsystem → resources → device tree → hardware".

```
Userspace
  ↓ syscall
VFS (virtual file)
  ↓
char device / cdev + file_operations
  ↓
device model / device ↔ device_driver (probe/remove)
  ↓
bus / platform / spi / i2c ...
  ↓
subsystem / SPI core / MTD / input ...
  ↓
resources / devm_ioremap / clk / gpio / irq
  ↓
Device Tree / device_node (static hardware description)
  ↓
hardware / MMIO registers · IRQs · DMA
```

The difference is not in the layers but in whether each layer is built at runtime (Linux) or generated statically at compile time (mini_tree).

---

## 2. Layer Comparison

| Layer | Linux | mini_tree | Difference |
| ------- | -------------------------------- | ------------------------------- | ---------------------------- |
| L0 Userspace | `open/read` on `/dev` | `device_open/read` | no `/dev` node in mini_tree |
| L1 VFS | `struct file` / `inode` | `dev_lifecycle` | no inode; lifecycle tracked by `dev_lifecycle` |
| L2 cdev | `struct cdev` + `dev_t` | `dev->ops` (no `dev_t`) | no major/minor; `device_id_t` handle instead |
| L3 core | `device` + `driver` + `bus_type` | `s_devices` + `DRIVER_REGISTER` | fully static, no runtime `device_add` |
| L4 bus | `spi_master` / `platform_device` | `bus_controller/client` | bus described statically in DTS |
| L5 subsystem | MTD / input / misc | `fft_spi_drv` / `ws2812_drv` | no unified subsystem layer; a driver is a service |
| L6 resources | `devm_*` | HAL + DTS props | resources not auto-released with device |
| L7 DT | `device_node` + DTB | `s_nodes` (dtc-lite) | compiled to C arrays; no runtime unflatten |
| L8 HW | SoC drivers | ESP-IDF HAL | hardware access via vendor HAL |

---

## 3. Key Concept Mapping

| Concept | Linux | mini_tree |
| ----- | ------------------------ | ------------------- |
| static DT | `struct device_node` | `s_nodes[]` |
| runtime device | `struct device` | `s_devices[]` |
| driver | `struct device_driver` | `DRIVER_REGISTER` macro |
| bus | `struct bus_type` | `bus_type` / `bus.c` |
| char device | `struct cdev` | `dev->ops` |
| file instance | `struct file` | `dev_lifecycle` |
| fops | `struct file_operations` | same name |
| private data | `dev_set_drvdata` | `device_set_priv` |
| parent device | `dev->parent` | `node->deps[0]` |
| child enumeration | `device_for_each_child` | global enumeration `device_get_first`/`device_get_next`/`device_get_count`; cascade parent-child enumeration `board_cascade_get` |
| device number | `dev_t` | `device_id_t` |
| sysfs | `kobject` | none |
| resources | `devm_*` (auto-released with device) | manual + HAL |
| lookup | `device_find_*` / `of_find_*` | `device_find` (returns `ERR_PTR`) |

---

## 4. How mini_tree Works

### 4.1 Compile-time static generation (vs Linux runtime registration)
- Linux: `device_add` / `driver_register` attach objects to a bus at runtime, supporting hotplug and dynamic matching.
- mini_tree: `dtc-lite` parses the DTS into `s_nodes[]` (nodes) and `s_devices[]` (devices) at compile time — fully static, ready at boot. No runtime creation, no hotplug.
- Trade-off: board hardware must be written as literal DTS; benefit: zero runtime allocation and a deterministic boot order.

### 4.2 Manual probe (vs Linux bus auto-matching)
- Linux: a bus walks `device` on `driver` registration and auto-`probe`s by `of_match_table` / `id_table`.
- mini_tree: no auto-matching. `board_driver_probe_all()` triggers probing; it first runs a **3-pass dependency resolution** over `deps`/`cascade`, then calls `probe` in the derived order.
- Order is decided by DTS dependency declarations, not by registration order, so drivers need not care about load sequence.

### 4.3 deps vs cascade (vs Linux parent / child)
- `deps`: child → parent. `device_get_parent()` follows `node->deps[0]` to the parent; a child is not probed until its parent is ready.
- `cascade`: parent → child. `board_cascade_get()` lets a parent enumerate its children (e.g. an SPI controller enumerates SPI slaves on its bus).
- They are opposite and complementary: `deps` guarantees parent-before-child during probe; `cascade` does child discovery at runtime.

### 4.4 Error handling (vs Linux `ERR_PTR`)
- mini_tree also uses `ERR_PTR` / `IS_ERR` / `PTR_ERR`: `device_find` returns a pointer encoding a negative error code on failure. Callers **must** check `IS_ERR` before dereferencing (see `service_spec.md`).
- Same as Linux, but mini_tree has no `IS_ERR_OR_NULL`; distinguish "not found" (error pointer) from "null" by your own convention.

### 4.5 Resource management (vs Linux `devm_*`)
- Linux: `devm_ioremap` / `devm_clk_get` bind resources to a `device`, auto-reclaimed when the device is released — no leaks.
- mini_tree: resources (register map, clk, GPIO, IRQ) are obtained explicitly via HAL + DTS properties and are NOT auto-released with the device; a driver must free them manually in `remove`. This is deliberate: a static device lives for the whole firmware lifetime, so there is no "device disappears" concept.

### 4.6 No sysfs / no userspace filesystem
- Linux exposes the device tree to userspace via `sysfs` (`kobject`): `echo` tuning, `/dev` nodes, `uevent`.
- mini_tree exposes no userspace filesystem; debugging relies on `compile_commands.json` (clangd jump) + logging. A cdev's `dev->ops` is called only in firmware (kernel) state — there is no `/dev/<name>` node.

---

## 5. Minimal Example

An SPI slave device shows the two sides are isomorphic but differently implemented:

| Step | Linux | mini_tree |
| --- | --- | --- |
| HW desc | `spi_board_info` or DTS `spi{}` | `spi` node in `board/dts/*.dts` |
| controller | `spi_master` | `bus_controller` (SPI) |
| client | `spi_device` + `spi_driver.probe` | `bus_client` + `DRIVER_REGISTER` `probe` |
| transfer | `spi_transfer` / `spi_sync` | transfer callback in `dev->ops` |
| expose | `/dev/spidev*` via `spidev` | no `/dev`; accessed via `device_open` handle |

---

## 6. Reading Order (this repo)

| Order | File | Linux counterpart |
| --- | -------------------------------------- | --------------------- |
| 1 | `board/dts/*.dts` | DTS |
| 2 | `build/generated/.../board_devtable.c` | unflattened DT |
| 3 | `build/generated/.../board_probe.c` | probe order + cascade |
| 4 | `board/src/board_driver.c` | `device_attach` equivalent |
| 5 | `vfs/spi/spi_bus.c` | `spi_master` |
| 6 | `vfs/spi/spi_client.c` | `spi_device` + fops |

---

## 7. References (for deep Linux study)

- [Driver model overview](https://www.kernel.org/doc/html/latest/driver-api/driver-model/index.html)
- [Platform devices](https://www.kernel.org/doc/html/latest/driver-api/driver-model/platform.html)
- [SPI subsystem](https://www.kernel.org/doc/html/latest/driver-api/spi.html)
- [Device Tree usage](https://www.kernel.org/doc/html/latest/devicetree/usage-model.html)
