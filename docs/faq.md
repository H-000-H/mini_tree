## 12. 常见问题

### Q: 如何选择合适的 OSAL 后端？

| 场景 | 选择 |
|------|------|
| 产品级多任务 | FreeRTOS 或 RT-Thread |
| 纯前后台裸机 | OSAL_NULL |
| 资源极度受限 (< 8KB RAM) | OSAL_NULL + system_c |
| 需要 FinSH 调试终端 | RT-Thread |
| 社区生态广泛 | FreeRTOS |

### Q: 如何选择 system 后端？

| 场景 | 推荐选择 |
|------|---------|
| 默认现代 C++ | SYSTEM_CPP |
| 医疗/工控合规要求纯 C | SYSTEM_C |
| 团队 C 技能为主 | SYSTEM_C |
| 功能安全认证交付 | SYSTEM_C |

### Q: 如何添加新的 HAL 接口？

1. 在 `hal/` 对应子目录中声明操作表结构体（如 `hal/spi/spi_hal.h`）
2. 在具体芯片项目中实现（通过 `HAL_SRCS` 变量传入，例如 `hal/spi/spi_hal_stm32.c`）
3. 驱动通过 `struct device*` 的 `device_ioctl` 调用，或总线层（`bus/spi/spi_bus.c`）通过 `spi_hal_*()` 调用

### Q: 为什么需要在启动前 touch Singleton？

```cpp
(void)EventBus::getInstance();  // 预触摸
```

C++11 局部静态变量的首次初始化使用 `__cxa_guard_acquire` 互斥锁。如果在 ISR 中首次调用 `getInstance()`，互斥锁可能导致死锁。Phase 1 在 RTOS 启动前执行，自然完成了实例化。

### Q: 构建报错 `Target requires the language dialect "C23"`？

历史版本曾使用 C23/C++23，当前版本统一为 C17/C++17 以兼容 RISC-V GCC 8.2.0 (Docker, WCH MounRiver Studio) 与 15.2.0 (Windows 原生, MounRiver GCC15)。使用提供的工具链文件自动处理：

```bash
# ARM (STM32F407) — 工具链文件: cmake/gcc-arm-none-eabi.cmake
cd STM32F407ZGT6 && cmake --preset Debug && cmake --build build/Debug

# RISC-V (CH32V307) — 工具链文件: cmake/riscv32-wch-elf-ch32v307.cmake
cd CH32V307 && cmake --preset Debug && cmake --build build/Debug
```

`CMakePresets.json` 已绑定正确的工具链文件，三端（Docker/Linux/Windows）路径由 `find_program` 自动探测。

### Q: mini_tree 在小容量单片机（如 ARM Cortex-M3，64KB Flash）上内存占用多少？

以 ARM Cortex-M3 + OSAL_NULL + system_cpp + GCC + newlib-nano 为参考估算。实际占用区分厂商 HAL 驱动和 mini_tree 框架自身：

| 配置 | Flash | RAM | 说明 |
|------|-------|-----|------|
| **框架裸架构（无厂商 HAL）** | **~8 KB** | **~2 KB** | 仅 mini_tree 内核：board/core/system/osal/algorithm |
| **+ 厂商 HAL 全外设（未调用）** | **~14–15 KB** | **~3–4 KB** | ADC/UART/SPI/I2C/TIM/RTC 全使能但因未调用被 `--gc-sections` 丢弃 |
| **+ 实际调用 UART/SPI/TIM** | **~20–28 KB** | **~4–5 KB** | 在代码中真正初始化使用这些外设，对应 HAL 函数被保留 |

> 注意：以上 "厂商 HAL" 指芯片厂家提供的 HAL 库（如 ST `stm32f1xx_hal_uart.c`、GD32 标准库等），**并非 mini_tree 驱动模块**。
> 框架自身只有 GPIO 和安全关断两个驱动，占用极小。如果你在业务代码里实际调用厂商 HAL API，
> 对应函数才会保留，Flash 随之增长——这是 HAL 的代价，与框架无关。

**关键结论：**

- mini_tree 框架自身仅 **~5.5 KB**，不存在"架构太大"的问题
- Flash 占用的主要来源是 **外设 HAL 驱动 + libc printf 实现**，与框架无关
- 开 HAL 外设越多，`-ffunction-sections` 越重要——否则未使用的 ADC/I2C/SPI/TIM 等全部被链接
- **推荐 GCC + newlib-nano**（`--specs=nano.specs`），在小芯片（≤ 64KB Flash）上优势明显：
  - nano printf 不支持浮点格式化，体积小
  - `--specs=nosys.specs` 提供弱符号 syscall 桩，无需手动实现
- 对大容量芯片（几百 KB 以上），可自由选择 newlib / picolibc / 完整 newlib

### Q: 多核配置注意事项？

业务任务通过 `task_manager_create_task(name, stack, prio, entry, arg, core_id)` 的 `core_id` 参数绑核（AMP 双核时指定核心，单核传 0）；EventBus 跨核事件投递由 `osal_queue_send` 保证原子性；BufferPool 内存建议对齐到 cache line 避免伪共享。

> `task_manager_create_task` 内部封装 `osal_task_create_handle` 并自动订阅 Task WDT，是业务层创建任务的推荐入口；`osal_task_create_handle` 仅在框架内部使用。

### Q: 业务任务怎么和硬件解耦？

通过"异步邮局模式"：收包任务调用 `SystemCmd::dispatch("cmd_name", args, len, nullptr)` 路由到 handler，handler 只把命令 `osal_queue_send` 到领域任务的专属队列（不阻塞、不碰硬件），领域任务再通过 `device_find_by_label`/`device_open`/`device_ioctl` 操作硬件。详见 [service_spec.md §9](service_spec.md#9-应用层解耦规范)。

### Q: `device_find_by_label` 返回 `IS_ERR(dev)` 怎么排查？

1. DTS 未定义该 `label`：检查 `board.dts` 是否有 `label = "ws2812";`
2. DTS 中 `status = "disabled"`：`device_get_status(dev)` 返回 `DEVICE_STATUS_DISABLED`
3. dtc-lite 未生成设备表：确认 `BOARD_DTS` 变量在项目 CMakeLists.txt 中已设置
4. 驱动未注册：确认 `DRIVER_REGISTER(my_drv, "compatible", probe, remove)` 已存在
