# 设备树与驱动

> DTS 布局、dtc-lite 流水线、`DRIVER_REGISTER`、compatible 与属性契约。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 写平台 dtsi / 写 VFS 驱动的人 |
| **前置** | [getting_started.md](getting_started.md) · [porting_guide.md](porting_guide.md) |
| **相关** | [README.md](../tools/README.md) · [architecture.md](architecture.md) |

---

## 目录

1. [文件布局](#1-文件布局)
2. [dtc-lite 流水线](#2-dtc-lite-流水线)
3. [DRIVER_REGISTER](#3-driver_register)
4. [当前仓库已注册驱动（扫描结果）](#4-当前仓库已注册驱动扫描结果)
5. [compatible 与属性](#5-compatible-与属性)
6. [运行期 API](#6-运行期-api)
7. [Remove 生命周期](#7-remove-生命周期)

---

## 1. 文件布局

### 中间件（本仓库）

| 路径 | 说明 |
| :--- | :--- |
| `board/dts/board.dts` | **占位**根节点，保证无板级覆盖时也能跑 dtc-lite |
| `board/dtsi/` | 空目录；留给平台 |
| `board/dt-bindings/{gpio,spi,uart,tim}/` | 通用 `#define`，供 dtsi `#include <dt-bindings/...>` |

### 平台工程（推荐）

```text
board/dts/<board>.dts              # 入口：#include 头、/ { }、&label 覆盖、status
board/dtsi/<soc>.dtsi              # SoC：cpus / soc simple-bus
board/dtsi/<soc>-gpio.dtsi         # 外设片段
board/dtsi/<soc>-spi.dtsi
…
```

CMake：`BOARD_DTS`、`BOARD_DTSI_DIR`；厂商头搜索：`VENDOR_INC_DIRS` / `VENDOR_DEFINES`。

---

## 2. dtc-lite 流水线

```bash
python3 tools/dtc-lite.py <board.dts> <out_dir> \
  vfs/spi vfs/uart … drivers/flash …
```

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
| `drivers/flash` | W25Q64 |
| `board` | `board,safety-hw` |

以源码中 `DRIVER_REGISTER` 行为准；增删驱动后需重跑 dtc-lite。  
**ioctl / 读写语义汇总**见 [peripherals.md](peripherals.md)。

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

Probe 成功路径记得 `device_lc_bind(dev)`。

---

## 相关文档

- [usb_tusb_port.md](usb_tusb_port.md) · [peripherals.md](peripherals.md) · [amp.md](amp.md)  
- [README.md](../tools/README.md)  
- [service_spec.md](service_spec.md) · [faq.md](faq.md)
