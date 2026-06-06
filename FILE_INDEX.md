# 文件索引

### 系统核心

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 顶层构建，Kconfig 条件路由，C23/C++23 标准，disasm |
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
| `include/system_init.hpp` | C++ 两段式点火 API |
| `include/system_runtime.hpp` | 运行时生命周期 (init/start/stop/suspend/resume) |
| `include/system_wdt.hpp` | Task WDT + RTC WDT + 栈水位监控 |
| `include/system_scrubber.hpp` | Flash CRC 巡检 |
| `include/task_manager.hpp` | 任务创建封装 |
| `include/lifecycle.hpp` | 生命周期基类 + 状态机 |
| `include/safe_state.h` | Bootloop 防护 + enter_safe_state |
| `src/system_init.cpp` | Phase 1 + Phase 2 点火实现 |
| `src/lifecycle.cpp` | 状态机转移实现 |

### system_c/ (C 后端)

| 文件 | 说明 |
|------|------|
| `include/system_init.h` | C 两段式点火 API |
| `include/system_cfg.h` | C 版配置日志宏 |
| `include/system_wdt.h` | 包装 system_wdt.hpp |
| `include/system_scrubber.h` | 包装 system_scrubber.hpp |
| `include/task_manager.h` | C 版任务创建封装 |
| `src/system_init.c` | C Phase 1 + Phase 2 点火 |
| `src/system_wdt.c` | C 版看门狗 (static const 替代 constexpr) |
| `src/system_scrubber.c` | C 版 CRC32 巡检 (全表) |
| `src/task_manager.c` | C 版任务创建 |

### board/

| 文件 | 说明 |
|------|------|
| `board.dts` | 设备树源文件 |
| `include/device.h` | VFS 设备框架 |
| `include/driver.h` | DRIVER_REGISTER 宏 |
| `include/board_config.h` | 集中配置入口 |
| `src/board_driver.c` | Probe 引擎 + safety shutdown |
| `src/board_device.c` | 设备树运行时 + 互斥锁 |
| `tools/dtc-lite.py` | 编译期设备树编译器 (Kahn 排序) |

### hal_if/

| 文件 | 说明 |
|------|------|
| `include/hal_gpio.h` | GPIO 抽象 |
| `include/hal_gpio_fast.h` | Fast-Path 寄存器直写 |
| `include/hal_spi_bus.h` | SPI 总线抽象 |
| `include/hal_i2c.h` | I2C 总线抽象 |
| `include/hal_uart.h` | UART 抽象 |
| `include/hal_pwm.h` | PWM 抽象 |
| `include/hal_adc.h` | ADC 抽象 |
| `include/hal_cpu.h` | CPU 紧急停止抽象 |
| `include/hal_wdt.h` | 硬件看门狗抽象 |
| `include/hal_flash.h` | Flash 抽象 |
| `include/hal_i2s_bus.h` | I2S 总线抽象 |
| `include/hal_storage.h` | 存储抽象 |
| `include/hal_platform_safety.h` | 平台安全停机抽象 |

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
| `ROADMAP.md` | 未来项目路线图（ESP32-S3 DSP、CAN 工控/汽车） |
| `CONTRIBUTING.md` | 贡献指南 |
| `docs/getting_started.md` | 快速开始、配置、工程集成、点火时序 |
| `docs/porting_guide.md` | 硬件移植指南 |
| `docs/driver_guide.md` | 设备树与驱动开发 |
| `docs/service_spec.md` | 服务编写与应用解耦规范 |
| `docs/debug_monitor.md` | 调试、监控与单元测试 |
| `docs/fast_path.md` | 红线区硬实时 Fast Path |
| `docs/faq.md` | 常见问题 |
| `docs/osal_switching.md` | OSAL 后端切换注意事项 |
| `docs/keil_integration.md` | Keil MDK 集成说明 |
| `docs/problem_summary.md` | 问题总结 |
