# ESP-IDF Integration Notes and Recent Fixes

This document records the **pure fixes** back-ported from ESP32-S3 board-level validation into the middleware, plus the ESP-path specifics and dependency-strategy recommendations.


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

Validation (ESP boards): full `idf.py build`; after touching the generator, confirm the generated `board_probe.c` externs have **no** `weak`.
---
## 2. ESP-IDF 特殊性（仅 ESP 路径）/ ESP-IDF Specifics (ESP Path Only)

### 2.1 Include 顺序（stubs vs 生成头）/ Include Order (stubs vs. generated headers)

`DEV_ID_COUNT=1` in `ide/stubs/board_nodes.h` is for the IDE only; if it sorts ahead of the generated dir it shadows the real dtc-lite table, showing as:

```text
GENERATED_BOARD_DIR  →  SCRUBBER_GEN_DIR  →  ide/stubs  →  KCONFIG_GEN_DIR(空壳)
```


The empty-shell `config.h` must come **after** stubs: the stubs' `config.h` provides `CONFIG_SYS_LOG_USE_PRINTF` and friends; if the shell wins, you hit `SYS_LOG backend not configured`.

### 跨组件驱动 / Strong References, Static Libs, and Cross-Component Drivers


Board-level dtc-lite extension (only needed for out-of-tree drivers; product drivers are already GLOB-scanned):
```cmake
set(MINI_TREE_DTC_EXTRA_SCAN_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../driver_ws2812/src")
set(MINI_TREE_DTC_EXTRA_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../driver_ws2812/src/ws2812_drv.c")
```

### 优先级 / OSAL / Priority

ESP-IDF's default `configMAX_PRIORITIES` is usually 25 (legal range 0..24). The middleware defaults dispatch prio to 24 and clamps in the OSAL layer to avoid asserts.
### 2.4 Xtensa windowed ABI

The `volatile` loop state in `board_driver_probe_all` originally fixed an ESP32 (Xtensa) issue where `call8` clobbered caller registers and only the first device got probed. It is kept on ARM as defensive alignment so behavior doesn't diverge across boards.
### Dual Source of `CONFIG_*`

IDF already injects `sdkconfig.h`. `esp_idf.cmake` only generates a `config.h` forwarder to `sdkconfig.h`.

---
## Dependency Strategy (already implemented in this branch)

This branch has completed ESP-ization: `lib/` vendors only **ETL**; the `freeRTOS` / `rtthread` / `threadx` / `uC-*` RTOS trees are removed, and the `cmake/*.cmake` FetchContent system is gone.

- **OSAL**: keep using `osal_freertos.c` against the IDF built-in FreeRTOS (`esp_idf.cmake` already forces `CONFIG_OSAL_FREERTOS`).
- **Third-party libs** (ETL, cJSON, TinyUSB, etc.): pull via the IDF Component Manager / registry (`idf_component.yml`); **don't** vendor a full upstream tree into `lib/` long-term.
- **TinyUSB**: prefer the Espressif component or the official registry; DCD/`CFG_TUSB_MCU` stays board-configured — the middleware binds no MCU.
---
## 4. 板级对照清单（ESP）/ Board Checklist (ESP)

---
## Deliberately Not Synced to the Middleware

| 内容 / Content | 原因 / Reason |
|------|------|
| WS2812 驱动 / dtsi / dtc 扫描路径<br>WS2812 driver / dtsi / dtc scan paths | 板级产品驱动，不属于中间件<br>Board-level product driver; not part of the middleware |
| `compiler_compat` 中 `WS2812` 魔法槽<br>The `WS2812` magic slot in `compiler_compat` | 同上；板级需要时再加<br>Same as above; add it when the board needs it |
| 板级 `.clangd` / 具体 GPIO 映射<br>Board `.clangd` / concrete GPIO mappings | 板工程私有<br>Private to the board project |
