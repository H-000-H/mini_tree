# ESP-IDF Porting Guide (CMake Integration)

> **Moved to the `esp` branch.** ESP-IDF support has been split out of `main`; the full porting guide, the reference board project (`components/mini_tree` + `board_port.cmake` + `hal_<soc>`), and all ESP-specific fixes now live in the **`esp` branch** (`espidf-branch`) or in the Espressif Component Registry.

| Item | Description |
| :--- | :--- |
| **Audience** | Anyone wiring this shelf into an ESP-IDF project |
| **Status** | ESP code & detailed steps moved out of `main` → see `esp` branch / registry |

---

## How to Get the ESP Version

Choose one:

- **`esp` branch** (full board project + porting steps):
  ```bash
  git clone -b espidf-branch https://github.com/H-000-H/mini_tree.git
  ```
- **Espressif Component Registry** (pull as an IDF component): add to `idf_component.yml`
  ```yaml
  dependencies:
    h-000-h/mini_tree: ">=1.2.0"
  ```
  or run `idf.py add-dependency "h-000-h/mini_tree"`.

## Porting Steps

The full ESP-IDF porting guide is maintained on the **`esp` branch**:

- English: [esp branch · `docs/en/esp_idf_cmake.md`](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/en/esp_idf_cmake.md)
- Chinese: [esp branch · `docs/cn/esp_idf_cmake.md`](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/cn/esp_idf_cmake.md)
- Fix log & ESP specifics: [esp branch · `docs/en/esp_idf_notes.md`](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/en/esp_idf_notes.md)

> In short: ESP uses the **IDF component** path (triggered by `ESP_PLATFORM` → `cmake/esp_idf.cmake`), not the generic `add_subdirectory`; board-level `hal_<soc>` component implements `hal_*` as strong symbols with `WHOLE_ARCHIVE`, and `board_port.cmake` injects `BOARD_DTS` / `BOARD_DTSI_DIR` / chip `-I/-D`. See the `esp` branch docs above for the complete guide.

---

## Related Docs

- [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) (ESP-IDF VFS mental mapping)
