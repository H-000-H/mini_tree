# 外设一览（compatible · I/O · ioctl）/ Peripheral Reference (compatible · I/O · ioctl)

> 按外设汇总：驱动注册名、DTS `compatible`、读写语义、ioctl 命令。
> **权威定义在各 `vfs-*.h`**；本表便于选型与 Code Review，变更后以源码为准。
>
> Per-peripheral summary: driver registration name, DTS `compatible`, read/write semantics, and ioctl commands.
> **The authoritative definitions live in each `vfs-*.h`**; this table is for selection and Code Review — the source wins on any change.

| 项 / Item | 内容 / Description |
| :--- | :--- |
| **读者 / Audience** | 应用作者、写 dtsi / VFS 的人<br>App authors, dtsi / VFS writers |
| **前置 / Prereq.** | [driver_guide.md](driver_guide.md) · [service_spec.md](service_spec.md) |
| **相关 / Related** | [usb_tusb_port.md](usb_tusb_port.md) · [can_hook.md](can_hook.md) |

> 每个外设的**节点模板**（属性名与全 0 占位）见 `board/dtsi/vfs/` 与 `board/dtsi/drivers/`；下方 compatible 与模板一一对应。
> **Node templates** for every peripheral (property names, all-0 placeholders) live in `board/dtsi/vfs/` and `board/dtsi/drivers/`; the compatibles below map one-to-one.

---

## 目录 / Table of Contents

1. [约定 / Conventions](#1-约定-conventions)
2. [总线类（有 Bus）/ Bus-Type Peripherals (with Bus)](#2-总线类有-bus-bus-type-peripherals-with-bus)
3. [简单外设（无 Bus）/ Simple Peripherals (no Bus)](#3-简单外设无-bus-simple-peripherals-no-bus)
4. [看门狗 / RTC / 定时器 / Watchdog / RTC / Timer](#4-看门狗--rtc--定时器-watchdog--rtc--timer)
5. [示例驱动与安全 / Example Drivers and Safety](#5-示例驱动与安全-example-drivers-and-safety)
6. [HAL-only（无 VFS）/ HAL-Only (no VFS)](#6-hal-only无-vfs-hal-only-no-vfs)

---

## 1. 约定 / Conventions

| 项 / Item | 说明 / Description |
| :--- | :--- |
| 访问入口 / Access | 业务只走 `device_open/read/write/ioctl/close`<br>Business code only uses `device_open/read/write/ioctl/close` |
| ioctl 魔数 / Magic | `COMPAT_MAGIC(XXX)` + 偏移；参数结构在对应 `vfs-*.h`<br>`COMPAT_MAGIC(XXX)` + offset; the argument struct lives in the matching `vfs-*.h` |
| `*_XFER_AUTO/POLL/DMA` | 与 HAL 同值；AUTO = 有 DMA 用 DMA，否则 poll<br>Same values as HAL; AUTO = DMA when available, else poll |
| 属性 / Properties | `reg` / `interrupts` / 时钟 / DMA / 管脚等 **整数直投**，见 [driver_guide.md](driver_guide.md) §5<br>`reg` / `interrupts` / clock / DMA / pins etc. are **injected as integers** — see [driver_guide.md](driver_guide.md) §5 |

---

## 2. 总线类（有 Bus）/ Bus-Type Peripherals (with Bus)

### SPI

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | Host：`spi-master` / `spi-slave`；Client：`heterogeneous,spi-master-client` / `heterogeneous,spi-slave-client`<br>Host: `spi-master` / `spi-slave`; Client: `heterogeneous,spi-master-client` / `heterogeneous,spi-slave-client` |
| **头文件 / Header** | `vfs/spi/vfs-spi.h` |
| **read/write** | 默认同步；模式受 `SPI_XFER_*` 影响<br>Blocking by default; mode follows `SPI_XFER_*` |
| **ioctl** | `SPI_CMD_TRANSFER`、`QUEUE_TX`、`GET_TRANS_RESULT`、`SET/GET_XFER_MODE`、`TRANSFER_ASYNC`、`ASYNC_WAIT` |

### I2C

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `i2c-master` / `i2c-slave`；`heterogeneous,i2c-*-client` |
| **头文件 / Header** | `vfs/i2c/vfs-i2c.h` |
| **ioctl** | `I2C_CMD_TRANSFER`、`QUEUE_TX`、`GET_TRANS_RESULT`、`SET/GET_XFER_MODE` |

### I2S

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `i2s-master` / `i2s-slave`；`heterogeneous,i2s-*-client` |
| **头文件 / Header** | `vfs/i2s/vfs-i2s.h` |
| **ioctl** | `TRANSFER` / `SET/GET_XFER_MODE` / `TRANSFER_ASYNC` / `ASYNC_WAIT` / `CIRC_*` / `SET/GET_DMA_IRQ_MODE` |
| **注意 / Note** | 环形缓冲 + SPSC；部分 async 为占位，以实现为准<br>Ring buffer + SPSC; some async paths are placeholders — trust the implementation |

### UART

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | host：`uart`；client：`uart-client`（以 `DRIVER_REGISTER` 为准）<br>host: `uart`; client: `uart-client` (per `DRIVER_REGISTER`) |
| **头文件 / Header** | `vfs/uart/vfs-uart.h` |
| **ioctl** | `UART_CMD_TRANSFER`（参数 `uart_transfer_arg`）<br>`UART_CMD_TRANSFER` (arg: `uart_transfer_arg`) |

### CAN

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `can-host`；`heterogeneous,can-client` |
| **头文件 / Header** | `vfs/can/vfs-can.h` |
| **read/write** | `struct can_frame` |
| **ioctl** | `CAN_CMD_TRANSFER`、`SET_FILTER`、`GET_STATE` |
| **钩子 / Hooks** | 开闭读写经 [can_hook.md](can_hook.md)（默认可透传）<br>Open/close/read/write go through [can_hook.md](can_hook.md) (transparent pass-through by default) |

### USB

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `usb-otg-host`；`heterogeneous,usb-cdc-acm` / `usb-cdc-ecm` / `usb-hid` |
| **头文件 / Header** | `vfs/usb/vfs-usb.h` |
| **ioctl** | `USB_CMD_SET_XFER_MODE`、`GET_XFER_MODE` |
| **板级 / Board** | 必须实现 [usb_tusb_port.md](usb_tusb_port.md)<br>Must implement [usb_tusb_port.md](usb_tusb_port.md) |

---

## 3. 简单外设（无 Bus）/ Simple Peripherals (no Bus)

### GPIO

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `heterogeneous,gpios` |
| **头文件 / Header** | `vfs/gpio/vfs-gpio.h` |
| **ioctl** | `GPIO_CMD_TOGGLE`、`SET_LEVEL`、`GET_LEVEL` |
| **快路径 / Fast path** | 热路径优先 `hal_gpio_fast_*`（见 [fast_path.md](fast_path.md)）<br>Hot paths prefer `hal_gpio_fast_*` (see [fast_path.md](fast_path.md)) |

### ADC

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `adc` |
| **头文件 / Header** | `vfs/adc/vfs-adc.h` |
| **ioctl** | `GET_CHANNEL_SAMPLE_TIME` / `GET_CHANNEL_ID` / `GET_CHANNEL_COUNT` / `POLL_FOR_CONVERSION` / `CLOSE_CHANNEL` / `READ_VALUE` |

### DAC

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `dac` |
| **头文件 / Header** | `vfs/dac/vfs-dac.h` |
| **ioctl** | `WRITE_VALUE`、`GET_VALUE`、`CALIBRATE_OFFSET`、`DMA_PAUSE`、`START`、`FORCE_STOP`、`DMA_WRITE_BUFFER`、`BASE_PAUSE` |

---

## 4. 看门狗 / RTC / 定时器 / Watchdog / RTC / Timer

### IWDG

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `iwdg` |
| **ioctl** | `FEED`、`SET_TIMEOUT`、`SET_LONG`、`RESTORE` |
| **行为 / Behavior** | 首次 `open` 常启动硬件；超时属性多来自 DTS `timeout-ms`<br>First `open` usually starts the hardware; the timeout often comes from DTS `timeout-ms` |

### WWDG

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `wwdg` |
| **ioctl** | `WWDG_CMD_FEED`（窗口内喂狗）<br>`WWDG_CMD_FEED` (feed inside the window) |

### RTC

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `rtc` |
| **ioctl** | `SET/GET_TIME`、`SET/CANCEL_ALARM`、`SET/CANCEL_WAKEUP`、`FORCE_STOP` |

### TIM

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **compatible** | `tim` |
| **ioctl** | `START/STOP/PAUSE/RESUME`、计数器、PWM、捕获、编码器/霍尔、ARR/PSC/分频/模式、中断配置等（完整列表见 `vfs-tim.h`，约 23 个命令）<br>`START/STOP/PAUSE/RESUME`, counters, PWM, capture, encoder/Hall, ARR/PSC/divider/mode, interrupt config, etc. (full list in `vfs-tim.h`, ~23 commands) |

---

## 5. 示例驱动与安全 / Example Drivers and Safety

产品驱动共 37 个，均位于 `drivers/<chip>/{include,src}`，经 `DRIVER_REGISTER` + dtc-lite 编译期 probe；下表为典型示例：

There are 37 product drivers, all under `drivers/<chip>/{include,src}`, probed at compile time via `DRIVER_REGISTER` + dtc-lite; typical examples:

| 驱动 / Driver | compatible | 说明 / Description |
| :--- | :--- | :--- |
| W25Qxx SPI Flash | `winbond,w25qxx` | `drivers/w25qxx/`；挂在 SPI client 下<br>`drivers/w25qxx/`; attaches under an SPI client |
| RS485 Modbus RTU | `modbus,rtu-rs485` | `drivers/rs485_modbus/`；挂在 UART client 下；ioctl：`READ_HOLDING`（03 读保持寄存器）、`WRITE_SINGLE`（06 写单寄存器）；响应含 CRC16 校验<br>`drivers/rs485_modbus/`; attaches under a UART client; ioctls: `READ_HOLDING` (03 read holding registers), `WRITE_SINGLE` (06 write single register); responses carry CRC16 |
| 板级 safety / Board safety | `board,safety-hw` | probe 期注册 shutdown；配合 `safe_state` / `hal_platform_safety`<br>Registers shutdown during probe; pairs with `safe_state` / `hal_platform_safety` |

---

## 6. HAL-only（无 VFS）/ HAL-Only (no VFS)

| 模块 / Module | 头 / Header | 用途 / Purpose |
| :--- | :--- | :--- |
| AMP / CPU | `hal/amp/hal_amp.h`（`hal_cpu_*` API；历史名 `HAL_CPU`） | 紧急停核、从核启动、ISR 检测、NVIC；见 [amp.md](amp.md)<br>Emergency core stop, secondary-core startup, ISR detection, NVIC; see [amp.md](amp.md) |
| Storage | `hal/storage/hal_storage.h` | 配置双槽 A/B、扇区 ioctl；供 `config_store` 等<br>Config dual-slot A/B, sector ioctl; used by `config_store` and others |
| Platform safety | `hal/system/hal_platform_safety.h` | 进安全态时关输出等<br>Shuts outputs down etc. when entering the safe state |
| SDIO（系统目录 / system dir） | `hal/system/hal_sdio.h` | 平台按需实现<br>Implemented by the platform as needed |

---

## 相关文档 / Related Docs

- [driver_guide.md](driver_guide.md) · [service_spec.md](service_spec.md) · [porting_guide.md](porting_guide.md)
- [usb_tusb_port.md](usb_tusb_port.md) · [can_hook.md](can_hook.md) · [amp.md](amp.md)
