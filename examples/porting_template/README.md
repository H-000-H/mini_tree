# Porting Template for mini_tree

This directory contains stub implementations of the HAL (Hardware Abstraction Layer)
functions that `mini_tree` expects. Use this as a starting point when porting the
framework to a new MCU platform.

## How to Port

1. **Copy** this directory into your project (e.g., `my_project/hal_port/`).

2. **Implement** each `.c` file with your MCU's SDK calls:
   - `hal_cpu.c` — cycle counter, busy-wait
   - `hal_flash.c` — flash read for bit-rot scrubber
   - `hal_force_stop.c` — emergency peripheral shutdown
   - `hal_storage.c` — persistent config storage (A/B slots)
   - `hal_wdt.c` — RTC watchdog + task watchdog
   - `hal_platform_safety.c` — fault LED/buzzer hardware lock

3. **Rename** the library in `CMakeLists.txt` from `your_hal_port` to your
   actual library name.

4. **Update your project's CMakeLists.txt**:

   ```cmake
   add_subdirectory(path/to/mini_tree)
   add_subdirectory(path/to/hal_port)    # your HAL implementation
   target_link_libraries(my_app PRIVATE mini_tree hal_port)
   ```

5. **Provide FreeRTOSConfig.h** in your project's include path.

6. **Write your main.c**:

   ```c
   #include "system_init.hpp"
   
   int main(void) {
       platform_init();                // your MCU HAL init
       MiniTree::System_Pre_OS_Init(); // framework phase 1
       register_my_drivers();          // register your hal_* with VFS
       MiniTree::System_Start_Tasks(); // framework phase 2
       my_app_init();                  // your app tasks
       vTaskStartScheduler();
   }
   ```

## Device Tree (DTS) 适配说明

本框架通过设备树实现 **配置与实现解耦**，移植时只需在板级 `board.dts` 中声明外设的引脚/时钟属性，
驱动代码无需修改。

### 继承覆盖模式

```
基节点: 默认值硬编码在 C 中 (如 DTS_DEF_UART_TX_PIN)
覆盖:   device_find_by_label("uart0") → 读取 DTS 节点属性
        属性不存在 → 保持默认值
```

### 实例参考

| 文件 | 说明 |
|------|------|
| `hal_init_stm32.c` | STM32 四种开发风格 (HAL/LL/SPL/寄存器) 统一接入 DTS |
| `hal_init_gd32.c` | GD32 标准外设库风格接入 DTS |

两个实例展示了同一场景 —— **SPI 四线整体换引脚**：

```dts
spi0: spi@0 {
    mosi  = <3>;         /* 改这里, 无需动 .c */
    miso  = <4>;         /* 改这里, 无需动 .c */
    sclk  = <5>;         /* 改这里, 无需动 .c */
    cs-gpios = <6>;      /* 改这里, 无需动 .c */
};
```

关键结论:
- **修改引脚只需改 `board.dts` 中的属性值，无需动任何 `.c` / `.h` 函数**
- GD32 与 STM32 共用同一套 DTS 适配模式，无缝切换
- 不同开发风格 (HAL/LL/SPL/寄存器/GD32 库) 对同一 DTS 属性的读取方式完全一致

### 移植步骤

1. 在 `board/<platform>/board.dts` 中定义外设节点 (参考两个实例顶部的 DTS 注释)
2. 填写实际的引脚号、时钟频率等属性
3. `device_find_by_label()` + `device_get_prop_int()` 在驱动中读取
4. 如需变更引脚，只改 DTS 属性值，不碰 C 代码

## ESP32 Quick Start

If you're targeting ESP32, the complete HAL implementation is maintained
separately at: `https://github.com/your-org/esp32-hal-port` (or similar).
