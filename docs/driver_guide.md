# 设备树与驱动 / Device Tree and Drivers

> DTS 布局、dtc-lite 流水线、`DRIVER_REGISTER`、compatible 与属性契约。
> DTS layout, the dtc-lite pipeline, `DRIVER_REGISTER`, and the compatible/property contracts.

| 项 / Item | 内容 / Description |
| :--- | :--- |
| **读者 / Audience** | 写平台 dtsi / 写 VFS 驱动的人<br>People writing platform dtsi / VFS drivers |
| **前置 / Prereq.** | [getting_started.md](getting_started.md) · [porting_guide.md](porting_guide.md) |
| **相关 / Related** | [tools/README.md](../tools/README.md) · [architecture.md](architecture.md) |

---

## 目录 / Table of Contents

- [设备树与驱动 / Device Tree and Drivers](#设备树与驱动-device-tree-and-drivers)
  - [目录 / Table of Contents](#目录-table-of-contents)
  - [1. 文件布局 / File Layout](#1-文件布局-file-layout)
    - [中间件（本仓库）/ Middleware (this repo)](#中间件本仓库-middleware-this-repo)
    - [平台工程（推荐）/ Platform project (recommended)](#平台工程推荐-platform-project-recommended)
  - [2. dtc-lite 流水线 / dtc-lite Pipeline](#2-dtc-lite-流水线-dtc-lite-pipeline)
  - [3. DRIVER_REGISTER](#3-driver_register)
  - [4. 当前仓库已注册驱动（扫描结果）/ Registered Drivers in This Repo (scan result)](#4-当前仓库已注册驱动扫描结果-registered-drivers-in-this-repo-scan-result)
  - [5. compatible 与属性 / Compatible Strings and Properties](#5-compatible-与属性-compatible-strings-and-properties)
    - [5.1 命名习惯 / Naming Conventions](#51-命名习惯-naming-conventions)
    - [5.2 属性直投 / Property Injection](#52-属性直投-property-injection)
    - [5.3 status / criticality / deps](#53-status-criticality-deps)
  - [6. 运行期 API / Runtime API](#6-运行期-api-runtime-api)
  - [7. Remove 生命周期 / Remove Lifecycle](#7-remove-生命周期-remove-lifecycle)
  - [相关文档 / Related Docs](#相关文档-related-docs)

---

## 1. 文件布局 / File Layout

### 中间件（本仓库）/ Middleware (this repo)

| 路径 / Path | 说明 / Description |
| :--- | :--- |
| `board/dts/board.dts` | **占位**根节点（仅 `mini-tree,placeholder`，无外设）；板级用 `BOARD_DTS` 覆盖<br>**Placeholder** root node (`mini-tree,placeholder` only, no peripherals); boards override via `BOARD_DTS` |
| `board/dtsi/` | **节点模板库**：`example-soc.dtsi`（SoC 骨架：cpus/gpio/uart）+ `vfs/`（11 个 VFS 各一文件）+ `drivers/`（37 个产品驱动各一文件）；参数全 0 占位 + 用法注释，板级拷走填值；`BOARD_DTSI_DIR` 可指向平台自有 dtsi<br>**Node-template library**: `example-soc.dtsi` (SoC skeleton: cpus/gpio/uart) + `vfs/` (one file per VFS) + `drivers/` (one file per product driver); all-0 placeholders with usage comments; `BOARD_DTSI_DIR` may point at platform-owned dtsi |
| `board/dt-bindings/{gpio,spi,uart,tim}/` | 通用 `#define`，供 dtsi `#include <dt-bindings/...>`<br>Generic `#define`s for dtsi `#include <dt-bindings/...>` |
| `drivers/<chip>/{include,src}` | 产品驱动（37 个）；CMake / `dtc-lite` GLOB 扫描<br>Product drivers (37); GLOB-scanned by CMake / `dtc-lite` |

> **模板用法 / How to use the templates**：drivers 模板挂载在 vfs 模板定义的 label 上（如 `&i2c0 { aht20: aht20@0 {...} }`）。板级同时启用总线节点与器件节点（`status = "okay"`）；实例池大小自动 = `DTC_GEN_COUNT_*`（同名 compatible 节点数，缺省 1），无需手调。

### 平台工程（推荐）/ Platform project (recommended)

```text
# ESP 参考：components/board_esp32s3/
dts/board.dts                      # BOARD_DTS 入口
dtsi/<soc>.dtsi                    # SoC / 总线 / 产品片段
dtsi/<soc>-product-drivers.dtsi
…
# HAL 强符号：components/hal_esp32s3/
# 例外驱动：components/driver_ws2812/
```

> 上例：`BOARD_DTS` 入口、SoC/产品 dtsi 片段、HAL 强符号与例外驱动均位于平台工程（ESP 参考）。
> Above: the `BOARD_DTS` entry, SoC/product dtsi fragments, HAL strong symbols, and the exception driver all live in the platform project (ESP reference).

CMake：`BOARD_DTS`、`BOARD_DTSI_DIR`；厂商头搜索：`VENDOR_INC_DIRS` / `VENDOR_DEFINES`。
CMake: `BOARD_DTS`, `BOARD_DTSI_DIR`; vendor header search: `VENDOR_INC_DIRS` / `VENDOR_DEFINES`.

产品驱动与 ESP 接线见 [esp_idf_cmake.md](esp_idf_cmake.md)。
Product drivers and ESP wiring: see [esp_idf_cmake.md](esp_idf_cmake.md).

---

## 2. dtc-lite 流水线 / dtc-lite Pipeline

```bash
python3 tools/dtc-lite.py <board.dts> <out_dir> \
  vfs/spi vfs/uart … drivers/w25qxx …
```

> 上例：`<board.dts>` 为入口，`<out_dir>` 为生成目录，其后为 `DRIVER_REGISTER` 扫描目录列表。
> Above: `<board.dts>` is the entry, `<out_dir>` the output dir, followed by the `DRIVER_REGISTER` scan dirs.

根 `CMakeLists.txt` 已传入本仓库相关 vfs/bus/drivers 目录。
The root `CMakeLists.txt` already passes the repo's relevant vfs/bus/drivers dirs.

| 步骤 / Step | 行为 / Behavior |
| :--- | :--- |
| 预处理 / Preprocess | 处理 `#include`，可对厂商头做 cpp<br>Resolves `#include`, may run cpp on vendor headers |
| 解析 / Parse | Lark 文法 → AST<br>Lark grammar → AST |
| 合并 / Merge | `/ { }`、`&label { }` overlay |
| 扫驱动 / Scan drivers | 在给定源目录找 `DRIVER_REGISTER`<br>Finds `DRIVER_REGISTER` in the given source dirs |
| 生成 / Generate | 见下表<br>See the table below |

| 生成文件 / Generated file | 内容 / Content |
| :--- | :--- |
| `board_nodes.h` | `device_id_t`、`DEV_ID_*`、`DEV_ID_COUNT`、chosen 宏<br>`device_id_t`, `DEV_ID_*`, `DEV_ID_COUNT`, chosen macros |
| `board_devtable.h/.c` | `board_node_get` / `board_dev_find*` / cascade 等 |
| `board_probe.c` | probe/remove 函数表与顺序<br>probe/remove function table and order |
| `dt_config_gen.h` | `DTC_GEN_COUNT_*`、时钟/容量聚合<br>`DTC_GEN_COUNT_*`, clock/capacity aggregation |
| `board_handles.h` | chosen 句柄类宏<br>chosen handle-style macros |

---

## 3. DRIVER_REGISTER

定义于 `board/include/driver.h`：
Defined in `board/include/driver.h`:

```c
DRIVER_REGISTER(name, "compatible-string", probe_fn, remove_fn);
```

展开为：
Expands to:

- `int board_driver_probe_<name>(struct device *dev);`
- `int board_driver_remove_<name>(struct device *dev);`

dtc-lite 把它们收进静态表；**运行期不再 `strcmp` 匹配驱动名**（compatible 在生成期已绑定）。

dtc-lite collects them into a static table; **no runtime `strcmp` matching of driver names** (the compatible is bound at generation time).

规则 / Rules:

- `name`：C 标识符，全局唯一<br>`name`: C identifier, globally unique
- `compatible`：与 DTS 节点 `compatible = "..."` **完全一致**<br>`compatible`: **exactly** matches the DTS node's `compatible = "..."`
- `probe`/`remove`：返回 `VFS_OK` 或 `VFS_ERR_*`<br>`probe`/`remove`: return `VFS_OK` or `VFS_ERR_*`

命名统一小写（`.clang-tidy` 的 `readability-identifier-naming` 强制）：`x_task` / `x_scheduler` / `list_node` / `k_tag` / `struct event` / `mini_tree::` 等；`.clang-format` 为 Allman、单语句去括号、4 空格、100 列。app 层为建议，app 以下为强规定。

Identifiers are uniformly lowercase (enforced by `.clang-tidy` `readability-identifier-naming`): `x_task` / `x_scheduler` / `list_node` / `k_tag` / `struct event` / `mini_tree::`, etc.; `.clang-format` uses Allman braces, no braces on single statements, 4-space indent, 100 columns. Recommended at `app`, mandatory below `app`.

---

## 4. 当前仓库已注册驱动（扫描结果）/ Registered Drivers in This Repo (scan result)

| 区域 / Area | 示例 compatible / 注册名<br>Example compatible / registration |
| :--- | :--- |
| `vfs/spi` | `spi-master` / `spi-slave` / `heterogeneous,spi-*-client` |
| `vfs/uart` | host + client 一对<br>host + client pair |
| `vfs/i2c` · `vfs/i2s` | master/slave + heterogeneous client |
| `vfs/can` | host + client |
| `vfs/usb` | `usb-otg-host`、`heterogeneous,usb-cdc-acm/ecm`、`heterogeneous,usb-hid` |
| `vfs/gpio` · `adc` · `dac` · `tim` · `rtc` · `iwdg` · `wwdg` | 各外设 compatible<br>Per-peripheral compatibles |
| `drivers/<chip>/` | 产品驱动 37 个（GLOB 扫描）；例：`winbond,w25qxx`、`sitronix,st7789`、`solomon,ssd1306`、`modbus,rtu-rs485` …<br>37 product drivers (GLOB-scanned); e.g. `winbond,w25qxx`, `sitronix,st7789`, `solomon,ssd1306`, `modbus,rtu-rs485`, … |
| `board` | `board,safety-hw` |
| 树外 `driver_ws2812` | `worldsemi,ws2812`（唯一厂商 RMT 例外）<br>`worldsemi,ws2812` (the only vendor-RMT exception) |

产品驱动目录约定：`drivers/<chip>/{include,src}`。CMake / dtc-lite 用 `drivers/*/src` 扫描，**勿**再维护逐文件列表。
Product drivers follow `drivers/<chip>/{include,src}`. CMake / dtc-lite scan with `drivers/*/src` — **don't** maintain per-file lists.

以源码中 `DRIVER_REGISTER` 行为准；增删驱动后需重跑 dtc-lite。
The `DRIVER_REGISTER` entries in the source are authoritative; re-run dtc-lite after adding/removing drivers.

**ioctl / 读写语义汇总**见 [peripherals.md](peripherals.md)。
The **ioctl / read-write semantics summary** lives in [peripherals.md](peripherals.md).

ESP 接线细节见 [esp_idf_cmake.md](esp_idf_cmake.md)。
ESP wiring details: see [esp_idf_cmake.md](esp_idf_cmake.md).

---

## 5. compatible 与属性 / Compatible Strings and Properties

### 5.1 命名习惯 / Naming Conventions

| 角色 / Role | 风格 / Style | 例 / Example |
| :--- | :--- | :--- |
| 控制器 host / Controller host | 短名或 `*-master` / `*-host`<br>Short name or `*-master` / `*-host` | `spi-master`、`can-host`、`usb-otg-host` |
| 总线客户端 / Bus client | `heterogeneous,<…>-client` | `heterogeneous,spi-master-client` |
| 板级特殊 / Board special | `board,…` | `board,safety-hw` |

### 5.2 属性直投 / Property Injection

常见键（外设而异）：`reg`、`interrupts`、`status`、`*-base`、`*-clk`、`tx-port`/`rx-pin`/`*-af`、`prescaler`、`dma-*`、`irqn`、`irq-priority`、`it-enable` …

Common keys (per peripheral): `reg`, `interrupts`, `status`, `*-base`, `*-clk`, `tx-port`/`rx-pin`/`*-af`, `prescaler`, `dma-*`, `irqn`, `irq-priority`, `it-enable`, …

值应为 **展开后的整数**（厂商宏），由 VFS probe 填进 `hal_*_config` / `hal_*_bus_config`。
Values should be **expanded integers** (vendor macros), filled into `hal_*_config` / `hal_*_bus_config` by the VFS probe.

### 5.3 status / criticality / deps

- `status = "okay"/"disabled"`（生成进节点默认状态）<br>`status = "okay"/"disabled"` (baked into the node default state)
- criticality：probe 失败时 IGNORE / WARNING / FATAL（见 `device.h`）<br>criticality: IGNORE / WARNING / FATAL on probe failure (see `device.h`)
- `deps`：依赖其它 `device_id_t`，probe 前检查<br>`deps`: dependencies on other `device_id_t`s, checked before probe

---

## 6. 运行期 API / Runtime API

| API | 用途 / Purpose |
| :--- | :--- |
| `device_find` / `_by_label` / `_by_compatible` / `_by_id` | 查找<br>Lookup |
| `device_get_prop_int` / `_str` / `_bool` / `_int_array` | 读属性<br>Read properties |
| `device_get_reg` / `device_get_irq` | 读 reg/irq 表<br>Read reg/irq tables |
| `device_open` / `read` / `write` / `ioctl` / `close` | 持锁 I/O<br>Locked I/O |
| `board_dev_get` / `board_probe_order` | 表访问（生成头）<br>Table access (generated headers) |

---

## 7. Remove 生命周期 / Remove Lifecycle

带 fops 的驱动建议顺序（`driver.h` 注释）：

Drivers with fops should follow this order (`driver.h` comments):

1. `dev_lc_remove_start(device_lc(dev))`
2. `device_ops_unregister(dev)`
3. `dev_lc_remove_drain(..., OSAL_WAIT_FOREVER)`
4. 释放硬件 / bus<br>Release hardware / bus
5. `dev_lc_remove_finish(...)`

Probe 成功路径：`drivers/` 目录产品驱动无需手动调用 `device_lc_bind(dev)`（框架 `device_tree_init` 已统一绑定）；VFS 层驱动（`vfs/`）因池重置需显式再绑定。

On the probe-success path: product drivers under `drivers/` don't call `device_lc_bind(dev)` manually (the framework's `device_tree_init` binds them all); VFS-layer drivers (`vfs/`) need an explicit re-bind after the pool resets.

---

## 相关文档 / Related Docs

- [usb_tusb_port.md](usb_tusb_port.md) · [peripherals.md](peripherals.md) · [amp.md](amp.md)
- [tools/README.md](../tools/README.md)
- [service_spec.md](service_spec.md) · [faq.md](faq.md)
