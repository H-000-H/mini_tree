# 文件索引

### 系统核心

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 顶层构建，Kconfig 条件路由，C17/C++17 标准，OSAL/System 后端切换，dtc-lite 集成 |
| `Kconfig` | 全局配置树，Platform / OSAL / System / Log / Board / Build 六菜单 |
| `.config` | 配置输出 (menuconfig 生成) |

### core/

| 文件 | 说明 |
|------|------|
| `include/event_bus.hpp` | 发布订阅总线，ISR 自适应，范围订阅，快照锁 |
| `include/buffer_pool.h` | 位图无锁 O(1) 内存池 |
| `include/system_log.h` | 日志宏，三后端 (OSAL/ESP/PRINTF) |
| `include/critical_data.h` | 双重反码 + volatile 关键数据保护 |
| `include/production_log.h` | NVS 环形错误缓冲 (黑匣子) |
| `src/event_bus.cpp` | EventBus C 兼容封装 + C++ 分发任务 |
| `src/buffer_pool.c` | CAS 原子位图分配器 |
| `src/production_log.c` | 弱符号钩子 Flash 持久化 |

### osal/

| 文件 | 说明 |
|------|------|
| `include/osal.h` | 统一抽象接口 (Task/Queue/Mutex/Spinlock/Memory/Time/Log) |
| `include/osal_null.h` | 裸机后端 ISR 入口/退出/SysTick 声明 |
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
| `src/task_manager.cpp` | 任务创建实现（封装 `osal_task_create_handle` + TWDT 订阅） |
| `src/lifecycle.cpp` | 状态机转移实现 |

### system_c/ (C 后端)

| 文件 | 说明 |
|------|------|
| `include/system_init.h` | C 两段式点火 API |
| `include/system_cfg.h` | C 版配置日志宏 |
| `include/system_wdt.h` | 包装 system_wdt.hpp |
| `include/system_scrubber.h` | 包装 system_scrubber.hpp |
| `include/task_manager.h` | C 版任务创建封装 (`task_manager_create_task`) |
| `src/system_init.c` | C Phase 1 + Phase 2 点火 |
| `src/system_wdt.c` | C 版看门狗 (static const 替代 constexpr) |
| `src/system_scrubber.c` | C 版 CRC32 巡检 (全表) |
| `src/task_manager.c` | C 版任务创建 |

### board/

| 文件 | 说明 |
|------|------|
| `board.dts` | 设备树源文件（由具体项目提供，经 `BOARD_DTS` 变量传入） |
| `include/device.h` | VFS 设备框架（`device_find_by_label`/`device_open`/`device_read`/`device_write`/`device_ioctl`） |
| `include/driver.h` | DRIVER_REGISTER 宏（编译期 dtc-lite 扫描收录） |
| `include/board_config.h` | 集中配置入口 |
| `src/board_driver.c` | Probe 引擎 + safety shutdown |
| `src/board_device.c` | 设备树运行时 + 互斥锁 |
| `src/dev_lifecycle.c` | 设备生命周期绑定（open/close/suspend/resume） |
| `src/bus.c` | 总线控制器/客户端注册 |
| `src/config_store.c` | 配置存储 |
| `src/task_config.c` | 任务配置（生成文件 + 用户覆写） |
| `src/task_utils.c` | 任务辅助工具 |
| `tools/dtc-lite.py` | 编译期设备树编译器 (Kahn 排序) |

### bus/ (总线层)

| 文件 | 说明 |
|------|------|
| `include/bus.h` | 总线抽象（`bus_controller`/`bus_client`） |
| `spi/spi_bus.{c,h}` | SPI Master 总线框架（host probe + client 注册 + 软 CS） |
| `uart/uart_bus.{c,h}` | UART 总线框架 |
| `dma/dma_core.c` | DMA 通道 request 抽象 |

### vfs/ (VFS 设备节点)

| 文件 | 说明 |
|------|------|
| `spi/spi_vfs.{c,h}` | SPI VFS client（`SPI_CMD_TRANSFER` / `spi_vfs_transfer`） |
| `uart/uart_vfs.{c,h}` | UART VFS client |
| `gpio/vfs-gpio.{c,h}` | GPIO VFS client |

### hal/

| 文件 | 说明 |
|------|------|
| `hal_if_dummy.c` | HAL 接口兜底实现 (未实现的 ops 返回 -1) |
| `paths.cmake` | HAL 源文件路径配置 |
| `gpio/hal_gpio.h` | GPIO 抽象 |
| `uart/hal_uart.h` | UART 抽象 |
| `spi/hal_spi.h` | SPI 抽象 |
| `pwm/hal_pwm.{c,h}` | PWM 抽象（通用实现） |
| `cpu/hal_cpu.h` | CPU 紧急停止 / AMP 副核启动抽象 |
| `cpu/hal_cpu_delay.h` | CPU 延时抽象 |
| `cpu/hal_cpu_amp.c` | AMP 副核启动 weak 实现 |
| `analog/hal_adc.h` | ADC 抽象 |
| `analog/hal_dac.h` | DAC 抽象 |
| `storage/hal_storage.h` | 存储抽象 |
| `storage/hal_flash.h` | Flash 抽象 |
| `system/hal_wdt.h` | 硬件看门狗抽象 |
| `system/hal_timer.h` | 定时器抽象 |
| `system/hal_rtc.h` | RTC 抽象 |
| `system/hal_dma.h` | DMA 抽象 |
| `system/hal_sdio.h` | SDIO 抽象 |
| `system/hal_platform_safety.h` | 平台安全抽象 |

> 具体芯片的 HAL 实现源文件由项目通过 `HAL_SRCS` 变量提供，例如 STM32F407 提供 `hal/spi/spi_hal_stm32.c`、CH32V307 由 `hal/CMakeLists.txt` 集成 `ch32_periph`。

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
