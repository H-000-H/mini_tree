# 项目路线图

> **作者前言**
>
> 本人学习和生活方面依然要兼顾，不可能一直按理论盯着代码哪里会犯病然后去优化。所谓实践出真理——会在移植项目过程中完成架构的综合性修改。

---

## 已完成里程碑

### 三平台 HAL 统一与硬件直投模式

STM32 / WCH / ESP32 三平台 HAL 头结构体统一为 `hal_spi_pin_cfg` / `hal_uart_pin_cfg` / `hal_gpio_obj_t`（`uintptr_t port` / `uint16_t pin` / `uint32_t clk_periph` / `uint32_t af`）。移除 `hal_pin_t` 复合引脚抽象与全部 vtable/ops 间接层（`hal_gpio_ops_t` / `hal_spi_ops_t` / `bus_ops`）。DTSI 厂商宏值直投，HAL 零翻译透传给 LL 库 / ESP-IDF driver。ESP32 不再单独维护 ops 路径，DMA 在无硬件支持的场景返回 `VFS_ERR_NOTSUPP`。

### 统一 compatible strings

去除 `stm32,` / `ch32,` / `esp32,` 平台前缀，三平台 IP dtsi 中 `compatible` 统一为 `spi-master` / `spi-slave` / `uart` / `gpio` / `*-platform-cap`，dtc-lite `_validate_compatibles()` 在编译期校验驱动匹配。业务侧 `device_find_by_label()` 通过节点 `label` 获取设备，不直接依赖 compatible string。

### Apache-2.0 许可证统一

所有 104+ 源文件统一 `/* SPDX-License-Identifier: Apache-2.0 */` 首行，LICENSE 文件为 Apache-2.0，告别此前混合的 MIT / Apache-2.0 / 无 license 头状态。`tools/genconfig.py` 生成头模板同步注入 SPDX 标识。

### 三层解耦架构

VFS / bus / HAL 三层解耦收敛为纯中间件：

- **VFS 层** (`vfs/`) — 设备节点管理，通过 `device_find_by_label` / `device_open` / `device_read` / `device_write` / `device_ioctl` 提供统一接口
- **bus 层** (`bus/`) — 总线抽象，`bus_controller` / `bus_client` / `bus_dma_chan` 结构
- **HAL 层** (`hal/`) — 仅含结构体定义、init 函数、`static inline` 快速路径，平台 `.c` 实现下沉到各平台项目目录

`core/include/compiler_compat_poison.h` 的 `#pragma GCC poison` 在编译期强制跨层隔离（bus 外禁调 hal 符号，vfs 外禁调 bus 符号），新增 L10 防御层。

---

## 参考实现：异构多核项目（Heterogeneous-Multicore）

mini_tree 不再维护独立的 `examples/` 展示工程，改为以一个**真实在做的异构多核项目**作为参考实现，在移植过程中完成架构的综合性修改与验证。

| 节点 | 架构基准 | 角色 | 主平台 (Linux) | 也支持 (中间件) |
|------|---------|------|---------------|----------------|
| **STM32F407ZGT6** | ARM Cortex-M4F（通用基准） | 实时控制层（PID/PWM/DMA/CAN 心跳） | Linux / Docker | Windows 原生 |
| **CH32V307** | RISC-V RV32（通用基准） | 网关层（CAN 路由 / Flash OTA / 流量整形） | Linux / Docker | Windows 原生 |
| **ESP32-S3** | Xtensa LX7（异构架构） | USB CDC-ECM 虚拟网卡 + SPI FFT 协处理器 | Linux（不走 Docker） | Windows (ESP-IDF 双端) |
| **i.MX6ULL** | ARM Cortex-A7（应用层） | LVGL UI / MQTT Broker / SocketCAN 网关 | Linux BSP（Yocto） | — |

### 验证目标

- 检验框架对 **ARM / RISC-V / Xtensa** 三类架构的统一适配能力
- ESP32-S3 全速运行（开最高频率），在保证成品效果的前提下尽可能多开任务
- 实测 VFS 表与直接操作寄存器（fast path）**性能天花板在哪，满了多少**
- 高时钟频率、高并发、高实时场景下检验架构**到底能不能经受住检验**
- CAN 总线跨节点通信可靠性、OTA 双区升级、工业级安全与故障自愈

### 构建验证

三节点编译验证以 **Linux** 为主环境，**Windows 原生同样可用**（中间件跨平台）：

```bash
# ARM (STM32F407) / RISC-V (CH32V307) — 主走 Linux 原生 / Docker，Windows 原生也可
./build.sh stm32 -native    # 或 -docker
./build.sh ch32   -native    # 或 -docker

# ESP32-S3 — 主走 Linux 原生，Windows 原生也可（ESP-IDF 官方双端，不走 Docker）
./build.sh esp32  -native
```

### 意义

- 验证 ESP32 异构架构通过 Linux 原生工具链接入的可行性（Windows 也支持），无需 Docker 依赖
- 验证三层解耦架构（VFS / bus / HAL）在三套工具链、三种中断模型、三种 SDK 体系下的实际收敛性
- 验证硬件直投模式（无 vtable/ops）在保持平台中性的同时是否能达到与裸寄存器操作接近的性能

---

## 未来方向

### 短期 — 外设覆盖与平台扩展

- **更多外设 HAL**：在现有 GPIO / SPI / UART / PWM / ADC / DAC / Flash / Storage / WDT / RTC / DMA / SDIO 基础上，补充 I2C / CAN / USB / I2S / Timer（capture/compare）等外设的 HAL 头与三平台实现
- **更多平台移植**：在 STM32F407 / CH32V307 / ESP32-S3 三节点之外，移植 GD32（同构 ARM）、NXP i.MX RT（Cortex-M7）、Allwinner（RISC-V）等平台，进一步验证框架通用性
- **API 命名收敛**：审计全部子系统，确保 `hal_<periph>_*` 命名规范一致，无残留旧命名

### 中期 — 性能与可靠性

- **Fast Path 性能实测**：量化 `device_ioctl` 与 HAL `static inline` 快速路径（GPIO set/get/toggle）的差距，定位并优化热路径
- **DMA 多通道调度**：`bus/dma/` 扩展多通道并行调度与优先级仲裁，提升高并发场景吞吐
- **MPU/PMP 内存保护**：为每个驱动分配内存白名单，越权写入即触发 MemManage Fault，提升功能安全等级
- **Power Management**：实现 suspend/resume 框架，补充 tickless idle、外设时钟门控、唤醒源配置
- **双核 SMP Cache 对齐**：在 `circle_fifo_buffer` 等共享结构上增加 Cache Line 对齐，实测双核场景下的缓存一致性开销

### 长期 — 生态与工具链

- **动态设备树探测**：作为编译期静态 DTS 的补充，支持运行时 I2C/SPI 总线设备枚举与 VFS 节点动态挂载，两者共存不冲突
- **64-bit 架构适配**：RISC-V 64 / ARMv8-A 平台适配，涉及指针宽度、`uintptr_t` 假设、原子操作实现
- **上位机 QT 工具**：补全 Kconfig 图形化之外的监控/配置上位机
- **CI/CD 流水线**：覆盖三平台 Debug/Release 双模式构建、静态检查（`-Wall -Wextra -Werror`、`mypy tools/ --strict`）、host 端单元测试
