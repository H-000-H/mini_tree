# 文件索引

> 本索引基于 `mini_tree/` 中间件实际代码结构维护，所有路径均与代码库一致。

### 系统核心

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 顶层构建，Kconfig 条件路由，C17/C++17 标准，OSAL/System 后端切换，dtc-lite 集成 |
| `Kconfig` | 全局配置树，Platform / OSAL / System / Log / Board / Build 六菜单 |
| `.config` | 配置输出 (menuconfig 生成) |

### core/ (核心基础设施)

| 文件 | 说明 |
|------|------|
| `include/bh/bh.h` | BH (Bottom Half) 中断下半部工作队列核心，无锁环形队列 |
| `include/bh/bh_bare.h` | 裸机后端 BH 适配 |
| `include/bh/bh_os.h` | RTOS 后端 BH 适配 |
| `include/bh/bh_config.h` | BH 配置宏 |
| `include/buffer_pool.h` | 位图无锁 O(1) 内存池 |
| `include/compiler_compat.h` | 跨编译器兼容宏 (COMPAT_WEAK / COMPAT_WARN_UNUSED_RESULT 等) |
| `include/compiler_compat_poison.h` | `#pragma GCC poison` 隔离强制层 (bus 外禁调 hal, vfs 外禁调 bus) |
| `include/critical_data.h` | 双重反码 + volatile 关键数据保护 |
| `include/event_bus.h` | EventBus C 接口 (轻量发布/订阅，ID 区间订阅，ISR 自适应，快照锁) |
| `include/event_bus.hpp` | EventBus C++ Singleton 实现 |
| `include/printf_output.h` | printf 输出后端抽象 |
| `include/production_log.h` | NVS 环形错误缓冲 (黑匣子) |
| `include/safe_state.h` | Bootloop 防护 + enter_safe_state |
| `include/system_log.h` | 日志宏，三后端 (OSAL/ESP/PRINTF)，含 SYS_LOG / DRV_LOG 两层 |
| `src/buffer_pool.c` | CAS 原子位图分配器，32 字节 DMA 对齐 |
| `src/event_bus.c` | EventBus C 实现 (纯 C 单例, SIOF 安全, 封表后 ISR 可安全 post) |
| `src/event_bus.cpp` | EventBus C++ 分发任务封装 |
| `src/printf_output.c` | printf 输出后端实现 |
| `src/production_log.c` | 弱符号钩子 Flash 持久化 |

### osal/ (操作系统抽象层)

| 文件 | 说明 |
|------|------|
| `include/osal.h` | 统一抽象接口 (Task/Queue/Mutex/Spinlock/Memory/Time/Log) |
| `include/osal_null.h` | 裸机后端 ISR 入口/退出/SysTick 声明 |
| `include/osal_tick.h` | RTOS tick 类型定义 (`osal_tick_t`，Kconfig 编译期选定后端类型) |
| `src/osal_freertos.c` | FreeRTOS 后端实现 |
| `src/osal_rtthread.c` | RT-Thread 后端实现 |
| `src/osal_null.c` | 裸机后端实现 (原子环形队列 + 忙等待) |

### system_cpp/ (C++ 后端)

| 文件 | 说明 |
|------|------|
| `include/system_init.hpp` | C++ 两段式点火 API (`mini_tree_pre_os_init`/`mini_tree_start_tasks`/`system_init_complete`) |
| `include/system_cmd.hpp` | 异步"邮局"模式命令路由器 (`SystemCmd::dispatch`/`registerCmd`) |
| `include/system_runtime.hpp` | 运行时生命周期 (init/start/stop/suspend/resume) |
| `include/system_wdt.hpp` | Task WDT + RTC WDT + 栈水位监控 |
| `include/system_scrubber.hpp` | Flash CRC 巡检 |
| `include/task_manager.hpp` | 任务创建封装（业务层使用 `task_manager_create_task` C API） |
| `include/lifecycle.hpp` | 生命周期基类 + 状态机 |
| `include/safe_state.h` | Bootloop 防护 + enter_safe_state |
| `src/system_init.cpp` | Phase 1 + Phase 2 点火实现 |
| `src/system_cmd.cpp` | `SystemCmd` 单例实现（命令表 + 反序列化派发） |
| `src/system_runtime.cpp` | 运行时生命周期实现 |
| `src/task_manager.cpp` | 任务创建实现（封装 `osal_task_create_handle` + TWDT 订阅） |
| `src/lifecycle.cpp` | 状态机转移实现 |
| `src/safe_state.c` | Safe state C 实现 |
| `src/system_wdt.cpp` | 看门狗实现 |
| `src/system_scrubber.cpp` | Flash CRC 巡检实现 |

### system_c/ (C 后端)

| 文件 | 说明 |
|------|------|
| `include/system_init.h` | C 两段式点火 API |
| `include/system_cfg.h` | C 版配置日志宏 |
| `include/system_wdt.h` | C 版看门狗 (static const 替代 constexpr) |
| `include/system_scrubber.h` | C 版 Flash CRC 巡检 |
| `include/system_scrubber_config.h` | Scrubber 巡检表配置 |
| `include/task_manager.h` | C 版任务创建封装 (`task_manager_create_task`) |
| `src/system_init.c` | C Phase 1 + Phase 2 点火 |
| `src/system_wdt.c` | C 版看门狗 |
| `src/system_scrubber.c` | C 版 CRC32 巡检 (全表) |
| `src/task_manager.c` | C 版任务创建 |

### board/ (板级设备框架 + DTS)

| 文件 | 说明 |
|------|------|
| `include/device.h` | VFS 设备框架（`device_find_by_label`/`device_open`/`device_read`/`device_write`/`device_ioctl`） |
| `include/driver.h` | DRIVER_REGISTER 宏（编译期 dtc-lite 扫描收录） |
| `include/bus.h` | 总线抽象（`bus_controller`/`bus_client`，三层架构 Bus Core） |
| `include/VFS.h` | VFS 错误码与指针错误编码 (`VFS_ERR_xxx` / `ERR_PTR` / `PTR_ERR` / `IS_ERR`) |
| `include/dev_lifecycle.h` | 设备生命周期绑定 (open/close/suspend/resume) |
| `include/board_dma.h` | 板级 DMA 抽象 |
| `include/config_store.h` | 配置存储 |
| `include/task_config.h` | 任务配置（生成文件 + 用户覆写） |
| `include/task_utils.h` | 任务辅助工具 |
| `src/board_driver.c` | Probe 引擎 + safety shutdown |
| `src/board_device.c` | 设备树运行时 + 互斥锁 |
| `src/bus.c` | 总线控制器/客户端注册 (静态表 + atomic ref_count) |
| `src/dev_lifecycle.c` | 设备生命周期绑定实现 |
| `src/board_dma.c` | 板级 DMA 实现 |
| `src/config_store.c` | 配置存储实现 |
| `src/task_config.c` | 任务配置实现 |
| `src/task_utils.c` | 任务辅助工具实现 |
| `dt-bindings/` | DTS 设备绑定 (vendor prefix / compatible / cells 定义) |

> 设备树源文件 (`*.dts` / `*.dtsi`) 由具体项目通过 `BOARD_DTS` 变量传入，不在 `mini_tree/` 仓内固化。

### bus/ (总线层)

| 文件 | 说明 |
|------|------|
| `spi/spi_bus.h` | SPI Master 总线框架声明 |
| `spi/spi_bus.c` | SPI Master 总线框架实现（host probe + client 注册 + 软 CS） |
| `uart/uart_bus.h` | UART 总线框架声明 |
| `uart/uart_bus.c` | UART 总线框架实现 |
| `dma/dma.h` | DMA 通道 request 抽象声明 |
| `dma/dma_internal.h` | DMA 内部头 (平台后端共享) |
| `dma/dma_core.c` | DMA 通道 request 抽象实现 |

### vfs/ (VFS 设备节点)

| 文件 | 说明 |
|------|------|
| `spi/spi_vfs.h` | SPI VFS client 声明 (`SPI_CMD_TRANSFER` / `spi_vfs_transfer`) |
| `spi/spi_vfs.c` | SPI VFS client 实现，注册 `spi-master` / `heterogeneous,spi-master-client` |
| `uart/uart_vfs.h` | UART VFS client 声明 |
| `uart/uart_vfs.c` | UART VFS client 实现，注册 `uart` / `uart-client` |
| `gpio/vfs-gpio.h` | GPIO VFS client 声明 |
| `gpio/vfs-gpio.c` | GPIO VFS client 实现，注册 `heterogeneous,gpios` |

### hal/ (硬件抽象层 — 平台中立头 + 平台 .c 后端)

> `hal/` 头文件 (`*.h`) 平台中立，仅声明结构体与函数原型；具体芯片的 HAL `.c` 实现由各平台项目目录提供（如 STM32 / CH32 / ESP32 平台目录下的 `hal/spi/hal_spi_<chip>.c` 等），通过 `HAL_SRCS` 变量集成。
> 采用**硬件直投模式**：无 vtable / ops 间接层，DTSI 厂商宏值直投，HAL 零翻译透传给 LL 库 / ESP-IDF driver。

| 文件 | 说明 |
|------|------|
| `hal_if_dummy.c` | HAL 接口兜底实现 (未实现函数返回 `-ENOTSUP`) |
| `gpio/hal_gpio.h` | GPIO 抽象 (`hal_gpio_obj_t { port, pin, clk_periph, is_used }`，VFS priv 内嵌) |
| `uart/hal_uart.h` | UART 抽象 (`hal_uart_pin_cfg { port, pin, clk_periph, af }`) |
| `spi/hal_spi.h` | SPI 抽象 (`hal_spi_pin_cfg { port, pin, clk_periph, af }`) |
| `pwm/hal_pwm.h` | PWM 抽象声明 |
| `pwm/hal_pwm.c` | PWM 通用实现 |
| `cpu/hal_cpu.h` | CPU 紧急停止 / AMP 副核启动抽象 |
| `cpu/hal_cpu_delay.h` | CPU 延时抽象 |
| `cpu/hal_cpu_amp.c` | AMP 副核启动 weak 实现 (`CPU_CORES > 1` 时编译) |
| `analog/hal_adc.h` | ADC 抽象 |
| `analog/hal_dac.h` | DAC 抽象 |
| `storage/hal_storage.h` | 存储抽象 |
| `storage/hal_flash.h` | Flash 抽象 |
| `system/hal_wdt.h` | 硬件看门狗抽象 |
| `system/hal_rtc.h` | RTC 抽象 |
| `system/hal_dma.h` | DMA 抽象 |
| `system/hal_sdio.h` | SDIO 抽象 |
| `system/hal_platform_safety.h` | 平台安全抽象 |

### drivers/ (示例驱动)

| 文件 | 说明 |
|------|------|
| `flash/w25q64_drv.h` | W25Q64 SPI NOR Flash 驱动声明 |
| `flash/w25q64_spi_drv.c` | W25Q64 SPI 驱动实现 (示例 DRIVER_REGISTER 用法) |

### algorithm/ (算法原语)

| 文件 | 说明 |
|------|------|
| `buffer/circle_fifo_buffer.c` | 无锁 SPSC 环形缓冲区实现 (`fifo_spsc`，原子 ACQ/REL) |

### tools/ (构建期工具与生成头)

| 文件 | 说明 |
|------|------|
| `dtc-lite.py` | 编译期设备树编译器入口 (Kahn 拓扑排序，零运行时解析) |
| `genconfig.py` | Kconfig → `config.h` / `dt_config_gen.h` 生成器 |
| `menuconfig.py` | menuconfig GUI 入口 (调用 kconfiglib) |
| `post_build_crc.py` | 构建后 CRC 注入与校验 (Scrubber 基线) |
| `system_scrubber_crc_gen.h` | Scrubber CRC 基线生成头模板 |
| `system_scrubber_crc_stub.h` | Scrubber CRC 默认 stub (无基线时回退) |
| `dtc_lite/` | dtc-lite Python 包 (lexer/parser/AST/compiler/generator) |

### docs/

| 文件 | 说明 |
|------|------|
| `README.md` | 项目总览与快速开始 |
| `ARCHITECTURE.md` | 架构总览 |
| `USAGE.md` | 使用手册索引 |
| `NOTICE.md` | 架构演进与重构全记录 |
| `API_COMPATIBILITY.md` | API 兼容性声明 |
| `FILE_INDEX.md` | 文件索引 (本文) |
| `CHANGELOG.md` | 更新日志 |
| `ROADMAP.md` | 项目路线图（异构多核项目 Heterogeneous-Multicore 参考实现） |
| `CONTRIBUTING.md` | 贡献指南 |
| `docs/getting_started.md` | 快速开始、配置、工程集成、点火时序 |
| `docs/porting_guide.md` | 硬件移植指南 |
| `docs/driver_guide.md` | 设备树与驱动开发 |
| `docs/service_spec.md` | 服务编写与应用解耦规范 |
| `docs/debug_monitor.md` | 调试、监控与单元测试 |
| `docs/fast_path.md` | 红线区硬实时 Fast Path |
| `docs/faq.md` | 常见问题 |
| `docs/osal_switching.md` | OSAL 后端切换注意事项 |
| `docs/problem_summary.md` | 问题总结 |
