# mini_tree 移植模板

本目录包含 mini_tree 框架所需的 HAL（硬件抽象层）函数桩实现。
将框架移植到新 MCU 平台时，以此作为起点。

## 移植步骤

1. **复制** 本目录到你的项目中（如 `my_project/hal_port/`）

2. **实现** 每个 `.c` 文件，调用你的 MCU SDK：
   - `hal_cpu.c` — 周期计数器、忙等待
   - `hal_flash.c` — Flash 读取（用于 bit-rot scrubber）
   - `hal_force_stop.c` — 紧急外设停机
   - `hal_storage.c` — 持久化配置存储（A/B 槽）
   - `hal_wdt.c` — RTC 看门狗 + 任务看门狗
   - `hal_platform_safety.c` — 故障 LED/蜂鸣器硬件锁定

3. **重命名** `CMakeLists.txt` 中的库名，从 `your_hal_port` 改为实际名称

4. **更新项目 CMakeLists.txt**：

   ```cmake
   add_subdirectory(path/to/mini_tree)
   add_subdirectory(path/to/hal_port)    # 你的 HAL 实现
   target_link_libraries(my_app PRIVATE mini_tree hal_port)
   ```

5. **提供 FreeRTOSConfig.h**，放在项目的 include 路径中

6. **编写 main.c**：

   ```c
   #include "system_init.hpp"
   
   int main(void) {
       platform_init();                // MCU HAL 初始化
       MiniTree::System_Pre_OS_Init(); // 框架阶段 1
       register_my_drivers();          // 注册 hal_* 到 VFS
       MiniTree::System_Start_Tasks(); // 框架阶段 2
       my_app_init();                  // 应用任务
       vTaskStartScheduler();
   }
   ```

## 设备树 (DTS) 适配说明

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

关键结论：
- **修改引脚只需改 `board.dts` 中的属性值，无需动任何 `.c` / `.h` 函数**
- GD32 与 STM32 共用同一套 DTS 适配模式，无缝切换
- 不同开发风格 (HAL/LL/SPL/寄存器/GD32 库) 对同一 DTS 属性的读取方式完全一致

### 移植步骤

1. 在 `board/<platform>/board.dts` 中定义外设节点（参考两个实例顶部的 DTS 注释）
2. 填写实际的引脚号、时钟频率等属性
3. `device_find_by_label()` + `device_get_prop_int()` 在驱动中读取
4. 如需变更引脚，只改 DTS 属性值，不碰 C 代码
