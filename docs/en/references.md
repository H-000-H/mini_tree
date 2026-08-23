# References & External Resources

> External standards, upstream repos and toolchain docs. For internal middleware docs, see [file_index.md](file_index.md).

| Item | Content |
| :--- | :--- |
| **Audience** | Deep dives / source tracing |
| **Related** | [file_index.md](file_index.md) · [ecosystem.md](ecosystem.md) |

---

## 1. Device Tree

| Resource | Description |
| :--- | :--- |
| Devicetree Specification | semantic source for `dtc-lite` |
| `dt-bindings/` conventions | middleware-generic macro naming |
| `board/dtsi/example-soc.dtsi` | generic example node templates |

---

## 2. RTOS & OSAL

| Resource | Description |
| :--- | :--- |
| FreeRTOS official docs | reference for `CONFIG_OSAL_FREERTOS` backend |
| RT-Thread official docs | reference for `CONFIG_OSAL_RTTHREAD` backend |
| OSAL three-backend design | `osal/src/osal_{null,freertos,rtthread}.c` |

---

## 3. Upstream Bricks (Fetch)

| Resource | Description |
| :--- | :--- |
| TinyUSB | USB device/host stack (`lib/tinyusb`, Fetch) |
| lwIP | networking stack (Fetch) |
| ETL | embedded template library (`lib/etl`, vendored) |
| FreeRTOS / RT-Thread | vendored (see [ecosystem.md](ecosystem.md)) |

---

## 4. Toolchain

| Resource | Description |
| :--- | :--- |
| ARMCLANG (AC6) | recommended compiler (ARMCC v5 unsupported) |
| clangd | editing / indexing (see [getting_started.md](getting_started.md) §7) |
| CMake | build system |
| Keil Studio | works (I've tried it, see [keil_integration.md](keil_integration.md)) |

---

## 5. Conventions & Specs

| Resource | Description |
| :--- | :--- |
| `.clang-format` / `.clang-tidy` | code style |
| `CONTRIBUTING.md` | contribution flow |
| `Kconfig` | config menu |

---

## Related Docs

- [file_index.md](file_index.md) · [ecosystem.md](ecosystem.md) · [getting_started.md](getting_started.md)
