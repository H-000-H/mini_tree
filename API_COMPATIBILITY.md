# API 兼容性声明

本文档定义 mini_tree 各头文件的接口稳定性等级, 帮助用户工程评估升级风险.

| 等级 | 含义 | 适用范围 |
|------|------|----------|
| **稳定** | 语义和签名向后兼容, 主版本内不破坏 | `osal.h`, `device.h` (`device_find_by_label`/`device_open`/`device_read`/`device_write`/`device_ioctl`), `driver.h` (`DRIVER_REGISTER`), `buffer_pool.h`, `safe_state.h`, `task_manager.h` (`task_manager_create_task`), `system_init.h` (`mini_tree_pre_os_init`/`mini_tree_start_tasks`/`system_init_complete`) |
| **实验性** | 可能在大版本间变更, 会提前一个版本标记 deprecated | `event_bus.h` / `event_bus.hpp` (C++ Singleton), `system_cmd.hpp` (`SystemCmd::dispatch`/`registerCmd`), `system_wdt.h`, `system_scrubber.h`, `hal/**/*.h` (平台中立头), `bus/**/*.h`, `vfs/**/*.h`, `production_log.h` |
| **内部** | 不对外承诺, 随时可改 | `task_config.h` (生成文件 + 用户覆写), `dt_config_gen.h` (Kconfig/DTS 产物), `config.h` (Kconfig 产物) |

**稳定接口的变更规则**:
- 主版本号递增时可破坏兼容性
- 次版本号递增仅做向后兼容的扩展 (新增函数 / 新增字段在 struct 末尾)
- 补丁版本仅修复 bug, 不修改公开 API 签名和语义

用户工程应只依赖标记为 **稳定** 的接口. 实验性接口可在评估后使用, 升级时需关注 changelog.

## HAL / Bus / VFS 三层架构说明

mini_tree 采用 VFS → Bus → HAL 三层解耦, 隔离由 `compiler_compat_poison.h` 中的 `#pragma GCC poison` 强制 (bus 外禁调 hal 符号, vfs 外禁调 bus 符号).

**HAL 头 (`hal/**/*.h`)** 为平台中立声明, 统一 HAL 头结构体:

| 结构体 | 字段 | 用途 |
|--------|------|------|
| `hal_spi_pin_cfg` | `uintptr_t port`, `uint16_t pin`, `uint32_t clk_periph`, `uint32_t af` | SPI 引脚配置 (mosi/miso/sclk) |
| `hal_uart_pin_cfg` | `uintptr_t port`, `uint16_t pin`, `uint32_t clk_periph`, `uint32_t af` | UART 引脚配置 (tx/rx) |
| `hal_gpio_obj_t` | `uintptr_t port`, `uint16_t pin`, `uint32_t clk_periph`, `bool is_used` | GPIO 对象 (嵌入 VFS priv) |

HAL `.c` 实现由各平台项目目录通过 `HAL_SRCS` 变量提供, 采用**硬件直投模式** (无 vtable / ops, DTSI 厂商宏值直投, HAL 零翻译透传给 LL 库 / ESP-IDF driver). 因此 `hal/**/*.h` 的稳定性只覆盖头结构与函数原型, 不覆盖平台 `.c` 实现的内部细节.

## 统一 compatible strings

设备树驱动匹配采用统一 compatible strings, 去除平台前缀:

| compatible | 角色 |
|------------|------|
| `spi-master` | SPI host controller |
| `spi-slave` | SPI slave |
| `uart` | UART host |
| `uart-client` | UART bus client |
| `heterogeneous,gpios` | GPIO VFS client |
| `heterogeneous,spi-master-client` | SPI bus client (spi_vfs) |
| `heterogeneous,fft-spi-slave` | FFT SPI slave (示例专用 IP) |
| `*-platform-cap` | 平台能力声明 (IP dtsi 中标记, 用于驱动匹配) |

业务侧 `device_find_by_label()` 通过节点 `label` 获取设备, 不直接依赖 compatible string, 因此 compatible 的调整不影响业务 API 兼容性.

## 已删除的过时概念 (不再受兼容性承诺)

下列符号 / 概念在统一中间件架构重构中**已完全删除**, 不再受任何兼容性承诺:

- `hal_pin_t` 复合引脚编码及其辅助函数 (改为 `hal_spi_pin_cfg` / `hal_uart_pin_cfg` / `hal_gpio_obj_t` 三组结构体)
- `hal_gpio_ops_t`, `hal_spi_ops_t` 等 vtable / ops 间接层 (改为硬件直投)
- `hal_gpio_fast.h`, `hal_cpu_fast.h`, `hal_pwm_fast.h` 等 fast 头文件
- `system_log.hpp` (统一为 `system_log.h`)
- `board_config.h` (统一在 `board/include/` 各专用头)
- `hal_timer.h` (定时器抽象未提供)
- `bus_types.h` / `bus_ops` struct (总线类型与 ops 合并入 `board/include/bus.h`)
- `spi_hal_*` 命名 (统一为 `hal_spi_*`)
- `hal/paths.cmake` (HAL 源文件路径改由各平台项目通过 `HAL_SRCS` 提供)
- `bus/include/bus.h` (实际位置在 `board/include/bus.h`)
