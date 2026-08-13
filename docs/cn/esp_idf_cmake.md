# ESP-IDF 移植指南（CMake 集成）

> **已迁移到 `esp` 分支。** ESP-IDF 支持已从 `main` 剥离；完整移植指南、参考板工程（`components/mini_tree` + `board_port.cmake` + `hal_<soc>`）以及全部 ESP 专属修复现在都位于 **`esp` 分支**（`espidf-branch`）或乐鑫组件注册表中。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 把本 shelf 接到 ESP-IDF 工程的人 |
| **状态** | ESP 代码与详细步骤已移出 `main` → 见 `esp` 分支 / 注册表 |

---

## 如何获取 ESP 版本

二选一：

- **`esp` 分支**（完整板工程 + 移植步骤）：
  ```bash
  git clone -b espidf-branch https://github.com/H-000-H/mini_tree.git
  ```
- **乐鑫组件注册表**（作为 IDF 组件下拉）：在 `idf_component.yml` 里添加
  ```yaml
  dependencies:
    h-000-h/mini_tree: ">=1.2.0"
  ```
  或执行 `idf.py add-dependency "h-000-h/mini_tree"`。

## 移植步骤

完整 ESP-IDF 移植指南在 **`esp` 分支**上维护：

- 中文：[esp 分支 · `docs/cn/esp_idf_cmake.md`](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/cn/esp_idf_cmake.md)
- 英文：[esp 分支 · `docs/en/esp_idf_cmake.md`](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/en/esp_idf_cmake.md)
- 修复记录与 ESP 特殊性：[esp 分支 · `docs/cn/esp_idf_notes.md`](https://github.com/H-000-H/mini_tree/blob/espidf-branch/docs/cn/esp_idf_notes.md)

> 简言之：ESP 走 **IDF 组件**路径（由 `ESP_PLATFORM` 触发 → `cmake/esp_idf.cmake`），而非通用 `add_subdirectory`；板级 `hal_<soc>` 组件以 `WHOLE_ARCHIVE` 提供 `hal_*` strong 实现，`board_port.cmake` 注入 `BOARD_DTS` / `BOARD_DTSI_DIR` / 芯片 `-I/-D`。完整指南见上方 `esp` 分支文档。

---

## 相关文档

- [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md)（ESP-IDF VFS 心智对照）
