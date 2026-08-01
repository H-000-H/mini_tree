# 外设一览（compatible · I/O · ioctl）

> 按外设汇总：驱动注册名、DTS `compatible`、读写语义、ioctl 命令。  
> **权威定义在各 `vfs-*.h`**；本表便于选型与 Code Review，变更后以源码为准。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 应用作者、写 dtsi / VFS 的人 |
| **前置** | [driver_guide.md](driver_guide.md) · [service_spec.md](service_spec.md) |
| **相关** | [usb_tusb_port.md](usb_tusb_port.md) · [can_hook.md](can_hook.md) |

---

## 目录

1. [约定](#1-约定)
2. [总线类（有 Bus）](#2-总线类有-bus)
3. [简单外设（无 Bus）](#3-简单外设无-bus)
4. [看门狗 / RTC / 定时器](#4-看门狗--rtc--定时器)
5. [示例驱动与安全](#5-示例驱动与安全)
6. [HAL-only（无 VFS）](#6-hal-only无-vfs)

---

## 1. 约定

| 项 | 说明 |
| :--- | :--- |
| 访问入口 | 业务只走 `device_open/read/write/ioctl/close` |
| ioctl 魔数 | `COMPAT_MAGIC(XXX)` + 偏移；参数结构在对应 `vfs-*.h` |
| `*_XFER_AUTO/POLL/DMA` | 与 HAL 同值；AUTO = 有 DMA 用 DMA，否则 poll |
| 属性 | `reg` / `interrupts` / 时钟 / DMA / 管脚等 **整数直投**，见 [driver_guide.md](driver_guide.md) §5 |

---

## 2. 总线类（有 Bus）

### SPI

| | |
| :--- | :--- |
| **compatible** | Host：`spi-master` / `spi-slave`；Client：`heterogeneous,spi-master-client` / `heterogeneous,spi-slave-client` |
| **头文件** | `vfs/spi/vfs-spi.h` |
| **read/write** | 默认同步；模式受 `SPI_XFER_*` 影响 |
| **ioctl** | `SPI_CMD_TRANSFER`、`QUEUE_TX`、`GET_TRANS_RESULT`、`SET/GET_XFER_MODE`、`TRANSFER_ASYNC`、`ASYNC_WAIT` |

### I2C

| | |
| :--- | :--- |
| **compatible** | `i2c-master` / `i2c-slave`；`heterogeneous,i2c-*-client` |
| **头文件** | `vfs/i2c/vfs-i2c.h` |
| **ioctl** | `I2C_CMD_TRANSFER`、`QUEUE_TX`、`GET_TRANS_RESULT`、`SET/GET_XFER_MODE` |

### I2S

| | |
| :--- | :--- |
| **compatible** | `i2s-master` / `i2s-slave`；`heterogeneous,i2s-*-client` |
| **头文件** | `vfs/i2s/vfs-i2s.h` |
| **ioctl** | `TRANSFER` / `SET/GET_XFER_MODE` / `TRANSFER_ASYNC` / `ASYNC_WAIT` / `CIRC_*` / `SET/GET_DMA_IRQ_MODE` |
| **注意** | 环形缓冲 + SPSC；部分 async 为占位，以实现为准 |

### UART

| | |
| :--- | :--- |
| **compatible** | host：`uart`；client：`uart-client`（以 `DRIVER_REGISTER` 为准） |
| **头文件** | `vfs/uart/vfs-uart.h` |
| **ioctl** | `UART_CMD_TRANSFER`（参数 `uart_transfer_arg`） |

### CAN

| | |
| :--- | :--- |
| **compatible** | `can-host`；`heterogeneous,can-client` |
| **头文件** | `vfs/can/vfs-can.h` |
| **read/write** | `struct can_frame` |
| **ioctl** | `CAN_CMD_TRANSFER`、`SET_FILTER`、`GET_STATE` |
| **钩子** | 开闭读写经 [can_hook.md](can_hook.md)（默认可透传） |

### USB

| | |
| :--- | :--- |
| **compatible** | `usb-otg-host`；`heterogeneous,usb-cdc-acm` / `usb-cdc-ecm` / `usb-hid` |
| **头文件** | `vfs/usb/vfs-usb.h` |
| **ioctl** | `USB_CMD_SET_XFER_MODE`、`GET_XFER_MODE` |
| **板级** | 必须实现 [usb_tusb_port.md](usb_tusb_port.md) |

---

## 3. 简单外设（无 Bus）

### GPIO

| | |
| :--- | :--- |
| **compatible** | `heterogeneous,gpios` |
| **头文件** | `vfs/gpio/vfs-gpio.h` |
| **ioctl** | `GPIO_CMD_TOGGLE`、`SET_LEVEL`、`GET_LEVEL` |
| **快路径** | 热路径优先 `hal_gpio_fast_*`（见 [fast_path.md](fast_path.md)） |

### ADC

| | |
| :--- | :--- |
| **compatible** | `adc` |
| **头文件** | `vfs/adc/vfs-adc.h` |
| **ioctl** | `GET_CHANNEL_SAMPLE_TIME` / `GET_CHANNEL_ID` / `GET_CHANNEL_COUNT` / `POLL_FOR_CONVERSION` / `CLOSE_CHANNEL` / `READ_VALUE` |

### DAC

| | |
| :--- | :--- |
| **compatible** | `dac` |
| **头文件** | `vfs/dac/vfs-dac.h` |
| **ioctl** | `WRITE_VALUE`、`GET_VALUE`、`CALIBRATE_OFFSET`、`DMA_PAUSE`、`START`、`FORCE_STOP`、`DMA_WRITE_BUFFER`、`BASE_PAUSE` |

---

## 4. 看门狗 / RTC / 定时器

### IWDG

| | |
| :--- | :--- |
| **compatible** | `iwdg` |
| **ioctl** | `FEED`、`SET_TIMEOUT`、`SET_LONG`、`RESTORE` |
| **行为** | 首次 `open` 常启动硬件；超时属性多来自 DTS `timeout-ms` |

### WWDG

| | |
| :--- | :--- |
| **compatible** | `wwdg` |
| **ioctl** | `WWDG_CMD_FEED`（窗口内喂狗） |

### RTC

| | |
| :--- | :--- |
| **compatible** | `rtc` |
| **ioctl** | `SET/GET_TIME`、`SET/CANCEL_ALARM`、`SET/CANCEL_WAKEUP`、`FORCE_STOP` |

### TIM

| | |
| :--- | :--- |
| **compatible** | `tim` |
| **ioctl** | `START/STOP/PAUSE/RESUME`、计数器、PWM、捕获、编码器/霍尔、ARR/PSC/分频/模式、中断配置等（完整列表见 `vfs-tim.h`，约 23 个命令） |

---

## 5. 示例驱动与安全

| 驱动 | compatible | 说明 |
| :--- | :--- | :--- |
| W25Qxx SPI Flash | `winbond,w25qxx` | `drivers/w25qxx/`；挂在 SPI client 下 |
| RS485 Modbus RTU | `modbus,rtu-rs485` | `drivers/rs485_modbus/`；挂在 UART client 下；ioctl：`READ_HOLDING`（03 读保持寄存器）、`WRITE_SINGLE`（06 写单寄存器）；响应含 CRC16 校验 |
| 板级 safety | `board,safety-hw` | probe 期注册 shutdown；配合 `safe_state` / `hal_platform_safety` |

---

## 6. HAL-only（无 VFS）

| 模块 | 头 | 用途 |
| :--- | :--- | :--- |
| AMP / CPU | `hal/amp/hal_amp.h`（历史名 `HAL_CPU`） | 紧急停核、从核启动、ISR 检测、NVIC；见 [amp.md](amp.md) |
| Storage | `hal/storage/hal_storage.h` | 配置双槽 A/B、扇区 ioctl；供 `config_store` 等 |
| Platform safety | `hal/system/hal_platform_safety.h` | 进安全态时关输出等 |
| SDIO（系统目录） | `hal/system/hal_sdio.h` | 平台按需实现 |

---

## 相关文档

- [driver_guide.md](driver_guide.md) · [service_spec.md](service_spec.md) · [porting_guide.md](porting_guide.md)  
- [usb_tusb_port.md](usb_tusb_port.md) · [can_hook.md](can_hook.md) · [amp.md](amp.md)
