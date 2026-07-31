# ESP-IDF 集成说明与近期修复

本文记录从 ESP32-S3 板级验证回灌到中间件的**纯修复**，以及 ESP 路径的特殊性与依赖策略建议。

产品驱动默认在 **`mini_tree/drivers/<chip>/`**（编进 `mini_tree` 组件）。  
**唯一**树外例外：`components/driver_ws2812`（厂商 RMT / `led_strip`）。详见 [esp_idf_cmake.md](../docs/esp_idf_cmake.md)。

入口：`CMakeLists.txt` 在 `ESP_PLATFORM` 时 `include(cmake/esp_idf.cmake)` 后 `return()`。

---

## 1. 已同步的修复（跨板通用）

| 项 | 位置 | 问题 | 修复 |
|----|------|------|------|
| 驱动强引用 | `tools/dtc_lite/generator.py` | `board_probe.c` 对 `DRIVER_REGISTER` 使用 `weak`，静态库中的驱动 `.o` 不会被拉入，运行时 `no generated probe` | 改为强 `extern`；DTS 匹配到的驱动缺失则**链接失败**（`undefined reference`） |
| ERR_PTR 判定 | `core/include/status.h` | `!dev` 挡不住 `ERR_PTR` | 增加 `IS_ERR_OR_NULL` |
| device getter | `board/src/board_device.c` | 对 ERR 指针解引用 / 误用 | `device_get_{name,compatible,status,criticality}` 使用 `IS_ERR_OR_NULL` |
| probe 循环 | `board/src/board_driver.c` | 依赖/级联误判；部分 ABI 下循环状态被打坏；无名根无驱动刷屏 | `IS_ERR_OR_NULL`、`board_probe_order_at`、循环状态 `volatile`、无名根静默禁用 |
| 任务优先级 | `core/src/event_bus.c` + `osal/src/osal_freertos.c` | `K_DISPATCH_PRIO` 过大触发 FreeRTOS assert | 默认 prio=24；`osal_clamp_task_priority` 钳到 `[0, configMAX_PRIORITIES)` |
| 对象池查询 | `osal/src/osal_freertos.c` | bus 层调用 `osal_pool_is_used`，FreeRTOS 后端缺失 | 补齐实现（与 `osal_null` 语义一致） |
| spinlock 告警 | `system_cpp/src/system_cmd.cpp` | `-Werror=unused-result` | `COMPAT_IGNORE_RESULT` 包裹 `osal_spinlock_*` |

验证建议（非 ESP 板）：默认 CMake Preset 全量编译；改过 generator 后确认生成的 `board_probe.c` 中 extern **无** `weak`。

---

## 2. ESP-IDF 特殊性（仅 ESP 路径）

### 2.1 Include 顺序（stubs vs 生成头）

`ide/stubs/board_nodes.h` 里 `DEV_ID_COUNT=1` 仅供 IDE；若排在生成目录之前，会盖住 dtc-lite 真表，表现为：

- `probe complete: 0 ok, 0 fail`
- `device_find_by_label` 找不到真实设备

`esp_idf.cmake` 中 **必须**保持：

```text
GENERATED_BOARD_DIR  →  SCRUBBER_GEN_DIR  →  ide/stubs  →  KCONFIG_GEN_DIR(空壳)
```

空壳 `config.h` 必须在 stubs **之后**：stubs 的 `config.h` 提供 `CONFIG_SYS_LOG_USE_PRINTF` 等；空壳若抢先则触发 `SYS_LOG backend not configured`。

非 ESP 的 genconfig 真 `config.h` 路径不受此约束。

### 2.2 强引用与静态库 / 跨组件驱动

- **同组件**（`drivers/*/src/*.c` 在 `libmini_tree.a` 内）：`board_probe` 强引用即可从同一 `.a` 抽出 `.o`。
- **树外组件**（当前仅 `driver_ws2812`）：对该组件使用 IDF `WHOLE_ARCHIVE`，保证 `board_driver_probe_*` 进最终 ELF。

板级扩展 dtc-lite（仅树外驱动需要；产品驱动已由 GLOB 扫入）：

```cmake
set(MINI_TREE_DTC_EXTRA_SCAN_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../driver_ws2812/src")
set(MINI_TREE_DTC_EXTRA_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../driver_ws2812/src/ws2812_drv.c")
```

### 2.3 OSAL / 优先级

ESP-IDF 默认 `configMAX_PRIORITIES` 常为 25（合法 0..24）。中间件默认 dispatch prio=24，并在 OSAL 层钳位，避免 assert。

### 2.4 Xtensa windowed ABI

`board_driver_probe_all` 中 `volatile` 循环状态最初为修复 ESP32（Xtensa）上 `call8` 打坏调用方寄存器导致的只 probe 首设备问题。ARM 上作为防御性对齐保留，避免跨板行为分叉。

### 2.5 `CONFIG_*` 双源

IDF 已注入 `sdkconfig.h`。`esp_idf.cmake` 只生成空壳 `config.h`，避免与完整 genconfig 头 `-Werror=redefined`。

---

## 3. 推荐：ESP 板删除 vendored `lib/`，改走 IDF / Fetch

当前板级 `components/mini_tree/lib/` 常 vendored：`freeRTOS`、`rtthread`、`tinyusb`、`lwip`、`cJSON`、`etl` 等。在 **ESP-IDF** 上这与内核/组件重复，且体积、版本、许可证维护成本高。

**推荐（ESP 板工程）：**

1. **删除**（或不再编入）`mini_tree/lib` 下与 IDF 重叠的树：至少 `freeRTOS`、`lwip`；RT-Thread 在 ESP 路径本就不使用。
2. **OSAL**：继续用 `osal_freertos.c` 对接 IDF 内置 FreeRTOS（`esp_idf.cmake` 已强制 `CONFIG_OSAL_FREERTOS`）。
3. **第三方有用库**（ETL、cJSON、TinyUSB 等）：用板级 / 中间件 `cmake/dep_fetch.cmake`（FetchContent）或 IDF Component Manager（`idf_component.yml`）拉取，**不要**长期把整棵 upstream 拷进 `lib/`。
4. **TinyUSB**：优先 Espressif 组件或官方 registry；DCD/`CFG_TUSB_MCU` 仍由板级配置，中间件不绑 MCU。

中间件仓库可继续保留 `lib/` 作为**非 ESP**（Cube / 裸机）的可选 vendored / fetch 落点；ESP 板应视 `lib/` 为可裁剪，默认不依赖其中的 FreeRTOS/lwIP 副本。

---

## 4. 板级对照清单（ESP）

1. `components/mini_tree` 使用本仓库（或 submodule）并走 `esp_idf.cmake`。
2. Include 顺序未被板级 `EXTRA_INCLUDE` 打乱。
3. 树外 `DRIVER_REGISTER`：`MINI_TREE_DTC_EXTRA_SCAN_DIRS` + 组件 `WHOLE_ARCHIVE`（或同库编译）。
4. 烧录后应看到真实 `probing '...'`，而非 `0 ok, 0 fail`。
5. 逐步去掉 vendored `lib/freeRTOS`、`lib/lwip` 等，确认仍链到 IDF 提供的实现。

---

## 5. 刻意不同步到中间件的内容

| 内容 | 原因 |
|------|------|
| WS2812 驱动 / dtsi / dtc 扫描路径 | 板级产品驱动，不属于中间件 |
| `compiler_compat` 中 `WS2812` 魔法槽 | 同上；板级需要时再加 |
| 板级 `.clangd` / 具体 GPIO 映射 | 板工程私有 |
