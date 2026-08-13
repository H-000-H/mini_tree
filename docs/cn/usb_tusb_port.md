# USB (TinyUSB) 端口指南

> 把 TinyUSB（`lib/tinyusb`，Fetch 积木）接进中间件的板级端口步骤。涉及：`vfs/usb`、设备树绑定、`dtsi` 节点。核心约定见 [peripherals.md](peripherals.md)、[device_tree_porting.md](device_tree_porting.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 接 USB / 板级端口 |
| **相关** | [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) |

---

## 1. 前置条件

- TinyUSB 走 **ESP-IDF 组件**（`esp_tinyusb` / registry，声明于 `idf_component.yml`）；不在 `lib/` vendor 内。
- 板级 `dtsi/` 提供 USB 控制器节点（参考 `board/dtsi/example-soc.dtsi`）。
- `CONFIG_OSAL_*` 后端已选（USB 中断需 OSAL 中断封装）。

---

## 2. 板级端口步骤

1. 板级 `dtsi/` 加 USB 控制器节点（`compatible = "mini-tree,usb"`，含中断号 / 端点数）。
2. 写 `drivers/<chip>/` 产品驱动（`DRIVER_REGISTER` + dtc-lite 探针），实现 `hal/usb` 回调。
3. `vfs/usb/vfs-usb.{c,h}` 经 `vfs/usb` 暴露设备/主机接口。
4. 平台 CMake 注入 `BOARD_DTSI_DIR` 指向板级 dtsi。
5. 跑 `dtc-lite` 验证探针命中。

---

## 3. 设备树绑定

| 字段 | 说明 |
| :--- | :--- |
| `compatible` | `"mini-tree,usb"` |
| `interrupts` | USB 中断号（经 VIRQ 封装） |
| `num-endpoints` | 端点数量 |
| `maximum-speed` | `high` / `full` / `low` |

> 绑定宏放 `dt-bindings/usb.h`（中间件通用）。

---

## 4. 中断与 OSAL

USB 中断经 `interrupt/interrupt.{c,h}` 的 VIRQ 封装后转 OSAL 中断；裸机（`CONFIG_OSAL_NULL`）下由 `time_slice` 调度处理。详见 [osal_switching.md](osal_switching.md)。

---

## 5. 验证

1. `dtc-lite` 探针命中（`drivers/<chip>/` USB 驱动注册）。
2. 编 `mini_tree` 含 `vfs/usb`，无未定义符号。
3. 接平台链接脚本，确认 USB 描述符段放置。
4. 实测枚举（设备模式）或连接（主机模式）。

---

## 相关文档

- [peripherals.md](peripherals.md) · [device_tree_porting.md](device_tree_porting.md) · [ecosystem.md](ecosystem.md) · [osal_switching.md](osal_switching.md)
