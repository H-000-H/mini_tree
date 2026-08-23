# Device-Tree Porting Guide (Consolidated)

> All device-tree (DTS/DTSI) porting steps for wiring mini_tree to a new SoC / new board are consolidated here — no longer scattered across architecture / porting_guide / getting_started. For HAL backends, OSAL backends and the overall CMake flow, see "Related Docs" at the end.
>
> Every example below is written against the current repo mechanisms (`tools/dtc-lite.py`, `board/dtsi/*`, `drivers/bmp280`, etc.) and current APIs.

| Item | Content |
| :--- | :--- |
| **Audience** | Engineers porting to a new platform / board, or writing product drivers |
| **Prereq** | Basic Linux device-tree concepts (`compatible` / `reg` / `status` / `&label` override) |
| **Related** | [driver_guide.md](driver_guide.md) (driver authoring) · [architecture.md](architecture.md) (layering & boot) · [getting_started.md](getting_started.md) (build) |

---

## 1. Why device tree, and how it works here

mini_tree uses a **Linux-style device tree**, but does **not** depend on the kernel `dtc`: the self-authored `tools/dtc-lite.py` (Lark grammar + Transformer) parses `*.dts` / `*.dtsi` **at compile time** into plain C static tables (`board_nodes.h`, `dt_config_gen.h`, `board_devtable.c`, `board_probe.c`).

Key differences (vs Zephyr and others):

- Generated artifacts are **plain C static tables** — you can set breakpoints, inspect symbols, grep. Debug-friendly.
- Driver matching is done at **compile time** (`DRIVER_REGISTER` macro + dtc-lite scan): no `strcmp` matching and no dynamic registry at runtime.
- Pool sizes, host counts, etc. are **auto-derived from node counts** (`DTC_GEN_COUNT_*` macros) instead of hand-written magic numbers.

---

## 2. dtc-lite compile pipeline

```
board/dts/board.dts                ← board entry (/dts-v1/; includes; / { } ; &label overrides)
        │
        │  ① C preprocessor (#include / #define / #ifdef, system cpp; dt-bindings constants expand here)
        ▼
        │  ② Lark parse → AST (node tree, properties, &label reference resolution)
        ▼
        │  ③ Transformer generates:
        ▼
   <build>/generated/board/mini_tree/
        ├── board_nodes.h       device-ID enum (device_id_t) + node struct declarations
        ├── dt_config_gen.h     DTC_GEN_COUNT_* / DTC_GEN_*_HZ / host MAX constants
        ├── board_devtable.c    device table (name / compatible / label / properties)
        └── board_probe.c       probe/remove fn table generated from DRIVER_REGISTER
```

CLI (normally invoked by CMake; no need to run by hand):

```bash
python3 tools/dtc-lite.py board/dts/board.dts <build>/generated <driver_dirs...>
```

`driver_dirs` are the driver `.c` directories; dtc-lite scans their `DRIVER_REGISTER(...)` macros to build the probe table.

---

## 3. Where files live (three locations)

| Location | Path | Description |
| :--- | :--- | :--- |
| Middleware placeholder (this repo) | `board/dts/board.dts`, `board/dtsi/example-soc.dtsi`, `board/dtsi/drivers/*.dtsi`, `board/dtsi/vfs/*.dtsi` | generic templates, no real peripherals; `board.dts` with no nodes still runs the full pipeline |
| Platform project (formal) | pointed to via `BOARD_DTS` / `BOARD_DTSI_DIR` / `MINI_TREE_BOARD_PORT` | **real SoC / board files live here**, overriding the middleware placeholders |
| dt-bindings constants | `board/dt-bindings/<bus>/*.h` | macros for `#include <dt-bindings/...>` (e.g. `DTS_GPIO_DEFAULT_INTR`) |

> **Core rule**: this repo keeps only placeholder examples; put real board files in the platform project and override via CMake variables. Do **not** stuff vendor-specific dtsi into `board/dtsi/` (use `VENDOR_INC_DIRS` for vendor macros).

---

## 4. Writing node templates (DTSI)

### 4.1 SoC skeleton template

`board/dtsi/example-soc.dtsi` shows the standard skeleton: `cpus` / `soc` (with `compatible = "simple-bus"`, `ranges`) / `gpio` / `uart`. Copy it as `<soc>.dtsi`, replace numbers with vendor macros, then point `BOARD_DTSI_DIR` at it.

```dts
// platform project my_soc.dtsi (copy of example-soc.dtsi, renamed)
/include/ "dt-bindings/gpio/gpio-parameter.h"

/ {
    compatible = "vendor,my-soc";
    #address-cells = <1>;
    #size-cells = <0>;

    soc: soc {
        compatible = "simple-bus";
        ranges;
        #address-cells = <1>;
        #size-cells = <0>;

        gpio: gpio@0 {
            compatible = "vendor,gpio";
            reg = <0>;
            gpio-controller;
            #gpio-cells = <2>;
            status = "disabled";   // board turns on via &gpio { status = "okay"; }
        };

        i2c0: i2c@1 {
            compatible = "vendor,i2c";
            reg = <1>;
            #address-cells = <1>;
            #size-cells = <0>;
            i2c-clk = <400000>;   // bus clock, overridable at board level
            status = "disabled";
        };
    };
};
```

### 4.2 Product-driver node template

One `board/dtsi/drivers/<chip>.dtsi` per product driver, attached to a bus via `&i2c0 { }`, **all numbers 0-placeholder + `status = "disabled"`**, then enabled at board level. Pool size auto = `DTC_GEN_COUNT_<COMPAT_UPPER>` (node count, default 1).

```dts
// board/dtsi/drivers/bmp280.dtsi (existing repo template)
&i2c0 {
bmp280: bmp280@0 {
    compatible = "bosch,bmp280";
    reg = <0>;          // I2C 7-bit address, typically 0x76/0x77
    status = "disabled";
};
};
```

### 4.3 `status` convention

- `status = "okay"`: device participates in compilation and probing.
- `status = "disabled"`: dtc-lite still parses the node (generates ID / slot), but it does **not** enter the probe table and the driver is never invoked.
- The board turns it on and fills real values via `&label { status = "okay"; ...override... };`.

> **⚠ Override requires the `label` to be declared in some dtsi first (`label: node@x { }`).** If the `&label` reference points to a label that does not exist, dtc-lite does **not** error — it auto-*phantoms* an orphan node at `/soc/<label>` and attaches the `status`/overridden properties to it, leaving the real target node untouched. As a result the real node stays `disabled` and its properties (e.g. `clock-frequency`) are never overridden. Real case: `board.dts` uses `&cpu0`, but `cpu.dtsi` declares `cpu@0` with no `cpu0:` label, so `/cpus/cpu@0` stayed `disabled` while `status="okay"` and `clock-frequency=<72MHz>` went to a phantom node. Fix: label the node `cpu0: cpu@0 { ... }` so the reference hits.

---

## 5. Writing the board DTS (full example)

The board `board.dts` is the entry: it includes the SoC dtsi and turns on/overrides nodes:

```dts
// platform project boards/my_board/board.dts
/dts-v1/;

/include/ "my_soc.dtsi"          // platform SoC skeleton (BOARD_DTSI_DIR points to boards/my_board/dtsi/)
/include/ "bmp280.dtsi"          // product-driver template (middleware board/dtsi/drivers/, or platform-local)

/ {
    compatible = "vendor,my-board";
    #address-cells = <1>;
    #size-cells = <0>;
};

/* Turn on the GPIO controller, fill real base address */
&gpio {
    status = "okay";
    reg = <0x40021000>;
};

/* Turn on I2C0 and attach BMP280 (real 7-bit addr 0x76) */
&i2c0 {
    status = "okay";
    i2c-clk = <400000>;

    bmp280@76 {
        compatible = "bosch,bmp280";
        reg = <0x76>;
        status = "okay";
    };
};
```

> Note: as long as `&i2c0` is defined on this board (from `my_soc.dtsi`), the `&i2c0 { ... }` in `bmp280.dtsi` merges with the board-level one — the template's `status="disabled"` is overridden by the board's `status="okay"`. If the board already carries the bmp280 node itself, you need not include the template.

---

## 6. Generated artifacts explained

### 6.1 `board_nodes.h` — device-ID enum

Every `status="okay"` node (including `chosen` etc.) gets a `DEV_ID_<label>` enum value; business/driver code indexes devices by it, **not by string lookup**:

```c
typedef enum {
    DEV_ID_ = 0,
    DEV_ID_gpio = 1,
    DEV_ID_i2c0 = 2,
    DEV_ID_bmp280 = 3,
    DEV_ID_COUNT = 4
} device_id_t;
```

### 6.2 `dt_config_gen.h` — counts & host constants

Node count → `DTC_GEN_COUNT_<COMPAT_UPPER>`; also clocks, tick, host counts:

```c
#define DTC_GEN_COUNT_BOSCH_BMP280  1     // bmp280 node count → driver static-pool size
#define DTC_GEN_COUNT_I2C_MASTER    1
#define DTC_GEN_CPU_CLOCK_HZ        168000000
#define DTC_GEN_TICK_RATE_HZ        1000
#define DTC_GEN_I2C_HOST_MAX        3
```

Drivers use it to size static pools (see §8); pool size tracks node count automatically.

### 6.3 `board_devtable.c` / `board_probe.c`

- `board_devtable.c`: device table with name / compatible / label and parsed properties (int / str / bool / reg / irq).
- `board_probe.c`: the probe/remove function table dtc-lite generates from all `DRIVER_REGISTER` macros; `board_driver_probe_all()` iterates devices, matches drivers by compatible, and calls probe.

---

## 7. How a driver talks to DTS

### 7.1 Register the driver

`DRIVER_REGISTER(name, compat, probe_fn, remove_fn)` — `compat` must **exactly** match the dtsi node's `compatible` (comma-separated string):

```c
// drivers/bmp280/src/bmp280_drv.c
#include "driver.h"
#include "device.h"
#include "dt_config_gen.h"

static int bmp280_probe(struct device* pdev)
{
    /* full example in §8 */
}

static int bmp280_remove(struct device* pdev)
{
    /* standard dev_lifecycle remove sequence, see driver.h comments */
}

DRIVER_REGISTER(bmp280, "bosch,bmp280", bmp280_probe, bmp280_remove);
```

> **No `strcmp` at runtime**: dtc-lite collects `DRIVER_REGISTER` into `board_probe.c` at compile time; `board_driver_probe_all()` calls the table directly. If you change `compatible`, rerun dtc-lite (CMake does it automatically).

### 7.2 Read properties in probe

`device_get_prop_int / _str / _bool / _int_array`, `device_get_reg / _irq` all read from the compile-time table:

```c
int reg_addr = device_get_prop_int(pdev, "reg", -1);          // I2C address
int clk      = device_get_prop_int(pdev, "i2c-clk", 400000);   // bus clock
const char* name = device_get_name(pdev);                     // node label/name
```

### 7.3 Get a dependency device (e.g. the I2C bus)

When a node is attached via `&i2c0`, the driver gets the bus device with `device_get_phandle_dev` or `board_dev_get`:

```c
struct device* bus = board_dev_get(DEV_ID_i2c0);   // compile-time enum, zero lookup
if (IS_ERR_OR_NULL(bus))
    return PTR_ERR(bus);
```

---

## 8. End-to-end porting example (compilable code)

A self-contained sensor driver + board dts + CMake injection forming the smallest closed loop.

### 8.1 Driver: `drivers/bmp280/src/bmp280_drv.c` (excerpt, real style)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "bmp280_drv.h"
#include "bmp280_regs.h"
#include "compiler_compat.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-i2c.h"
#include <stddef.h>
#include <stdint.h>

/* Pool size auto-generated from the DTS node count: more bmp280 nodes ⇒ bigger pool */
#define BMP280_POOL_COUNT  DTC_GEN_COUNT_BOSCH_BMP280

struct bmp280_device {
    struct device* bus;       /* I2C bus device */
    int            addr;      /* 7-bit address */
    /* ... calibration coefficients, state ... */
};

/* Static pool: fixed size at compile time, no runtime heap allocation */
static struct bmp280_device s_bmp280_pool[BMP280_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_bmp280_used[BMP280_POOL_COUNT] COMPAT_ALIGNED(4);

static struct bmp280_device* bmp280_claim(void)
{
    for (int i = 0; i < BMP280_POOL_COUNT; i++)
    {
        if (!s_bmp280_used[i])
        {
            s_bmp280_used[i] = 1u;
            return &s_bmp280_pool[i];
        }
    }
    return NULL;
}

static int bmp280_probe(struct device* pdev)
{
    struct bmp280_device* dev = bmp280_claim();
    if (dev == NULL)
        return MINI_ERR_NOMEM;

    /* 1. read board-level properties (reg = I2C address) */
    dev->addr = device_get_prop_int(pdev, "reg", -1);
    if (dev->addr < 0)
        return MINI_ERR_INVAL;

    /* 2. get the bus dependency (compile-time enum, zero lookup) */
    dev->bus = board_dev_get(DEV_ID_i2c0);
    if (IS_ERR_OR_NULL(dev->bus))
        return PTR_ERR(dev->bus);

    /* 3. bind lifecycle (register during probe only, see driver.h) */
    /* device_lc_bind(pdev); */

    SYS_LOGI("bmp280", "probed @0x%02x on i2c0", dev->addr);
    return MINI_OK;
}

static int bmp280_remove(struct device* pdev)
{
    /* dev_lc_remove_start / device_ops_unregister / dev_lc_remove_drain /
       dev_lc_remove_finish standard sequence, see driver.h comments */
    return MINI_OK;
}

DRIVER_REGISTER(bmp280, "bosch,bmp280", bmp280_probe, bmp280_remove);
```

### 8.2 Board DTS: `boards/my_board/board.dts`

See §5 (copy as-is).

### 8.3 CMake injection (platform project)

The platform project injects its tree via these variables (set before `add_subdirectory(mini_tree)`):

```cmake
set(MINI_TREE_BOARD_PORT   ${CMAKE_CURRENT_SOURCE_DIR}/boards/my_board)
set(BOARD_DTS              ${MINI_TREE_BOARD_PORT}/board.dts)
set(BOARD_DTSI_DIR         ${MINI_TREE_BOARD_PORT}/dtsi)   # contains my_soc.dtsi
# vendor macros if needed:
# set(VENDOR_INC_DIRS      ${MINI_TREE_BOARD_PORT}/vendor_inc)
```

| Variable | Effect |
| :--- | :--- |
| `BOARD_DTS` | full board `.dts` entry path |
| `BOARD_DTSI_DIR` | board `dtsi/` directory (SoC skeleton, etc.) |
| `MINI_TREE_BOARD_PORT` | board-port root directory |
| `VENDOR_INC_DIRS` | extra `-I` for vendor headers (macros beyond dt-bindings) |

---

## 9. Application-layer porting (getting your business running)

> Porting is **not just adding VFS/driver device nodes** — the goal is to get your **application code** running on the new platform. In practice this **usually needs only DTS + HAL changes**: the DTS declares peripherals & properties, the HAL hooks up the underlying hardware, and application code (business tasks, device access, framework services) is **cross-platform with zero changes**.

### 9.1 What the application layer needs

| Layer | What the new platform must do | Cross-platform? |
| :--- | :--- | :--- |
| Board entry `main` | clocks/heap/console + two-stage boot | **platform-specific** (change) |
| App task module `app/<module>/` | business logic: task callback + task registration | **zero change** (once DTS/HAL ready) |
| Device access | `device_find_by_label` / `device_open` / `device_ioctl` | zero change (via DTS label) |
| Framework services | OSAL tasks / EventBus / config_store / scheduler | zero change (OSAL backends unify) |

### 9.2 API key points

- Boot: `mini_tree_pre_os_init()` → `mini_tree_start_tasks()` (**internally calls `board_driver_probe_all` to probe peripherals**) → `xscheduler_start()`.
- Task creation:
  - C cooperative: `xscheduler_task_create(task, name, cb, period_ms)` (TCB statically allocated by the caller).
  - C preemptive (`XTASK_PREEMPT`): `x_scheduler_task_create(name, period_ms, priority, cb, param)` (pool-allocated).
  - C++ bare metal: `osal_task_create(name, stack_size, period, entry, param1, ...)` overload, returns `etl::optional<x_task_handle_t>`.
  - OS backends (FreeRTOS/RT-Thread): unified C API `osal_task_create`.
- Main loop: bare metal `while(1) x_scheduler_poll()` (or `mini_tree_system_loop()`); OS backends start their own scheduler.

**Bare-metal scheduler tick source (`xscheduler_start()`) — two-level selection:**

| Platform | DTS has `chosen { scheduler-tim = &timN; }`? | Result |
| :--- | :--- | :--- |
| Cortex-M | Not set | **SysTick by default** (`hal/systick`, architecture standard, zero-config out of the box) |
| Cortex-M | Set | **chosen TIM explicit override** (`CHOSEN_SCHEDULER_TIM` resolved at compile time; free up SysTick by using a generic TIM) |
| RISC-V | **Required** | chosen TIM (mtime / SoC timer as a normal `tim` node); SysTick path is compiled out |

> **Frequency via DTS**: SysTick's tick frequency comes from `/chosen` `tick-rate` (`DTC_GEN_TICK_RATE_HZ`), CPU clock from `/cpus/cpu@0` `clock-frequency` (`DTC_GEN_CPU_CLOCK_HZ`); `hal_systick` only hard-codes the register base (`HAL_SYSTICK_BASE`, default `0xE000E010`, overridable), never any frequency. RISC-V boards must set `scheduler-tim` explicitly, otherwise `hal_systick_init` returns `MINI_ERR_NOTSUPP` and the scheduler won't start.

#### 9.2.1 SysTick DTS configuration (no VFS layer; dtc-lite macro injection)

SysTick is **not** a DTS device node (not on a bus, no `compatible`, no VFS/probe), but **frequency and timing are still configured in DTS**: `dtc-lite` reads attributes from non-device nodes and generates macros into `dt_config_gen.h`, which `hal_systick_init(DTC_GEN_TICK_RATE_HZ)` consumes. In your `<soc>.dtsi` (pointed to by `BOARD_DTSI_DIR`):

```dts
/ {
	chosen {
		tick-rate = <1000>;              /* tick frequency Hz: scheduler IRQ every 1ms (default) */
		heap-size = <32768>;             /* existing: heap size */
		/* Only for RISC-V, or to override SysTick with a generic TIM, add: */
		/* scheduler-tim = &tim0; */
	};

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;
		cpu@0 {
			device_type = "cpu";
			compatible = "<vendor>,cpu";
			reg = <0>;
			clock-frequency = <72000000>;  /* CPU clock (HCLK), must be the real value! */
		};
	};
};
```

| DTS location | Property | Generated macro | SysTick purpose |
| :--- | :--- | :--- | :--- |
| `/cpus/cpu@0` | `clock-frequency` | `DTC_GEN_CPU_CLOCK_HZ` | Compute SysTick LOAD reload value |
| `/chosen` | `tick-rate` | `DTC_GEN_TICK_RATE_HZ` | SysTick IRQ frequency / `tick_delay` |
| `/chosen` | `scheduler-tim = &timN` | `CHOSEN_SCHEDULER_TIM` | RISC-V or explicit override to generic TIM |
| `/chosen` | `heap-size` | `DTC_GEN_HEAP_SIZE` | Unrelated to tick (existing config) |

> **Two constraints**
> 1. **`clock-frequency` must be a real value**: the repo's `board/dtsi/example-soc.dtsi` template uses placeholder `<0>`. If you explicitly write `cpus/cpu@0` with `clock-frequency` 0 or missing, `dtc-lite` emits `#error`, failing the build to force a fix (any non-ESP CPU must have a clock, SysTick uses it for LOAD). Only placeholder/ESP boards without `cpus` fall back to 16 MHz.
> 2. **`tick-rate` semantics**: the scheduler advances tick by `tick_delay = 1000 / DTC_GEN_TICK_RATE_HZ` (ms per IRQ). Default 1000 → +1ms per IRQ; 500 → +2ms per IRQ (keeps a 1ms logical tick, correct); >1000 falls back to 1ms. Keep 1000 recommended.

#### 9.2.2 ESP exception: no cpus / clock configuration needed

**ESP does not need `cpus` / `clock-frequency`; the SysTick layer is fully excluded**, guaranteed threefold:

1. **`hal_systick.c` is not in the ESP source list**: `cmake/esp_idf.cmake` has its own `HAL_SRCS`, which does not compile `hal_systick.c`.
2. **ESP forces `CONFIG_OSAL_FREERTOS`** (ties to the IDF kernel), so `xtask_coop/preempt` is not compiled and nothing calls `hal_systick_init`; tick belongs to **IDF's SYSTIMER + FreeRTOS** (`CONFIG_FREERTOS_HZ`).
3. **ESP's default board DTS has no `cpus`** → dtc-lite takes the fallback, no `#error`, builds normally.

> If an ESP board project explicitly writes `cpus/cpu@0` with `clock-frequency = <0>`, the `#error` fires — but ESP normally should not write `cpus` (it uses the IDF clock tree); keeping the constraint catches copying the placeholder template without fixing it.

#### 9.2.3 Why SysTick does not go through the VFS layer

The VFS layer serves **DTS device nodes** (probe, open/close, `get_hal_dev`, ioctl). SysTick is **not a device node**, hence no VFS:

| Dimension | Generic TIM (via VFS) | SysTick (no VFS) |
| :--- | :--- | :--- |
| DTS role | Device node (`tim@...` + compatible + probe) | **Non-device node**: no compatible, not on a bus, no register table |
| Config source | Node properties, fetched via `device_get_prop_*` | DTS **macro** injection (`DTC_GEN_TICK_RATE_HZ` / `DTC_GEN_CPU_CLOCK_HZ`), consumed directly by the scheduler |
| Operations | Full open/close/ioctl set | Only `init / deinit / irq_handler` |
| IRQ routing | NVIC + `interrupt_hw_enable` + VIRQ dispatch | **Core exception (exception 15)**, `SysTick_Handler`(weak) → `hal_systick_irq_handler`(strong) direct call |

Conclusion: SysTick is an architecture standard, has minimal operations, and its IRQ is a core exception; forcing it into the VFS device model would need a fake node + a `vfs-systick` driver + dtc-lite support — pure added complexity. **The scheduler calling `hal_systick_*` directly** best matches the "config via DTS macros, ops via HAL direct" layering. `hal_systick` and the generic TIM are two independent paths, each with its own role.

### 9.3 Board entry — C version (`main.c`)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "system_init.h"
#include "xtask.h"          /* bare-metal scheduler (CONFIG_OSAL_NULL) */
#include "led.h"            /* app task module */

/* platform: clocks / heap / console init (HAL backend, platform-specific) */
static void platform_boot(void);

int main(void)
{
    platform_boot();                  /* platform-specific: clocks, SysTick, console */

    mini_tree_pre_os_init();          /* framework static init */
    mini_tree_start_tasks();          /* probe peripherals + start framework tasks */
    xscheduler_start();               /* SysTick by default; DTS chosen overrides to generic TIM */

    App_Led_register();               /* register app task */

#if defined(CONFIG_OSAL_NULL)
    for (;;)
        x_scheduler_poll();           /* bare-metal time-slice poll (incl. preemptive) */
#elif defined(CONFIG_OSAL_FREERTOS)
    vTaskStartScheduler();
#elif defined(CONFIG_OSAL_RTTHREAD)
    rt_system_scheduler_start();
#endif
    return 0;
}
```

### 9.4 App task module — C version (`app/led/led.c` + `led.h`)

`led.h` (declaration + static TCB):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "xtask.h"          /* x_task / xscheduler_task_create */

#define APP_LED_NAME      "Led_Task"
#define APP_LED_PERIOD_MS 500u

void App_Led_register(void);          /* task-registration entry (called from main) */
extern x_task g_led_task;             /* static TCB (caller-allocated for cooperative) */
```

`led.c` (business callback + registration, cooperative `XTASK_COOP`):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "led.h"
#include "device.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"

static const char* const s_kTag = "Led_Task";
x_task g_led_task;                    /* static TCB */

static struct device* s_led_dev = NULL;

/* task callback: toggle LED each deadline (no-op safely if DTS lacks a led node) */
static void led_task_cb(x_task* self)
{
    struct vfs_gpio_arg arg = {0};
    int ret;
    COMPAT_IGNORE_RESULT(self);

    if (s_led_dev == NULL)
    {
        struct device* pdev = device_find_by_label("led");
        if (IS_ERR_OR_NULL(pdev))
            return;
        if (device_open(pdev, NULL) != MINI_OK)
        {
            SYS_LOGE(s_kTag, "device_open(led) failed");
            return;
        }
        s_led_dev = pdev;
    }

    ret = device_ioctl(s_led_dev, GPIO_CMD_TOGGLE, &arg, sizeof(arg), 100);
    if (ret != MINI_OK)
        SYS_LOGE(s_kTag, "device_ioctl(TOGGLE) failed: %d", ret);
}

/* task registration: cooperative signature (task, name, cb, period_ms) */
void App_Led_register(void)
{
    if (xscheduler_task_create(&g_led_task, APP_LED_NAME, led_task_cb, APP_LED_PERIOD_MS) == 0)
        SYS_LOGE(s_kTag, "register failed");
}
```

### 9.5 App task module — C++ version (`app/led/led.hpp` + `led.cpp`)

`led.hpp` (static TCB + registration interface):

```cpp
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "xtask.h"
#include <cstdint>
#include <etl/optional.h>
#include <etl/string.h>

namespace App_Led
{
    constexpr uint32_t kPeriodMs = 500u;                    /* period ms */
    const etl::string<16> kName = "Led_Task";               /* task name */
    x_task g_led_task{};                                    /* static TCB */
    void led_task_cb(x_task* self);                         /* callback */
    etl::optional<int> register_task();                     /* registration */
} // namespace App_Led
```

`led.cpp` (C++ uses the bare-metal `osal_task_create` overload, returns `etl::optional`):

```cpp
/* SPDX-License-Identifier: Apache-2.0 */
#include "led.hpp"
#include "device.h"
#include "osal_null.h"          /* bare-metal C++ osal_task_create overload */
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"

namespace App_Led
{
    static struct device* s_led_dev = nullptr;

    void led_task_cb(x_task* self)
    {
        struct vfs_gpio_arg arg = {0};
        int ret;
        COMPAT_IGNORE_RESULT(self);

        if (s_led_dev == nullptr)
        {
            struct device* pdev = device_find_by_label("led");
            if (IS_ERR_OR_NULL(pdev))
                return;
            if (device_open(pdev, nullptr) != MINI_OK)
            {
                SYS_LOGE(kName.c_str(), "device_open(led) failed");
                return;
            }
            s_led_dev = pdev;
        }

        ret = device_ioctl(s_led_dev, GPIO_CMD_TOGGLE, &arg, sizeof(arg), 100);
        if (ret != MINI_OK)
            SYS_LOGE(kName.c_str(), "device_ioctl(TOGGLE) failed: %d", ret);
    }

    etl::optional<int> register_task(void)
    {
        /* bare-metal C++ overload: cooperative = (name, stack_size, period, entry, param1);
           with CONFIG_XTASK_PREEMPT=y the 3rd arg is priority and stack_size is reused as period */
        auto handle = osal_task_create(kName.c_str(), 0u, kPeriodMs,
                                       led_task_cb, nullptr);
        if (!handle)
            return etl::nullopt;
        return etl::make_optional(MINI_OK);
    }
} // namespace App_Led
```

> On bare-metal C++ prefer the `osal_task_create` overload (consistent OSAL habits); C projects call `xscheduler_task_create` directly. OS backends (FreeRTOS/RT-Thread) always go through the C API `osal_task_create`. With preemptive scheduling (`XTASK_PREEMPT`), C uses `x_scheduler_task_create(name, period, priority, cb, param)`.

---

## 10. Verification flow

1. **genconfig**: `.config` → `config.h`; confirm `CONFIG_*` matches your selection.
2. **dtc-lite**: runs automatically at build; check `<build>/generated/board/mini_tree/board_nodes.h` contains `DEV_ID_bmp280`, and `dt_config_gen.h` has `DTC_GEN_COUNT_BOSCH_BMP280 >= 1`.
3. **Compile**: build the `mini_tree` static lib; confirm `board_driver_probe_bmp280` is collected and there are no undefined symbols (`board_dev_get` comes from the generated `board_devtable.c`).
4. **Run**: `board_driver_probe_all()` runs early in boot; the log should print `bmp280 probed @0x76 on i2c0`.
5. **Link**: use the platform linker script and confirm `ERR_SECTION_BASE` is present (see memory_footprint.md §1).

---

## 11. Common pitfalls & troubleshooting

| Symptom | Cause | Fix |
| :--- | :--- | :--- |
| `DEV_ID_xxx` not generated | node `status="disabled"` or template not included | enable it at board level: `&label { status = "okay"; }` |
| `&label { status="okay" }` leaves the node `DISABLED` / frequency not applied | **label `label` was never declared in any dtsi**; dtc-lite silently *phantoms* an orphan node under `/soc`, so the override never reaches the target | add the label to the target node, e.g. `cpu0: cpu@0 { ... }`, so `&cpu0` resolves exactly; verify `.status` / properties via `git diff` on `board_devtable.c` |
| probe never called | `DRIVER_REGISTER` compat ≠ dtsi `compatible` (space/case) | make them exactly identical; rerun dtc-lite |
| pool overflow / `MINI_ERR_NOMEM` | more board nodes than expected | `DTC_GEN_COUNT_*` follows node count; check for un-enabled nodes |
| driver can't find bus | `board_dev_get(DEV_ID_i2c0)` errors | confirm `i2c0` is `status="okay"` and has an enum in `board_nodes.h` |
| changed dts not effective | incremental build didn't rerun dtc-lite | clean `<build>/generated` or trigger CMake reconfigure (dts / `.config` are `CONFIGURE_DEPENDS`) |
| vendor macros not found | dt-bindings path missing | put in `board/dt-bindings/` or use `VENDOR_INC_DIRS` |

> **Debug hint**: generated artifacts are plain C tables — grep `<build>/generated/board/mini_tree/board_probe.c` for your driver's probe, or open `board_nodes.h` to inspect the ID enum.

---

## 12. Related docs

- [driver_guide.md](driver_guide.md) — driver authoring (probe/remove, dev_lifecycle, fops)
- [architecture.md](architecture.md) — layering & boot sequence
- [getting_started.md](getting_started.md) — build & integration
- [memory_footprint.md](memory_footprint.md) — memory/flash benchmarks (incl. `ERR_SECTION_BASE`)

> This document merges the former `board_devicetree.md` (device-tree pipeline overview) and `porting_guide.md` (porting checklist); their content is incorporated into the sections above. Examples follow the current `board/dtsi`, `tools/dtc-lite.py`, and `drivers/bmp280` mechanisms.
