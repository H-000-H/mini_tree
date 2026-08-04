# ESP-IDF 集成说明与近期修复 / ESP-IDF Integration Notes and Recent Fixes

本文记录从 ESP32-S3 板级验证回灌到中间件的**纯修复**，以及 ESP 路径的特殊性与依赖策略建议。

This document records the **pure fixes** back-ported from ESP32-S3 board-level validation into the middleware, plus the ESP-path specifics and dependency-strategy recommendations.

产品驱动默认在 **`mini_tree/drivers/<chip>/`**（共 37 个，GLOB 编进 `mini_tree` 组件）。
Product drivers live by default in **`mini_tree/drivers/<chip>/`** (37 total, GLOB-compiled into the `mini_tree` component).

**唯一**树外例外：`components/driver_ws2812`（厂商 RMT / `led_strip`）。详见 [esp_idf_cmake.md](../docs/esp_idf_cmake.md)。
**The only** out-of-tree exception: `components/driver_ws2812` (vendor RMT / `led_strip`). See [esp_idf_cmake.md](../docs/esp_idf_cmake.md).

入口：`CMakeLists.txt` 在 `ESP_PLATFORM` 时 `include(cmake/esp_idf.cmake)` 后 `return()`。
Entry: when `ESP_PLATFORM` is set, `CMakeLists.txt` does `include(cmake/esp_idf.cmake)` and then `return()`.

---

## 1. 已同步的修复（跨板通用）/ Synced Fixes (Board-Agnostic)

| 项 / Item | 位置 / Location | 问题 / Problem | 修复 / Fix |
|----|------|------|------|
| 驱动强引用 / Driver strong refs | `tools/dtc_lite/generator.py` | `board_probe.c` 对 `DRIVER_REGISTER` 使用 `weak`，静态库中的驱动 `.o` 不会被拉入，运行时 `no generated probe`<br>`board_probe.c` used `weak` on `DRIVER_REGISTER`; driver `.o` files in the static lib weren't pulled in → runtime `no generated probe` | 改为强 `extern`；DTS 匹配到的驱动缺失则**链接失败**（`undefined reference`）<br>Switch to strong `extern`; a missing matched driver now **fails the link** (`undefined reference`) |
| ERR_PTR 判定 / ERR_PTR check | `core/include/status.h` | `!dev` 挡不住 `ERR_PTR`<br>`!dev` doesn't catch `ERR_PTR` | 增加 `IS_ERR_OR_NULL`<br>Added `IS_ERR_OR_NULL` |
| device getter | `board/src/board_device.c` | 对 ERR 指针解引用 / 误用<br>Dereference / misuse of ERR pointers | `device_get_{name,compatible,status,criticality}` 使用 `IS_ERR_OR_NULL`<br>`device_get_{name,compatible,status,criticality}` now use `IS_ERR_OR_NULL` |
| probe 循环 / Probe loop | `board/src/board_driver.c` | 依赖/级联误判；部分 ABI 下循环状态被打坏；无名根无驱动刷屏<br>Bad dependency/cascade checks; loop state clobbered on some ABIs; spam from a nameless root with no driver | `IS_ERR_OR_NULL`、`board_probe_order_at`、循环状态 `volatile`、无名根静默禁用<br>`IS_ERR_OR_NULL`, `board_probe_order_at`, `volatile` loop state, silent disable of the nameless root |
| 任务优先级 / Task priority | `core/src/event_bus.c` + `osal/src/osal_freertos.c` | `K_DISPATCH_PRIO` 过大触发 FreeRTOS assert<br>Oversized `K_DISPATCH_PRIO` triggered a FreeRTOS assert | 默认 prio=24；`osal_clamp_task_priority` 钳到 `[0, configMAX_PRIORITIES)`<br>Default prio=24; `osal_clamp_task_priority` clamps to `[0, configMAX_PRIORITIES)` |
| 对象池查询 / Pool query | `osal/src/osal_freertos.c` | bus 层调用 `osal_pool_is_used`，FreeRTOS 后端缺失<br>The bus layer calls `osal_pool_is_used`, missing in the FreeRTOS backend | 补齐实现（与 `osal_null` 语义一致）<br>Implemented (same semantics as `osal_null`) |
| spinlock 告警 / Spinlock warning | `system_cpp/src/system_cmd.cpp` | `-Werror=unused-result` | `COMPAT_IGNORE_RESULT` 包裹 `osal_spinlock_*`<br>Wrap `osal_spinlock_*` in `COMPAT_IGNORE_RESULT` |

验证建议（非 ESP 板）：默认 CMake Preset 全量编译；改过 generator 后确认生成的 `board_probe.c` 中 extern **无** `weak`。

Validation (non-ESP boards): full build with the default CMake preset; after touching the generator, confirm the generated `board_probe.c` externs have **no** `weak`.

---

## 2. ESP-IDF 特殊性（仅 ESP 路径）/ ESP-IDF Specifics (ESP Path Only)

### 2.1 Include 顺序（stubs vs 生成头）/ Include Order (stubs vs. generated headers)

`ide/stubs/board_nodes.h` 里 `DEV_ID_COUNT=1` 仅供 IDE；若排在生成目录之前，会盖住 dtc-lite 真表，表现为：

`DEV_ID_COUNT=1` in `ide/stubs/board_nodes.h` is for the IDE only; if it sorts ahead of the generated dir it shadows the real dtc-lite table, showing as:

- `probe complete: 0 ok, 0 fail`
- `device_find_by_label` 找不到真实设备
- `device_find_by_label` cannot find the real devices

`esp_idf.cmake` 中 **必须**保持：
`esp_idf.cmake` **must** keep:

```text
GENERATED_BOARD_DIR  →  SCRUBBER_GEN_DIR  →  ide/stubs  →  KCONFIG_GEN_DIR(空壳)
```

> 上例：生成目录在前、stubs 次之、空壳 `config.h` 最后（详见下）。
> Above: generated dirs first, then stubs, then the empty-shell `config.h` (see below).

空壳 `config.h` 必须在 stubs **之后**：stubs 的 `config.h` 提供 `CONFIG_SYS_LOG_USE_PRINTF` 等；空壳若抢先则触发 `SYS_LOG backend not configured`。

The empty-shell `config.h` must come **after** stubs: the stubs' `config.h` provides `CONFIG_SYS_LOG_USE_PRINTF` and friends; if the shell wins, you hit `SYS_LOG backend not configured`.

非 ESP 的 genconfig 真 `config.h` 路径不受此约束。
The non-ESP genconfig real `config.h` path is not subject to this constraint.

### 2.2 强引用与静态库 / 跨组件驱动 / Strong References, Static Libs, and Cross-Component Drivers

- **同组件**（`drivers/*/src/*.c` 在 `libmini_tree.a` 内）：`board_probe` 强引用即可从同一 `.a` 抽出 `.o`。
  **Same component** (`drivers/*/src/*.c` inside `libmini_tree.a`): the strong reference in `board_probe` pulls the `.o` out of the same `.a`.
- **树外组件**（当前仅 `driver_ws2812`）：对该组件使用 IDF `WHOLE_ARCHIVE`，保证 `board_driver_probe_*` 进最终 ELF。
  **Out-of-tree component** (currently only `driver_ws2812`): use IDF `WHOLE_ARCHIVE` on it so `board_driver_probe_*` lands in the final ELF.

板级扩展 dtc-lite（仅树外驱动需要；产品驱动已由 GLOB 扫入）：

Board-level dtc-lite extension (only needed for out-of-tree drivers; product drivers are already GLOB-scanned):

```cmake
set(MINI_TREE_DTC_EXTRA_SCAN_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../driver_ws2812/src")
set(MINI_TREE_DTC_EXTRA_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../driver_ws2812/src/ws2812_drv.c")
```

> 上例：仅树外驱动需要；产品驱动（37 个）已由 `drivers/*/src` GLOB 扫入。
> Above: only needed for out-of-tree drivers; product drivers (37) are already GLOB-scanned via `drivers/*/src`.

### 2.3 OSAL / 优先级 / OSAL / Priority

ESP-IDF 默认 `configMAX_PRIORITIES` 常为 25（合法 0..24）。中间件默认 dispatch prio=24，并在 OSAL 层钳位，避免 assert。

ESP-IDF's default `configMAX_PRIORITIES` is usually 25 (legal range 0..24). The middleware defaults dispatch prio to 24 and clamps in the OSAL layer to avoid asserts.

### 2.4 Xtensa windowed ABI

`board_driver_probe_all` 中 `volatile` 循环状态最初为修复 ESP32（Xtensa）上 `call8` 打坏调用方寄存器导致的只 probe 首设备问题。ARM 上作为防御性对齐保留，避免跨板行为分叉。

The `volatile` loop state in `board_driver_probe_all` originally fixed an ESP32 (Xtensa) issue where `call8` clobbered caller registers and only the first device got probed. It is kept on ARM as defensive alignment so behavior doesn't diverge across boards.

### 2.5 `CONFIG_*` 双源 / Dual Source of `CONFIG_*`

IDF 已注入 `sdkconfig.h`。`esp_idf.cmake` 只生成空壳 `config.h`，避免与完整 genconfig 头 `-Werror=redefined`。

IDF already injects `sdkconfig.h`. `esp_idf.cmake` only generates an empty-shell `config.h` to avoid `-Werror=redefined` against the full genconfig header.

> 自 Kconfig 拆分后（`Kconfig` / `Kconfig.projbuild` / `Kconfig.mini_tree` 三文件），ESP 路径下 `idf.py menuconfig` 会在顶层菜单显示 "mini_tree Configuration" 子菜单，所有 mini_tree 的 `CONFIG_*` 开关经 IDF confgen 求值后写入 `sdkconfig.h`。`esp_idf.cmake` 中的 `file(STRINGS)` 软编码仍保留以向后兼容旧 sdkconfig，可逐步迁到 `if(CONFIG_OSAL_FREERTOS)` 等 IDF 原生 CMake 变量。
> After the Kconfig split (`Kconfig` / `Kconfig.projbuild` / `Kconfig.mini_tree`), `idf.py menuconfig` on the ESP path shows a "mini_tree Configuration" submenu at the top level; all mini_tree `CONFIG_*` switches are evaluated by IDF confgen and written into `sdkconfig.h`. The `file(STRINGS)` soft-coding in `esp_idf.cmake` is kept for backward compat with legacy sdkconfigs and can be migrated incrementally to IDF-native CMake variables like `if(CONFIG_OSAL_FREERTOS)`.

---

## 3. 推荐：ESP 板删除 vendored `lib/`，改走 IDF / Fetch / Recommended: Drop Vendored `lib/` on ESP Boards, Use IDF / Fetch

现状：`lib/` 已只 vendor **FreeRTOS（v11.3.0）、RT-Thread（v5.3.0）、ETL**；TinyUSB / lwIP / cJSON 与其余积木（LVGL、u8g2、littlefs、FatFs、SFUD、Mbed TLS、coreMQTT、coreHTTP、nanopb、miniz、MCUBoot、FreeModbus、libmodbus、CMSIS-DSP、MultiButton、EasyFlash、EasyLogger、FlashDB）均为**链接期 FetchContent**（`mini_tree_link_*`）。在 **ESP-IDF** 上与内核/组件重复的部分仍应裁剪，避免体积、版本、许可证维护成本。

Current state: `lib/` vendors only **FreeRTOS (v11.3.0), RT-Thread (v5.3.0), and ETL**; TinyUSB / lwIP / cJSON are now **config-time FetchContent**, and the rest (LVGL, u8g2, littlefs, FatFs, SFUD, Mbed TLS, coreMQTT, coreHTTP, nanopb, miniz, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB) are **link-time FetchContent** (`mini_tree_link_*`). Under **ESP-IDF**, anything duplicating the kernel/components should still be dropped to avoid size, version, and license maintenance costs.

**推荐（ESP 板工程）/ Recommended (ESP board project):**

1. **删除**（或不再编入）`mini_tree/lib` 下与 IDF 重叠的树：至少 `freeRTOS`、`lwip`；`rtthread` 在 ESP 路径本就不使用。
   **Remove** (or stop compiling) the trees under `mini_tree/lib` that overlap IDF: at least `freeRTOS` and `lwip`; `rtthread` is unused on the ESP path anyway.
2. **OSAL**：继续用 `osal_freertos.c` 对接 IDF 内置 FreeRTOS（`esp_idf.cmake` 已强制 `CONFIG_OSAL_FREERTOS`）。
   **OSAL**: keep using `osal_freertos.c` against IDF's built-in FreeRTOS (`esp_idf.cmake` already forces `CONFIG_OSAL_FREERTOS`).
3. **第三方有用库**（ETL、cJSON、TinyUSB 等）：用板级 / 中间件 `cmake/*.cmake`（FetchContent）或 IDF Component Manager（`idf_component.yml`）拉取，**不要**长期把整棵 upstream 拷进 `lib/`。
   **Useful third-party libs** (ETL, cJSON, TinyUSB, etc.): pull via the board / middleware `cmake/*.cmake` (FetchContent) or the IDF Component Manager (`idf_component.yml`); **don't** vendor a full upstream tree into `lib/` long-term.
4. **TinyUSB**：优先 Espressif 组件或官方 registry；DCD/`CFG_TUSB_MCU` 仍由板级配置，中间件不绑 MCU。
   **TinyUSB**: prefer the Espressif component or the official registry; DCD/`CFG_TUSB_MCU` stays board-configured — the middleware binds no MCU.

中间件仓库可继续保留 `lib/` 作为**非 ESP**（Cube / 裸机）的可选 vendored / fetch 落点；ESP 板应视 `lib/` 为可裁剪，默认不依赖其中的 FreeRTOS/lwIP 副本。

The middleware repo may keep `lib/` as an optional vendored / fetch landing spot for **non-ESP** (Cube / bare-metal) builds; ESP boards should treat `lib/` as trimmable and not depend on its FreeRTOS/lwIP copies by default.

---

## 4. 板级对照清单（ESP）/ Board Checklist (ESP)

1. `components/mini_tree` 使用本仓库（或 submodule）并走 `esp_idf.cmake`。
   `components/mini_tree` uses this repo (or a submodule) and goes through `esp_idf.cmake`.
2. Include 顺序未被板级 `EXTRA_INCLUDE` 打乱。
   The include order isn't disturbed by board `EXTRA_INCLUDE`.
3. 树外 `DRIVER_REGISTER`：`MINI_TREE_DTC_EXTRA_SCAN_DIRS` + 组件 `WHOLE_ARCHIVE`（或同库编译）。
   Out-of-tree `DRIVER_REGISTER`: `MINI_TREE_DTC_EXTRA_SCAN_DIRS` + component `WHOLE_ARCHIVE` (or compile into the same lib).
4. 烧录后应看到真实 `probing '...'`，而非 `0 ok, 0 fail`。
   After flashing you should see real `probing '...'` lines, not `0 ok, 0 fail`.
5. 逐步去掉 vendored `lib/freeRTOS`、`lib/lwip` 等，确认仍链到 IDF 提供的实现。
   Gradually drop vendored `lib/freeRTOS`, `lib/lwip`, etc., and confirm you still link against the IDF-provided implementations.

---

## 5. 刻意不同步到中间件的内容 / Deliberately Not Synced to the Middleware

| 内容 / Content | 原因 / Reason |
|------|------|
| WS2812 驱动 / dtsi / dtc 扫描路径<br>WS2812 driver / dtsi / dtc scan paths | 板级产品驱动，不属于中间件<br>Board-level product driver; not part of the middleware |
| `compiler_compat` 中 `WS2812` 魔法槽<br>The `WS2812` magic slot in `compiler_compat` | 同上；板级需要时再加<br>Same as above; add it when the board needs it |
| 板级 `.clangd` / 具体 GPIO 映射<br>Board `.clangd` / concrete GPIO mappings | 板工程私有<br>Private to the board project |
