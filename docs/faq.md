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

1. 在 `hal_if/include/` 中声明操作表结构体
2. 在 `soc_port_` 中实现
3. 驱动通过 `device_t*` 获取 ops 并调用

### Q: 为什么需要在启动前 touch Singleton？

```cpp
(void)EventBus::getInstance();  // 预触摸
```

C++11 局部静态变量的首次初始化使用 `__cxa_guard_acquire` 互斥锁。如果在 ISR 中首次调用 `getInstance()`，互斥锁可能导致死锁。Phase 1 在 RTOS 启动前执行，自然完成了实例化。

### Q: 构建报错 `Target requires the language dialect "C23"`？

ARM GCC 13.3.1 使用 `-std=c2x` 而非 `-std=c23`。使用提供的 toolchain 文件自动处理：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
```

### Q: mini_tree 在小容量单片机 STM32F103C8T6 (64KB Flash) 上内存占用多少？

对应示例工程：[stm32f103c8t6_cubemx_test](https://github.com/H-000-H/mini-tree-example/tree/master/stm32f103c8t6_cubemx_test)（内存极限测试，CubeMX HAL 集成示范）。

以 STM32F103C8T6 + OSAL_NULL + system_cpp + GCC + newlib-nano 为例。以下数据区分 CubeMX HAL 驱动和 mini_tree 框架：

| 配置 | Flash | RAM | 说明 |
|------|-------|-----|------|
| **框架裸架构（无 CubeMX HAL）** | **~8 KB** | **~2 KB** | 仅 mini_tree 内核：board/core/system/osal/algorithm |
| **+ CubeMX HAL 全外设（未调用）** | **~14.8 KB (22%)** | **~3.3 KB** | ADC/UART/SPI/I2C/TIM/RTC 全使能但因未调用被 `--gcc-sections` 丢弃 |
| **+ 实际调用 UART/SPI/TIM** | **~20–28 KB** | **~4–5 KB** | 在代码中真正初始化使用这些外设，对应 HAL 函数被保留 |
| **starm-clang + newlib（对比）** | **~49 KB (76%)** | **~5.3 KB** | 缺少 `-ffunction-sections`，未使用的外设代码全部被链接 + 完整版 printf ~25 KB |
| **大量外设接入后实测（2025-06）** | **71544 B (109.17%)** | **8952 B (43.71%)** | FLASH 超限因 HAL 外设全面开启，RAM 仅用 43.71%，证明框架在小资源 MCU 上资源效率依然稳健 |

> 注意：以上 "CubeMX HAL" 指 ST 官方 HAL 库（`stm32f1xx_hal_uart.c` 等），**并非 mini_tree 驱动模块**。
> 框架自身只有 GPIO 和安全关断两个驱动，占用极小。如果你在业务代码里实际调用 `HAL_UART_Transmit` 等，
> 对应函数才会保留，Flash 随之增长——这是 HAL 的代价，与框架无关。

**关键结论：**

- mini_tree 框架自身仅 **~5.5 KB**，不存在"架构太大"的问题
- Flash 占用的主要来源是 **外设 HAL 驱动 + libc printf 实现**，与框架无关
- 开 HAL 外设越多，`-ffunction-sections` 越重要——否则未使用的 ADC/I2C/SPI/TIM 等全部被链接
- **推荐 GCC + newlib-nano**（`--specs=nano.specs`），在小芯片（≤ 64KB Flash）上优势明显：
  - nano printf 不支持浮点格式化，体积小
  - `--specs=nosys.specs` 提供弱符号 syscall 桩，无需手动实现
- starm-clang 并非比 GCC 差，而是 CubeCLT 环境缺少 picolibc，只能链接完整版 newlib；配合 picolibc + `-ffunction-sections` 也能达到接近 newlib-nano 的水平
- 对大容量芯片（几百 KB 以上），两种工具链均可自由选择

### Q: 多核配置注意事项？

任务通过 `osal_task_create_handle` 的 `core_id` 参数绑核；EventBus 跨核事件投递由 `osal_queue_send` 保证原子性；BufferPool 内存建议对齐到 cache line 避免伪共享。
