# SPI 子系统架构

**仅 Master 模式**：从设备树 probe 到 HAL 轮询/DMA 传输的分层说明。本框架以 ARM Cortex-M 与 RISC-V RV32 为通用基准，具体 HAL 实现由项目通过 `HAL_SRCS` 变量提供。

---

## 1. 分层概览

```
设备树 (board.dtsi + board *.dts)
        ↓ dtc-lite → board_probe.c
bus/spi/spi_bus.c          — Host probe、Client 注册、总线锁、CS 控制、选 poll/DMA
        ↓ spi_hal_*()
hal/spi/spi_hal_<chip>.c   — 寄存器配置与传输（具体项目提供）
        ↓ SPI 外设 + DMA
硬件

────────────────── 上层 ──────────────────

w25q64_spi_drv (drivers/flash)
        ↓ spi_vfs_transfer
vfs/spi/spi_vfs.c          — VFS client（heterogeneous,w25q64-master）
        ↓ spi_bus_transfer()
bus/spi/spi_bus.c
```

**职责划分**

| 层 | 做什么 | 不做什么 |
|----|--------|----------|
| **DTS** | host 引脚、hw-instance、DMA phandle、子设备 CS/mode/frequency | 不含 C 逻辑 |
| **bus/spi** | 解析 DTS、`bus_controller` / `bus_client`、持锁、软件 CS | 不直接写 SPI 寄存器 |
| **hal/spi** | 波特率/mode 配置、poll/DMA 传输 | 不管 VFS / 锁 / CS |
| **vfs/spi** | `file_operations` + `dev_lifecycle`、`SPI_CMD_TRANSFER` | 不解析 Flash 命令 |
| **drivers/flash** | W25Q64 JEDEC / 页编程 / 擦除 | 不碰引脚映射 |

**外设初始化**：SPI 时钟与 GPIO 由**厂商板级初始化**（如 STM32 CubeMX `MX_SPIx_Init()`、CH32 `SPI_Init()`、ESP-IDF `spi_bus_initialize()`）在板级 `pre_execution` 完成；mini_tree 只配置运行时 mode/分频与传输，不重复开 RCC。

> 具体芯片的 HAL 实现（如 `hal/spi/spi_hal_stm32.c`、`hal/spi/spi_hal_ch32.c`、`hal/spi/spi_hal_esp32.c`）由项目通过 `HAL_SRCS` 变量传入，框架层只依赖 `hal/spi/spi_hal.h` 声明的统一 API。

---

## 2. 目录与文件

| 路径 | 文件 | 作用 |
|------|------|------|
| `board/dtsi/` | `<soc>-spi.dtsi` | SPI host + W25Q64 子节点模板（由具体项目提供） |
| `board/dtsi/` | `<soc>-dma.dtsi` | `dma_spi1_tx` / `dma_spi1_rx` phandle（如适用） |
| `board/dt-bindings/spi/` | `spi-parameter.h` | 默认 host-id、频率、mode |
| `bus/spi/` | `spi_bus.c` / `spi_bus.h` | Master 总线框架 |
| `hal/spi/` | `spi_hal.h` | HAL API（Master only，跨芯片统一） |
| `hal/spi/` | `spi_hal_<chip>.c` | 具体芯片实现（由项目 `HAL_SRCS` 提供） |
| `bus/dma/` | `dma_core.c` | DMA 通道 request 抽象（如适用） |
| `vfs/spi/` | `spi_vfs.c` | VFS（`heterogeneous,w25q64-master`） |
| `vfs/spi/include/` | `spi_vfs.h` | ioctl 与 `spi_vfs_transfer()` |
| `drivers/flash/` | `w25q64_spi_drv.c` | Flash 协议驱动（如项目使用） |

---

## 3. 设备树契约

| compatible | 驱动 | 说明 |
|------------|------|------|
| `<vendor>,spi-master` | `bus/spi/spi_bus.c` | SPI Host（Master） |
| `heterogeneous,w25q64-master` | `vfs/spi/spi_vfs.c` | Flash 功能节点（VFS client） |

> `compatible` 的 `<vendor>` 段因芯片而异：STM32 用 `stm32,spi-master`，CH32 用 `wch,spi-master`，ESP32 用 `esp32,spi-master`。dtc-lite 通过 `DRIVER_REGISTER` 在编译期匹配。

**Host 节点**（如 `&spi1`）：

| 属性 | 含义 |
|------|------|
| `host-id` | 逻辑 host 编号（默认 `SPI_DEFAULT_HOST_ID` = 1） |
| `hw-instance` | 硬件 SPI 外设号（1→SPI1，见 `DTS_HW_SPI1`） |
| `mosi-port` / `mosi-pin` 等 | 逻辑引脚 → `hal_pin_map_hw_gpio()`（多端口 MCU） |
| `mosi-pin` 等 | 直接 GPIO 编号（ESP32 等单端口 SoC） |
| `dma-tx` / `dma-rx` | 指向 DMA 节点（如适用） |
| `max-transfer-buffer` | 单次传输上限（默认 4096 B） |

**Client 节点**（如 `&w25q64_master`）：

| 属性 | 含义 |
|------|------|
| `cs-port` / `cs-pin` | 软件 CS 引脚（多端口 MCU） |
| `cs-pin` | 软件 CS 引脚（单端口 SoC） |
| `spi-mode` | CPOL/CPHA 0–3 |
| `spi-max-frequency` | 目标 SCK（Hz），HAL 映射为分频 |

---

## 4. 核心数据结构

### `spi_bus_host`

| 字段 | 含义 |
|------|------|
| `cfg` | host_id、hw_instance、GPIO 号、max_transfer_sz |
| `hal_host` | `struct spi_hal_host` |
| `dma_tx` / `dma_rx` | 可选；长度 ≥ 32 B 时走 DMA |
| `bus_mutex` | 总线互斥锁 |
| `ref_count` | Client open 计数 |

### `spi_bus_client`

| 字段 | 含义 |
|------|------|
| `host` | 所属 bus host |
| `cfg` | mode、clock_speed_hz、cs_pin（port\|pin 编码或裸 GPIO） |
| `hw_open` | 是否已 `spi_bus_open()` |

---

## 5. 传输 API

### Bus 层

```c
int spi_bus_transfer(struct device* dev,
                     const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms);
```

流程：加锁 → `spi_hal_device_config_apply` → 拉低 CS → poll 或 DMA → 拉高 CS → 解锁。

### VFS 层

| API | 用途 |
|-----|------|
| `SPI_CMD_TRANSFER` | 全双工 |
| `spi_vfs_transfer()` | 便捷包装 |
| `read` / `write` | 半双工 |

### HAL 层（跨芯片统一，由 `hal/spi/spi_hal.h` 声明）

| 函数 | 说明 |
|------|------|
| `spi_hal_host_init` / `deinit` | 绑定 SPI 外设基址 |
| `spi_hal_device_config_apply` | mode + 分频 |
| `spi_hal_transfer_poll` | 轮询全双工 |
| `spi_hal_transfer_dma` | DMA 全双工（如支持） |

---

## 6. 典型调用链：W25Q64 读 JEDEC ID

```
w25q64_spi_drv
  → spi_vfs_transfer(flash->spi_dev, tx, rx, len, tmo)
    → spi_vfs.c  SPI_CMD_TRANSFER
      → spi_bus_transfer()
        → spi_hal_transfer_poll() 或 transfer_dma()
          → 芯片 SPI 寄存器收发
```

`w25q64_spi_drv` 以 `device_get_parent()` 取得 SPI **Host** 设备；`heterogeneous,w25q64-master` 节点由 `spi_vfs.c` probe。

**Probe 顺序**：`<vendor>,spi-master`（Host）→ `heterogeneous,w25q64-master`（Client + VFS）。dtc-lite 通过 `depends-on` 保证 Host 先于 Client probe。

---

## 7. 引脚与配置流

```
DTS: cs-port / cs-pin, mosi-port / mosi-pin …
  → hal_pin_probe()  → hal_pin_t { port, pin }
  → hal_pin_map_hw_gpio()  → 芯片引脚编号
    ├── 多端口 MCU (STM32/CH32): port << 4 | pin 编码 (HAL_PIN_MAP_LINEAR)
    └── 单端口 SoC (ESP32):     pin 字段直接用 (HAL_PIN_MAP_FLAT)
  → spi_hal_<chip>.c  → SPI 模式与分频
```

逻辑端口 enum 与 `gpio-ctl.h`（`GPIOA` …）同名同值（多端口 MCU）；ESP32 等 SoC 直接用 GPIO 编号。

---

## 8. 相关头文件

| 用途 | 头文件 |
|------|--------|
| 上层 transfer | `vfs/spi/include/spi_vfs.h` |
| Bus API | `bus/spi/spi_bus.h` |
| HAL API | `hal/spi/spi_hal.h` |
| DTS 常量 | `board/dt-bindings/spi/spi-parameter.h` |

---

## 9. 限制与注意点

1. **仅 Master**：无 SPI Slave 栈；勿引用 `spi_slave_*` API（ESP32-S3 的 SPI Slave 走 `esp32,spi` 单独实现，不在此框架内）。
2. **CS 软件控制**：每个 client 独立 CS 引脚，传输前后由 bus 层 toggle。
3. **传输长度**：超过 `max-transfer-buffer` 返回 `VFS_ERR_INVAL`。
4. **厂商初始化与 mini_tree 分工**：引脚复用/时钟在厂商板级初始化；mode/频率/数据在 mini_tree HAL 每次 transfer 前 apply。
5. **compatible 分工**：dtc-lite 对 `heterogeneous,w25q64-master` 匹配 **spi_vfs**；Flash 驱动通过 parent 调 `spi_vfs_transfer`。
6. **跨架构兼容**：HAL 实现因芯片而异，但 `spi_hal.h` 接口统一，业务层与 bus 层无需感知具体芯片。
