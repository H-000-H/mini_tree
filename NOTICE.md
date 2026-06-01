# mini_tree — 架构演进与重构全记录

> **摘要**: mini_tree 从 0.x 单体耦合架构 (sound_dsp_project) 经过多轮解耦重构，演变为当前支持 OSAL 三后端 + 双系统编译期隔离 + 工业级安全回路的通用中间件框架。本文档记录关键架构决策与演进路径。

---

## 目录

1. [核心架构决策](#核心架构决策)
2. [安全防御层次](#安全防御层次)
3. [重构全貌统计](#重构全貌统计)
4. [各轮次明细](#各轮次明细)
5. [已知限制](#已知限制)
6. [完整文件索引](#完整文件索引)

---

## 核心架构决策

| 决策 | 影响范围 | 说明 |
|------|----------|------|
| OSAL 三栖抽象层 | 全框架 | FreeRTOS / RT-Thread / NULL 统一 C 接口，上层无感切换 |
| 系统双后端 Kconfig 隔离 | system_cpp + system_c | 编译期选择 C++23 或 C23 实现，输出同名 system 库 |
| 编译期设备树 (dtc-lite) | board + drivers | Kahn 拓扑排序，零运行时解析，零硬编码引脚 |
| BSS 静态池分配 | 全驱动 | 零运行时堆分配，确定性内存布局 |
| BufferPool 无锁位图分配 | core | O(1) 分配/释放, ISR 安全, 零碎片, 32 字节 DMA 对齐 |
| EventBus 发布订阅总线 | core | 范围订阅 [id_min, id_max]，ISR 自适应，快照锁遍历，seal 封表，SIOF 防御 |
| Flash Scrubber CRC 巡检 | system | 后台极低优先级任务，电磁 Bit-Rot 防御 |
| 双重看门狗 (WDT + RTC) | system | SW Task WDT 防死循环 + HW RTC WDT 防总线死锁 |
| 500ms 锁超时原则 | 全框架 | 拒绝 OSAL_WAIT_FOREVER，防级联死锁 |
| 构建期反汇编追踪 | CMake | CONFIG_BUILD_DISASM 自动生成 .lst，指令级审查 |
| C23/C++23 标准 | 全框架 | ARM GCC 13 / RISC-V GCC 15 / MinGW 全工具链覆盖 |
| fno-exceptions / fno-rtti | C++ 全量 | 裸机 C++ 环境禁用异常与运行时类型识别 |
| Meyers Singleton 预触 | 启动阶段 | ISR 前完成 __cxa_guard_acquire，杜绝 ABI 死锁 |
| hal_pin_t 复合引脚 | hal_if | 32-bit port+pin 编码，ARM + RISC-V 跨架构移植 |

---

## 安全防御层次

| 层级 | 触发路径 | 行为 |
|------|----------|------|
| **L1** — Bootloop 防护 | 连续崩溃 ≥ 5 次 | RTC_DATA_ATTR 计数器，永久锁死 Flash 写入 |
| **L2** — RTC 硬件看门狗 | CPU/总线卡死 | 独立 32kHz 时钟物理电源复位，不受 APB 影响 |
| **L3** — Task WDT | 3 秒未喂狗 | Core Dump + 硬件复位 |
| **L4** — 栈水位监控 | 剩余 < 512 字节 | 两级预警，超限中断闭锁 |
| **L5** — Flash Scrubber | CRC 校验失配 | 后台逐页巡检, 检测到 Bit-Rot 进入安全状态 |
| **L6** — Safe State | OSAL_PANIC 或服务 init 失败 | 关中断 + 锁调度器 + 死循环等待维修介入 |
| **L6** — EventBus SIOF 防御 | C++ 全局构造函数早产 | Phase 1 末尾 `g_system_os_initialized = true`, post() 静默丢弃 |
| **L7** — EventBus seal 封表 | ISR 读写踩踏 | Phase 2 末尾冻结订阅表, dispatch 遍历只读数组 |
| **L8** — Safe State | OSAL_PANIC 或服务 init 失败 | 关中断 + 锁调度器 + 死循环等待维修介入 |
| **L9** — 反汇编审查 | CONFIG_BUILD_DISASM | 构建期指令级验证原子操作与死代码 |

---

## 重构全貌统计

| 阶段 | 涉及文件 | 主题 |
|------|----------|------|
| Phase 0 (原始状态) | — | sound_dsp_project 0.x 单体耦合架构审计 |
| 第 1 轮: OSAL 抽象 | osal/ 全量 | FreeRTOS 隔离，创建 osal.h 统一接口 |
| 第 2 轮: EventBus 解耦 | core/ | ISR 自适应 + subscribe 锁 + KillBus 优雅停机 |
| 第 3 轮: 系统服务 OSAL 化 | system_cpp/ | system_wdt / task_manager / system_scrubber 迁移 OSAL |
| 第 4 轮: BufferPool | core/ | 位图无锁 O(1) 分配器，零碎片 |
| 第 5 轮: OSAL_NULL 裸机后端 | osal/ | 原子操作 + 位掩码无锁环形队列平替 RTOS IPC |
| 第 6 轮: 工具链硬化 | CMake + cmake/ | C23/C++23 升级，fno-exceptions/fno-rtti，disasm 目标 |
| 第 7 轮: system 双后端 | system_c/ + system_cpp/ + Kconfig | C++ / C 编译期选择，Kconfig SYSTEM_BACKEND |
| **总计** | **~85+ 文件** | **7 轮重构 + 文档体系** |

---

## 各轮次明细

### Phase 0: 原始状态 (sound_dsp_project 0.x)

0.x 版本为 ESP32-S3 音频产品的单体架构，存在以下架构约束：

| 编号 | 约束 | 说明 |
|------|------|------|
| ARCH-001 | 业务耦合 | `system_runtime.cpp` 直接引用 AudioService / CloudService / UiService |
| ARCH-002 | 平台硬编码 | `esp_netif_init()` / `esp_event_loop_create_default()` 在框架层 |
| ARCH-003 | FreeRTOS 直调 | 无 OSAL 抽象，`xQueueSend` / `xTaskCreate` 散落各处 |
| ARCH-004 | 单阶段点火 | `SystemRuntime::start()` 一口气全做，无 Phase 1/Phase 2 分离 |
| ARCH-005 | C++11 标准 | 缺乏现代 C++ 特性支持 |
| ARCH-006 | 无 Kconfig | 全硬编码宏定义，无统一配置管理 |
| ARCH-007 | 无裸机支持 | 只能运行在 FreeRTOS 上 |

### 第 1 轮: OSAL 抽象层建立

**动机**: 切断框架对 FreeRTOS 的直接依赖，为多后端铺路。

**核心变更**:

```
之前:                              之后:
xQueueSend(m_queue, &evt, 0);      osal_queue_send(m_queue, &evt, 0);
vTaskDelay(ms);                     osal_delay_ms(ms);
xTaskCreatePinnedToCore(...);       osal_task_create_handle(...);
portDISABLE_INTERRUPTS();           osal_spinlock_lock(&lock);
__get_IPSR();                       osal_in_isr();
```

**文件**: `osal/include/osal.h` + `osal/src/osal_freertos.c` + `osal/src/osal_rtthread.c`

### 第 2 轮: EventBus 工业级加固

**动机**: EventBus 原始实现缺少 ISR 安全、subscribe 并发保护、优雅停机。

**变更清单**:

| 修复 | 之前 | 之后 |
|------|------|------|
| ISR 自适应 | `xQueueSend` 裸调用 | `osal_in_isr()` 分支分流 |
| subscribe 锁 | `m_count++` 无锁 | OSAL Mutex + 快照拷贝遍历 |
| 队列深度 | 16 槽 | 64 槽 (512B SRAM) |
| 优雅停机 | `vTaskDelete` 直接杀 | KillBus 信号 + 500ms 等待兜底 |

**涉及**: `core/include/event_bus.hpp` + `core/src/event_bus.cpp`

### 第 3 轮: 系统服务 OSAL 化

**动机**: system_wdt / task_manager / system_scrubber 直调 FreeRTOS API，无法跨后端。

**文件**:

| 文件 | 关键变更 |
|------|----------|
| `system_wdt.cpp` | `xTaskGetTickCount` → `osal_time_ms` |
| `task_manager.cpp` | `xTaskCreateStaticPinnedToCore` → `osal_task_create_handle` |
| `system_scrubber.cpp` | `xTaskCreate` → `osal_task_create_handle` |

### 第 4 轮: BufferPool 无锁内存池

**动机**: EventBus 投递大块数据需要零拷贝机制，避免堆分配碎片。

```c
// 位图 O(1) 分配器
static uint32_t bitmap_alloc(volatile uint32_t* mask)
{
    uint32_t old, new_mask;
    int bit;
    do {
        old = *mask;
        if (old == 0) return BP_MAX_BUFS;  // 池满
        bit = __builtin_ctz(old);          // 前导零扫描
        new_mask = old & ~(1u << bit);     // 位清零
    } while (!__atomic_compare_exchange_n(mask, &old, new_mask,
                0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    return (uint32_t)bit;
}
```

**特性**:
- O(1) 分配与释放 (`__builtin_ctz` + 位图)
- 原子 CAS 操作，ISR 安全
- 零碎片（固定块大小）
- 峰值使用跟踪

### 第 5 轮: OSAL_NULL 裸机后端

**动机**: 为纯前后台系统提供 OSAL 支持，覆盖 FreeRTOS / RT-Thread 无法部署的场景。

**核心平替技术**:

| RTOS 原语 | NULL 后端实现 |
|-----------|--------------|
| `xQueueSend` / `xQueueReceive` | 位掩码无锁环形队列 (`buf_mask = queue_len - 1`) |
| `xSemaphoreTake` / `xSemaphoreGive` | 原子变量忙等待 (`__atomic_load_n`) |
| `vTaskDelay` | SysTick 计数器递增，忙等待轮询 |
| `xTaskCreate` | 返回 NULL（裸机无多任务） |
| 中断上下文检测 | `s_isr_nest` 原子嵌套计数 |
| `portDISABLE_INTERRUPTS` | `osal_spinlock_lock` 原子自旋 |

**关键决策**: `osal_queue_receive` 传入 `OSAL_WAIT_FOREVER` 时，裸机下退化为轻量自旋等待。EventBus 分发任务在裸机模式下由主循环 `mini_tree_system_loop()` 驱动。

**新增文件**: `osal/src/osal_null.c` + `osal/include/osal_null.h`

### 第 6 轮: 工具链硬化与标准升级

**动机**: 项目需支持 ARM / RISC-V / MinGW 三工具链，统一 C23/C++23 标准。

**变更清单**:

| 项目 | 变更 |
|------|------|
| C 标准 | C11 → C23 (ARM: `-std=c2x`, RISC-V: `-std=c23`) |
| C++ 标准 | C++11 → C++23 |
| C++ 特性 | 全量 `-fno-exceptions -fno-rtti` |
| ARM 工具链 | ARM GCC 13.3.1 (STM32CubeCLT) |
| RISC-V 工具链 | RISC-V GCC 15.2.0 (xPack) |
| host 工具链 | MinGW 8.1.0 |
| 反汇编 | `CONFIG_BUILD_DISASM` → 自动生成 `build/disasm/*.lst` |

**CMake 实践**:

```cmake
# ARM GCC 13 不支持 -std=c23, 使用 c2x (C23 草案名称)
set(CMAKE_C_FLAGS "-mcpu=cortex-m3 -mthumb -std=c2x -Os -g" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-mcpu=cortex-m3 -mthumb -std=c++23 -Os -g" CACHE STRING "" FORCE)

# RISC-V GCC 15 原生支持 C23
add_compile_options(-march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany)
```

### 第 7 轮: System 双后端 Kconfig 隔离

**动机**: 部分行业交付要求 100% 纯 C 编译，C++ 的 Singleton 和异常栈不被允许。

**架构**:

```
Kconfig SYSTEM_BACKEND
  ├── SYSTEM_CPP (默认) ──→ system_cpp/ ──→ libsystem.a (C++23)
  └── SYSTEM_C      ──→ system_c/  ──→ libsystem.a (C23)
```

**system_cpp/** (C++ 实现):
- Meyers Singleton 生命周期
- `MiniTree::System_Pre_OS_Init()` / `MiniTree::System_Start_Tasks()` / `MiniTree::System_Loop()`
- 完整生命周期回调

**system_c/** (C 实现):
- 纯函数过程式 API
- `mini_tree_pre_os_init()` / `mini_tree_start_tasks()` / `mini_tree_system_loop()`
- 与 C++ 版本同名的 `libsystem.a` 输出，用户工程无需改链接配置

**C 兼容层修复**:
- `system_scrubber.hpp`: `<cstdint>` → `<stdint.h>`（C++ 独有的 `<cstdint>` 在 C 编译中不存在）
- `system_init.c`: 移除 C++ `EventBus::getInstance().poll()`，裸机循环仅喂狗

---

## 已知限制

| 限制 | 影响 | 缓解 |
|------|------|------|
| **OSAL_NULL 无多任务** | 裸机模式下 Task 创建返回 NULL | 提供 `mini_tree_system_loop()` 主循环轮询 |
| **BufferPool 固定块大小** | 小块浪费内存，大块无法分配 | 按典型消息 Size 配置 Kconfig |
| **无异步 DMA 事件驱动** | EventBus 分发为同步轮询 | RTOS 模式下由独立 Task 驱动 |
| **dtc-lite 非标准 DTS** | 不兼容 Linux DTS 语法 | 专为 MCU 裁剪，零运行时开销 |
| **Scrubber CRC 基线固化** | 出厂后无法更新基线 | OTA 时可重算并更新 |
| **仅 32-bit 架构验证** | 未在 64-bit 平台测试 | RISC-V 64 / ARMv8-A 需适配 |
| **无 Power Management** | 无 suspend/resume 框架 | OSAL 层预留接口，待实现 |

---

## 项目前身与演进路线

### 项目前身

[sound_dsp_project](https://github.com/H-000-H/sound_dsp_project)（基于 ESP32-S3 + LVGL 9.5 的音频 DSP 系统）是 mini_tree 的前驱原型。mini_tree 是在该音频项目过程中，将通用中间件逻辑从具体业务中抽离、泛化后重构而来。

**注意**: 该前驱项目是解耦前的单体架构, 未经过 RT-Thread 适配和系统级优先级审计, 其中发现的许多内存和优先级问题已在 mini_tree 中修复。**仅驱动层的 `DRIVER_REGISTER`、`device_t` 属性读取、goto 清理等写法可参考, 整体架构和服务层请以 mini_tree 当前版本为准。**

因此该前驱项目在目录结构和驱动挂载方式上与当前框架存在差异，反映了从原型到通用框架的演进过程。

## API 兼容性声明

本文档定义 mini_tree 各头文件的接口稳定性等级, 帮助用户工程评估升级风险.

| 等级 | 含义 | 适用范围 |
|------|------|----------|
| **稳定** | 语义和签名向后兼容, 主版本内不破坏 | `osal.h`, `device.h`, `driver.h`, `VFS.h`, `event_bus.hpp` (C 封装), `buffer_pool.h`, `safe_state.h` |
| **实验性** | 可能在大版本间变更, 会提前一个版本标记 deprecated | `task_manager.h`, `system_wdt.h`, `system_scrubber.h`, 各类 `hal_if/*.h`, `production_log.h` |
| **内部** | 不对外承诺, 随时可改 | `board_devtable.h`, `board_nodes.h`, `board_handles.h`, `task_config.h` (生成文件), `config.h` (Kconfig 产物) |

**稳定接口的变更规则**:
- 主版本号递增时可破坏兼容性
- 次版本号递增仅做向后兼容的扩展 (新增函数 / 新增字段在 struct 末尾)
- 补丁版本仅修复 bug, 不修改公开 API 签名和语义

用户工程应只依赖标记为 **稳定** 的接口. 实验性接口可在评估后使用, 升级时需关注 changelog.

### 后续规划

计划基于 mini_tree v1.0.0 标准接口推出两个参考工程：

- **mini_tree_bare_metal_demo** — 基于 `osal_null.c` 的纯裸机工程示例，展示在无 RTOS 条件下使用 dtc-lite 静态拓扑和位掩码环形队列构建前后台系统
- **mini_tree_rtos_fully_decoupled** — 基于 FreeRTOS/RT-Thread 双后端的参考工程示例，展示音频 Service、GUI Service 与 ConfigStore 之间通过 EventBus 异步通信的完整模式

---

## 第 8 轮: 硅片级安全加固 (最终轮)

**动机**: 覆盖所有已知嵌入式软件失效模式中尚未被框架防御的三个死角: ISR 与任务对订阅者数组的读写踩踏、DMA 引擎因内存不对齐触发的总线错误、以及 C++ 静态初始化顺序惨案 (SIOF) 导致的 main() 前崩溃。

**变更清单**:

| 修复 | 涉及文件 | 影响 |
|------|----------|------|
| EventBus seal 封表 | `event_bus.hpp`, `event_bus.cpp`, `system_init.cpp/.c` | Phase 2 点火后冻结订阅表, ISR dispatch 遍历只读数组 |
| BufferPool 32 字节对齐 | `buffer_pool.c` | 动态分配超额申请 + BP_ALIGN_UP 确保 32 字节对齐, DMA 安全 |
| SIOF 防御 | `event_bus.cpp`, `system_init.cpp/.c` | `g_system_os_initialized` 标志 Phase 1 后置 true, post 静默丢弃早产事件 |

**设计要点**:

- **seal 封表** — 非加锁方案, 通过架构契约而非运行时互斥切断读写踩踏路径. seal 后 subscribe 返回 false, 相当于在架构层面禁止运行时动态路由. 对于 ISR post 场景, 这意味着 dispatch 遍历的订阅者数组在 vTaskStartScheduler 启动后永远是只读的.

- **BufferPool 对齐策略** — 池内存分配采用超额申请 + 地址向上取整, 而非 `aligned_alloc` / `posix_memalign`, 避免对 C 运行时库的依赖. 对静态池 (`use_static = true`) 场景, 调用者自行确保 `static_mem` 对齐.

- **SIOF 防御的轻量实现** — 使用 `extern bool` 全局标志而非更复杂的 `call_once` / `pthread_once` 模式, 零堆栈和锁开销. 考虑到框架现有的 `m_queue` 判空已能防止崩溃, 该标志更多是作为可维护性层面的双保险, 确保未来新增的 EventBus 方法天然具备点火前拦截能力.

---



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
| `include/system_log.hpp` | 日志宏，三后端 (OSAL/ESP/PRINTF) |
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
| `README.md` | 架构总览 + 快速开始 |
| `USAGE.md` | 完整使用手册 (配置/集成/服务/移植/调试) |
| `NOTICE.md` | 架构演进与重构全记录 (本文) |
