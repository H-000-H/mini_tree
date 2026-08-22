# 设备树与驱动

> DTS 布局、dtc-lite 流水线、`DRIVER_REGISTER`、compatible 与属性契约。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 写平台 dtsi / 写 VFS 驱动的人 |
| **前置** | [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) |
| **相关** | [tools_guide.md](../tools_guide.md) · [architecture.md](architecture.md) |

---

## 目录

- [设备树与驱动](#设备树与驱动)
  - [目录](#目录)
  - [1. 文件布局](#1-文件布局)
    - [中间件（本仓库）](#中间件本仓库)
    - [平台工程（推荐）](#平台工程推荐)
  - [2. dtc-lite 流水线](#2-dtc-lite-流水线)
  - [3. DRIVER_REGISTER](#3-driver_register)
  - [4. 当前仓库已注册驱动（扫描结果）](#4-当前仓库已注册驱动扫描结果)
  - [5. compatible 与属性](#5-compatible-与属性)
    - [5.1 命名习惯](#51-命名习惯)
    - [5.2 属性直投](#52-属性直投)
    - [5.3 status / criticality / deps](#53-status-criticality-deps)
  - [6. 运行期 API](#6-运行期-api)
  - [7. Remove 生命周期](#7-remove-生命周期)
  - [相关文档](#相关文档)

---

## 1. 文件布局

### 中间件（本仓库）

| 路径 | 说明 |
| :--- | :--- |
| `board/dts/board.dts` | **占位**根节点（仅 `mini-tree,placeholder`，无外设）；板级用 `BOARD_DTS` 覆盖 |
| `board/dtsi/` | **节点模板库**：`example-soc.dtsi`（SoC 骨架：cpus/gpio/uart）+ `vfs/`（11 个 VFS 各一文件）+ `drivers/`（37 个产品驱动各一文件）；参数全 0 占位 + 用法注释，板级拷走填值；`BOARD_DTSI_DIR` 可指向平台自有 dtsi |
| `board/dt-bindings/{gpio,spi,uart,tim}/` | 通用 `#define`，供 dtsi `#include <dt-bindings/...>` |
| `drivers/<chip>/{include,src}` | 产品驱动（37 个）；CMake / `dtc-lite` GLOB 扫描 |

> **模板用法**：drivers 模板挂载在 vfs 模板定义的 label 上（如 `&i2c0 { aht20: aht20@0 {...} }`）。板级同时启用总线节点与器件节点（`status = "okay"`）；实例池大小自动 = `DTC_GEN_COUNT_*`（同名 compatible 节点数，缺省 1），无需手调。

### 平台工程（推荐）

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

CMake：`BOARD_DTS`、`BOARD_DTSI_DIR`；厂商头搜索：`VENDOR_INC_DIRS` / `VENDOR_DEFINES`。

产品驱动与 ESP 接线见 [esp_idf_cmake.md](esp_idf_cmake.md)（在 **`esp` 分支**上）。

---

## 2. dtc-lite 流水线

```bash
python3 tools/dtc-lite.py <board.dts> <out_dir> \
  vfs/spi vfs/uart … drivers/w25qxx … \
  -I <vendor_include_dir> … -D <NAME[=VALUE]> …
```

> 上例：`<board.dts>` 为入口，`<out_dir>` 为生成目录，其后为 `DRIVER_REGISTER` 扫描目录列表；`-I` 追加厂商头搜索目录、`-D` 追加预处理宏（对应根 `CMakeLists.txt` 的 `VENDOR_INC_DIRS` / `VENDOR_DEFINES`）。

根 `CMakeLists.txt` 已传入本仓库相关 vfs/bus/drivers 目录。

| 步骤 | 行为 |
| :--- | :--- |
| 预处理 | 处理 `#include`，可对厂商头做 cpp |
| 解析 | Lark 文法 → AST |
| 合并 | `/ { }`、`&label { }` overlay |
| 扫驱动 | 在给定源目录找 `DRIVER_REGISTER` |
| 生成 | 见下表 |

| 生成文件 | 内容 |
| :--- | :--- |
| `board_nodes.h` | `device_id_t`、`DEV_ID_*`、`DEV_ID_COUNT`、chosen 宏 |
| `board_devtable.h/.c` | `board_node_get` / `board_dev_find*` / cascade 等 |
| `board_probe.c` | probe/remove 函数表与顺序 |
| `dt_config_gen.h` | `DTC_GEN_COUNT_*`、时钟/容量聚合 |
| `board_handles.h` | chosen 句柄类宏 |

---

## 3. DRIVER_REGISTER

定义于 `board/include/driver.h`：

```c
DRIVER_REGISTER(name, "compatible-string", probe_fn, remove_fn);
```

展开为：

- `int board_driver_probe_<name>(struct device *dev);`
- `int board_driver_remove_<name>(struct device *dev);`

dtc-lite 把它们收进静态表；**运行期不再 `strcmp` 匹配驱动名**（compatible 在生成期已绑定）。

规则：

- `name`：C 标识符，全局唯一
- `compatible`：与 DTS 节点 `compatible = "..."` **完全一致**
- `probe`/`remove`：返回 `VFS_OK` 或 `VFS_ERR_*`

命名统一小写（`.clang-tidy` 的 `readability-identifier-naming` 强制）：`x_task` / `x_scheduler` / `list_node` / `k_tag` / `struct event` / `mini_tree::` 等；`.clang-format` 为 Allman、单语句去括号、4 空格、200 列。app 层为建议，app 以下为强规定。

---

## 4. 当前仓库已注册驱动（扫描结果）

| 区域 | 示例 compatible / 注册名 |
| :--- | :--- |
| `vfs/spi` | `spi-master` / `spi-slave` / `heterogeneous,spi-*-client` |
| `vfs/uart` | host + client 一对 |
| `vfs/i2c` · `vfs/i2s` | master/slave + heterogeneous client |
| `vfs/can` | host + client |
| `vfs/usb` | `usb-otg-host`、`heterogeneous,usb-cdc-acm/ecm`、`heterogeneous,usb-hid` |
| `vfs/gpio` · `adc` · `dac` · `tim` · `rtc` · `iwdg` · `wwdg` | 各外设 compatible |
| `drivers/<chip>/` | 产品驱动 37 个（GLOB 扫描）；例：`winbond,w25qxx`、`sitronix,st7789`、`solomon,ssd1306`、`modbus,rtu-rs485` … |
| `board` | `board,safety-hw` |
| 树外 `driver_ws2812` | `worldsemi,ws2812`（唯一厂商 RMT 例外） |

产品驱动目录约定：`drivers/<chip>/{include,src}`。CMake / dtc-lite 用 `drivers/*/src` 扫描，**勿**再维护逐文件列表。

以源码中 `DRIVER_REGISTER` 行为准；增删驱动后需重跑 dtc-lite。

**ioctl / 读写语义汇总**见 [peripherals.md](peripherals.md)。

ESP 接线细节见 [esp_idf_cmake.md](esp_idf_cmake.md)（在 **`esp` 分支**上）。

---

## 5. compatible 与属性

### 5.1 命名习惯

| 角色 | 风格 | 例 |
| :--- | :--- | :--- |
| 控制器 host | 短名或 `*-master` / `*-host` | `spi-master`、`can-host`、`usb-otg-host` |
| 总线客户端 | `heterogeneous,<…>-client` | `heterogeneous,spi-master-client` |
| 板级特殊 | `board,…` | `board,safety-hw` |

### 5.2 属性直投

常见键（外设而异）：`reg`、`interrupts`、`status`、`*-base`、`*-clk`、`tx-port`/`rx-pin`/`*-af`、`prescaler`、`dma-*`、`irqn`、`irq-priority`、`it-enable` …

值应为 **展开后的整数**（厂商宏），由 VFS probe 填进 `hal_*_config` / `hal_*_bus_config`。

### 5.3 status / criticality / deps

- `status = "okay"/"disabled"`（生成进节点默认状态）
- criticality：probe 失败时 IGNORE / WARNING / FATAL（见 `device.h`）
- `deps`：依赖其它 `device_id_t`，probe 前检查

---

## 6. 运行期 API

| API | 用途 |
| :--- | :--- |
| `device_find` / `_by_label` / `_by_compatible` / `_by_id` | 查找 |
| `device_get_prop_int` / `_str` / `_bool` / `_int_array` | 读属性 |
| `device_get_reg` / `device_get_irq` | 读 reg/irq 表 |
| `device_open` / `read` / `write` / `ioctl` / `close` | 持锁 I/O |
| `board_dev_get` / `board_probe_order` | 表访问（生成头） |

---

## 7. Remove 生命周期

带 fops 的驱动建议顺序（`driver.h` 注释）：

1. `dev_lc_remove_start(device_lc(dev))`
2. `device_ops_unregister(dev)`
3. `dev_lc_remove_drain(..., OSAL_WAIT_FOREVER)`
4. 释放硬件 / bus
5. `dev_lc_remove_finish(...)`

Probe 成功路径：`drivers/` 目录产品驱动无需手动调用 `device_lc_bind(dev)`（框架 `device_tree_init` 已统一绑定）；VFS 层驱动（`vfs/`）因池重置需显式再绑定。

---

## 相关文档

- [usb_tusb_port.md](usb_tusb_port.md) · [peripherals.md](peripherals.md) · [amp.md](amp.md)
- [tools_guide.md](../tools_guide.md)
- [service_spec.md](service_spec.md) · [faq.md](faq.md)
