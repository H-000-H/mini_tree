# 外设与设备矩阵

> 中间件提供的可用硬件抽象层（HAL / VFS / bus）清单，与 `board/` 设备模型、dtc-lite 编译期探针的关系。新增外设需配套 HAL + 设备树绑定，详见 [driver_guide.md](driver_guide.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 选外设 / 写驱动 |
| **相关** | [architecture.md](architecture.md)（§2.1 覆盖）· [driver_guide.md](driver_guide.md) |

---

## 1. 设备树绑定与编译期探针

所有外设经 dtc-lite 在编译期用设备树节点匹配，框架 `board_driver_probe_all` 仅在设备树存在对应 `compatible` 时注册驱动。无设备树节点 = 不编对应驱动。详见 [driver_guide.md](driver_guide.md) §2。

| 类别 | 绑定路径 `dt-bindings/` | 说明 |
| :--- | :--- | :--- |
| GPIO | `dt-bindings/gpio.h` | 引脚号 / 极性的通用宏 |
| SPI | `dt-bindings/spi.h` | 帧格式 / 时钟极性的通用宏 |
| UART | `dt-bindings/uart.h` | 波特率 / 流控的通用宏 |
| TIM | `dt-bindings/tim.h` | 定时器分频 / 通道的通用宏 |

---

## 2. HAL 矩阵

| 外设 | HAL 路径 | 后端 / 依赖 |
| :--- | :--- | :--- |
| GPIO | `hal/gpio/hal_gpio.{h,c}` | `hal/amp`（多核时） |
| UART | `hal/uart/hal_uart.{h,c}` | 依赖 `bus/uart` |
| SPI | `hal/spi/hal_spi.{h,c}` | 依赖 `bus/spi` |
| I2C | `hal/i2c/hal_i2c.{h,c}` | 依赖 `bus/i2c` |
| TIM | `hal/tim/hal_tim.{h,c}` | 通用定时 |
| ADC | `hal/adc/hal_adc.{h,c}` | 经 `vfs/adc` 暴露 |
| WDT | `hal/wdt/hal_wdt.{h,c}` | `system` 的 WDT 组 |
| Storage | `hal/storage/hal_storage.{h,c}` | `hal/amp` / `hal/flash` 协作 |
| AMP | `hal/amp/hal_amp.{h,c}` | 多核消息 / 启动 |

> 所有 HAL 提供 weak 空实现（`hal/hal_if_dummy.c`），无板级即可编过。

---

## 3. VFS 矩阵

| 外设 | VFS 路径 | 依赖 |
| :--- | :--- | :--- |
| UART 设备 | `vfs/uart/vfs-uart.{c,h}` | `hal/uart` + `bus/uart` |
| ADC 设备 | `vfs/adc/vfs-adc.c` | `hal/adc` |
| SPI 从设备 | `vfs/spi/vfs-spi.{c,h}` | `hal/spi` + `bus/spi` |
| I2C 从设备 | `vfs/i2c/vfs-i2c.{c,h}` | `hal/i2c` + `bus/i2c` |
| USB 设备/主机 | `vfs/usb/vfs-usb.{c,h}` | `lib/tinyusb`（Fetch 积木） |

> VFS 层驱动（`vfs/`）因池重置需显式再绑定（见 [driver_guide.md](driver_guide.md) §7）。

---

## 4. Bus 矩阵

| 总线 | Bus 路径 | 控制器抽象 |
| :--- | :--- | :--- |
| UART | `bus/uart/uart_bus.{c,h}` | 控制器表 + 端口注册 |
| SPI | `bus/spi/spi_bus.{c,h}` | 控制器表 + 片选 |
| I2C | `bus/i2c/i2c_bus.{c,h}` | 控制器表 + 地址 |

---

## 5. 添加新外设

1. 加 `dt-bindings/<name>.h` 参数宏（中间件通用）。
2. 写 `hal/<name>/hal_<name>.{h,c}`（含 weak 空实现）。
3. 写 `vfs/<name>/vfs-<name>.{c,h}`（如经 VFS 暴露）。
4. 写 `drivers/<chip>/` 产品驱动（`DRIVER_REGISTER` + dtc-lite 探针）。
5. 板级 `dtsi/` 加节点（或平台设 `BOARD_DTSI_DIR`）。
6. 跑 `dtc-lite` 验证编译期探针命中。

详见 [driver_guide.md](driver_guide.md)。

---

## 相关文档

- [architecture.md](architecture.md) · [driver_guide.md](driver_guide.md) · [file_index.md](file_index.md)
