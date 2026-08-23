# Documentation Index

> Entry point for the middleware topic documents. The repository root keeps only `README` / `CHANGELOG` / `CONTRIBUTING` and `LICENSE` / `NOTICE`.

| Item | Content |
| :--- | :--- |
| **Audience** | Everyone |
| **Related** | [README.md](../README.md) · [SUMMARY.md](SUMMARY.md) (top-level bilingual summary) · [usage.md](usage.md) |

---

## How to Choose a Document

| Role | Suggested Order |
| :--- | :--- |
| First-time integration | [README.md](README.md) (full index) → [getting_started.md](getting_started.md) → [usage.md](usage.md) → [faq.md](faq.md) |
| Picking open-source bricks / extending the ecosystem | [ecosystem.md](ecosystem.md) |
| Platform porting | [device_tree_porting.md](device_tree_porting.md) → [esp_idf_cmake.md](esp_idf_cmake.md) (ESP: **on the `esp` branch**) → [usb_tusb_port.md](usb_tusb_port.md) → [amp.md](amp.md) → [driver_guide.md](driver_guide.md) |
| Writing applications | [service_spec.md](service_spec.md) → [peripherals.md](peripherals.md) → [runtime_services.md](runtime_services.md) → [fast_path.md](fast_path.md) |
| Writing code / checking naming | [coding_style.md](coding_style.md) (language rules: enforced below `app/`, recommended in `app/`) |
| Design rationale / mechanisms | [design_decisions.md](design_decisions.md) · [patterns.md](patterns.md) · [references.md](references.md) · [architecture.md](architecture.md) |
| File lookup | [file_index.md](file_index.md) |
| Compliance / license | [../NOTICE](../NOTICE) (third-party list and compliance notes) · [../LICENSE](../LICENSE) (Apache-2.0 full text) · [ecosystem.md](ecosystem.md) §6 (acknowledgements) |

---

## All Topics

### Getting Started

| Document | Description |
| :--- | :--- |
| [README.md](README.md) | Full documentation index (by topic + priority) |
| [usage.md](usage.md) | Terminology + reading paths |
| [getting_started.md](getting_started.md) | Dependencies, Kconfig, CMake, ignition |
| [faq.md](faq.md) | Frequently asked questions |

### Architecture & Porting

| Document | Description |
| :--- | :--- |
| [architecture.md](architecture.md) | Layers and data flow |
| [patterns.md](patterns.md) | Key mechanisms anatomy: pre_execution chain / two-phase boot / compile-time probe table / xtask scheduling / VIRQ top-bottom halves / SPSC lock-free channel / dev_lifecycle / non-blocking state machines |
| [ecosystem.md](ecosystem.md) | Brick-style linking: integrated open-source libraries and how to extend |
| [design_decisions.md](design_decisions.md) | Design decisions still in force and author preferences |
| [references.md](references.md) | External references: ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt |
| [device_tree_porting.md](device_tree_porting.md) | Platform porting checklist |
| [esp_idf_cmake.md](esp_idf_cmake.md) | ESP-IDF component-style CMake — **moved to the `esp` branch** |
| [driver_guide.md](driver_guide.md) | DTS / `DRIVER_REGISTER` |
| [peripherals.md](peripherals.md) | Peripheral compatible / ioctl overview |
| [usb_tusb_port.md](usb_tusb_port.md) | TinyUSB board-level contract |
| [amp.md](amp.md) | Dual-core AMP |
| [osal_switching.md](osal_switching.md) | OSAL backend switching |
| [net.md](net.md) | Network protocol stack glue (MQTT / TCP / PPP / USB NIC) |

### Coding & Runtime

| Document | Description |
| :--- | :--- |
| [coding_style.md](coding_style.md) | Language rules: naming & formatting (enforced below `app/`, recommended in `app/`) |
| [app_cpp_guide.md](app_cpp_guide.md) | Upper-layer C++ restrictions and recommendations (ETL containers / tiering / forbidden) |
| [memory_footprint.md](memory_footprint.md) | Memory footprint: fixed static RAM overhead & trimming knobs |
| [service_spec.md](service_spec.md) | Application-layer do's and don'ts |
| [runtime_services.md](runtime_services.md) | EventBus / VIRQ / SYSTEM_C·CPP / buffers |
| [can_hook.md](can_hook.md) | CAN protocol superset hooks |
| [fast_path.md](fast_path.md) | ISR / hot-path red lines |
| [api_compatibility.md](api_compatibility.md) | API stability surface |

### Debugging & History

| Document | Description |
| :--- | :--- |
| [debug_monitor.md](debug_monitor.md) | Logging, generated artifacts, clangd |
| [keil_integration.md](keil_integration.md) | Keil Studio (supported) / µVision (not recommended) · IDE |
| [problem_summary.md](problem_summary.md) | Historical problem timeline |

### Planning & Index

| Document | Description |
| :--- | :--- |
| [file_index.md](file_index.md) | Source navigation |
| [roadmap.md](roadmap.md) | Roadmap |
| [todolist.md](todolist.md) | TODO list |

Toolchain notes live in [tools_guide.md](tools_guide.md). Contribution rules live in [../CONTRIBUTING.md](../CONTRIBUTING.md).

---

## Documentation Conventions

Each topic document should try to include:

1. **Title + one-sentence summary**
2. **Audience / prerequisites**
3. **Table of contents (for long documents)**
4. **Body (tables and copy-paste commands first)**
5. **Related documents (links at the end)**

Paths and symbols are always wrapped in back-ticks; error codes are written with full `VFS_ERR_*` names. New documents go into `docs/cn/` and `docs/en/`; the root keeps only `README` / `CHANGELOG` / `CONTRIBUTING` and the legal files.

---

## Related Documents

- [README.md](../README.md) · [CHANGELOG.md](../CHANGELOG.md) · [ecosystem.md](ecosystem.md)
