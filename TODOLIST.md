# 待办与优化清单

## 已完成

- [x] **三平台 HAL 统一** — STM32 / WCH / ESP32 三平台 HAL 头结构体统一为 `hal_spi_pin_cfg` / `hal_uart_pin_cfg` / `hal_gpio_obj_t`，ESP32 不再单独维护 ops/vtable 路径
- [x] **硬件直投模式** — 移除 `hal_gpio_ops_t` / `hal_spi_ops_t` / `bus_ops` 等 vtable/ops 间接层，DTSI 厂商宏值直投，HAL 零翻译透传给 LL 库 / ESP-IDF driver
- [x] **`hal_pin_t` 删除** — 移除 `hal_pin_t` 复合引脚抽象及全部辅助函数与宏（`hal_pin_make` / `hal_pin_invalid` / `hal_pin_is_valid` / `hal_pin_equal` / `HAL_PINPORT` / `HAL_PINNUM` / `HAL_MAKE_PIN` / `hal_pin_probe` / `hal_pin_map_hw_gpio`），pin 字段统一使用 plain `int`（STM32/WCH = `port<<16|pin`，ESP32 = SoC GPIO number，`-1` = 未用）
- [x] **统一 compatible strings** — 去除 `stm32,` / `ch32,` / `esp32,` 平台前缀，三平台 IP dtsi 中 `compatible` 统一为 `spi-master` / `spi-slave` / `uart` / `gpio` / `*-platform-cap`，dtc-lite `_validate_compatibles()` 编译期校验
- [x] **Apache-2.0 许可证统一** — 所有 104+ 源文件统一 `/* SPDX-License-Identifier: Apache-2.0 */` 首行，LICENSE 文件为 Apache-2.0，告别此前混合的 MIT / Apache-2.0 / 无 license 头状态
- [x] **API 命名统一** — `spi_hal_*` 统一为 `hal_spi_*`，`uart_hal_*` 统一为 `hal_uart_*`
- [x] **三层解耦架构** — VFS / bus / HAL 三层解耦收敛为纯中间件，`core/include/compiler_compat_poison.h` 的 `#pragma GCC poison` 编译期强制跨层隔离（bus 外禁调 hal 符号，vfs 外禁调 bus 符号），新增 L10 防御层
- [x] **`bus_types.h` 删除** — STM32 与 WCH 平台的 `bus_types.h` 已删除，总线类型合并入 `board/include/bus.h`
- [x] **SPI / UART 子系统清理** — SPI 相关文件移除 `hal_gpio.h` include 与 `spi_bus_host_config` / `spi_bus_client_config` 残留；UART 相关文件移除 `hal_gpio.h` include 与 `uart_bus_host_config` / `hal_uart_bus_get` / vtable 残留
- [x] **ESP32 DMA stub** — ESP32 无 STM32/WCH 风格 DMA，`hal_uart_write_dma` 返回 `VFS_ERR_NOTSUPP`，`hal_uart_dma_abort` 为空实现，避免 `#ifdef ESP_PLATFORM` 散落各处

## 待办

### 安全与可靠性

- **MPU/PMP 内存保护** — 为每个驱动分配内存白名单，越权写入即触发 MemManage Fault 停机。需在 `hal/` 层增加 `hal_mpu_*` / `hal_pmp_*` 抽象，OSAL 层提供区域配置接口
- **Power Management** — 缺少 suspend/resume 框架，OSAL 层已预留接口，待实现。需补充 tickless idle、外设时钟门控、唤醒源配置
- **双核 SMP Cache 对齐** — `circle_fifo_buffer` 在双核 MCU（ESP32-S3、Cortex-M7 双核等）上存在理论上的 Cache Thrashing 风险，需在关键结构体增加 `__attribute__((aligned(N)))` 并实测验证。涉及 ARM 与 RISC-V 两种缓存模型，Cache Line 大小通常为 32 或 64 字节

### 架构扩展

- **64-bit 架构适配** — 当前仅在 32-bit 平台验证，RISC-V 64 / ARMv8-A 需适配。涉及指针宽度、`uintptr_t` 假设、原子操作实现
- **更多外设 HAL** — 当前已覆盖 GPIO / SPI / UART / PWM / ADC / DAC / Flash / Storage / WDT / RTC / DMA / SDIO，待补充 I2C / CAN / USB / I2S / Timer（capture/compare）等外设的 HAL 头与平台实现
- **更多平台移植** — 当前覆盖 STM32F407 / CH32V307 / ESP32-S3，待移植 GD32（同构 ARM）、NXP i.MX RT（Cortex-M7）、Allwinner（RISC-V）等平台，进一步验证框架通用性
- **动态设备树探测** — 当前通过 `dtc-lite.py` 在编译期完成设备拓扑排序和驱动 Probe，可热插拔场景需运行时动态枚举。新增动态探测应作为独立模块，不破坏现有静态路径

### 性能优化

- **BufferPool 池组** — 单池上限 32 个 buffer（`uint32_t` 位图限制）。需要大于 32 的组件需自行管理多个池。内置池组机制，内部用多字位图扩展容量，对外保持 `bp_alloc/bp_free` 接口不变
- **Fast Path 性能实测** — 实测 VFS 表与直接操作寄存器（fast path）的性能天花板，量化 `device_ioctl` 与 HAL `static inline` 快速路径（GPIO set/get/toggle）的差距，优化热路径
- **DMA 多通道调度** — `bus/dma/` 当前为单通道串行调度，待扩展多通道并行调度与优先级仲裁

### 工具链与生态

- **上位机 QT 工具** — 当前只有 Kconfig 的图形化界面太土了，缺少监控/配置上位机。因为本人不会 QT 所以写不出上位机，在未来学会 QT 之后会回来弥补缺少上位机的这一个缺陷
- **CI/CD 流水线** — 补充 GitHub Actions / GitLab CI 配置，覆盖三平台 Debug/Release 双模式构建、`-Wall -Wextra -Werror` 零警告检查、`mypy tools/ --strict` 静态类型检查
- **单元测试框架** — 补充 host 端单元测试（MinGW / Linux），覆盖 `core/`（EventBus、BufferPool、circle_fifo_buffer）与 `board/`（device 查找、DTS 解析）逻辑
