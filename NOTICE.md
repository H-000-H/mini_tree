# mini_tree — 架构演进与重构全记录

> **摘要**: 本文档记录 mini_tree 的关键架构决策与演进路径。

---

## 目录

1. [核心架构决策](#核心架构决策)
2. [安全防御层次](#安全防御层次)
3. [重构全貌统计](#重构全貌统计)
4. [各轮次明细](#各轮次明细)
5. [已知限制](#已知限制)

---

## 核心架构决策

| 决策 | 影响范围 | 说明 |
|------|----------|------|
| OSAL 三栖抽象层 | 全框架 | FreeRTOS / RT-Thread / NULL 统一 C 接口，上层无感切换 |
| 系统双后端 Kconfig 隔离 | system_cpp + system_c | 编译期选择 C++17 或 C17 实现，输出同名 mini_tree 静态库 |
| 编译期设备树 (dtc-lite) | board + drivers | Kahn 拓扑排序，零运行时解析，零硬编码引脚 |
| BSS 静态池分配 | 全驱动 | 零运行时堆分配，确定性内存布局 |
| BufferPool 无锁位图分配 | core | O(1) 分配/释放, ISR 安全, 零碎片, 32 字节 DMA 对齐 |
| EventBus 发布订阅总线 | core | 范围订阅 [id_min, id_max]，ISR 自适应，快照锁遍历，seal 封表，SIOF 防御 |
| Flash Scrubber CRC 巡检 | system | 后台极低优先级任务，电磁 Bit-Rot 防御 |
| 双重看门狗 (WDT + RTC) | system | SW Task WDT 防死循环 + HW RTC WDT 防总线死锁 |
| 500ms 锁超时原则 | 全框架 | 拒绝 OSAL_WAIT_FOREVER，防级联死锁 |
| 构建期反汇编追踪 | CMake | CONFIG_BUILD_DISASM 自动生成 .lst，指令级审查 |
| C17/C++17 标准 | 全框架 | Docker: ARM GCC 14.2.1 / RISC-V GCC 8.2.0 (WCH)；Windows 原生: ARM GCC 13.3.1 (CubeCLT) / RISC-V GCC 15.2.0 (MounRiver)；MinGW 全工具链覆盖 |
| fno-exceptions / fno-rtti | C++ 全量 | 裸机 C++ 环境禁用异常与运行时类型识别 |
| Meyers Singleton 预触 | 启动阶段 | ISR 前完成 __cxa_guard_acquire，杜绝 ABI 死锁 |
| hal_pin_t 复合引脚 | hal/ | 32-bit port+pin 编码，ARM + RISC-V 跨架构移植 |

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
| **L7** — EventBus SIOF 防御 | C++ 全局构造函数早产 | Phase 1 末尾 `g_system_os_initialized = true`, post() 静默丢弃 |
| **L8** — EventBus seal 封表 | ISR 读写踩踏 | Phase 2 末尾冻结订阅表, dispatch 遍历只读数组 |
| **L9** — 反汇编审查 | CONFIG_BUILD_DISASM | 构建期指令级验证原子操作与死代码 |

---

## 重构全貌统计

| 阶段 | 涉及文件 | 主题 |
|------|----------|------|
| 第 1 轮: OSAL 抽象 | osal/ 全量 | FreeRTOS 隔离，创建 osal.h 统一接口 |
| 第 2 轮: EventBus 解耦 | core/ | ISR 自适应 + subscribe 锁 + KillBus 优雅停机 |
| 第 3 轮: 系统服务 OSAL 化 | system_cpp/ | system_wdt / task_manager / system_scrubber 迁移 OSAL |
| 第 4 轮: BufferPool | core/ | 位图无锁 O(1) 分配器，零碎片 |
| 第 5 轮: OSAL_NULL 裸机后端 | osal/ | 原子操作 + 位掩码无锁环形队列平替 RTOS IPC |
| 第 6 轮: 工具链硬化 | CMake + cmake/ | C17/C++17 标准统一，fno-exceptions/fno-rtti，disasm 目标 |
| 第 7 轮: system 双后端 | system_c/ + system_cpp/ + Kconfig | C++ / C 编译期选择，Kconfig SYSTEM_BACKEND |
| 第 8 轮: 硅片级安全加固 | core/ + system_cpp/ | seal 封表 / DMA 对齐 / SIOF 防御 |
| **总计** | **~85+ 文件** | **8 轮重构 + 文档体系** |

---

## 各轮次明细

### 第 1 轮: OSAL 抽象层建立

**动机**: 切断框架对 FreeRTOS 的直接依赖，为多后端铺路。

**核心变更**:

```
之前:                              之后:
xQueueSend(m_queue, &evt, 0);      osal_queue_send(m_queue, &evt, 0);
vTaskDelay(ms);                     osal_delay_ms(ms);
xTaskCreatePinnedToCore(...);       task_manager_create_task(...);
portDISABLE_INTERRUPTS();           osal_spinlock_lock(&lock);
__get_IPSR();                       osal_in_isr();
```

> 注: 应用层业务任务通过 `task_manager_create_task()`（封装 `osal_task_create_handle()` + 自动 TWDT 订阅）创建；底层 OSAL API `osal_task_create_handle()` 仅在框架内部使用。

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
| `task_manager.cpp` | `xTaskCreateStaticPinnedToCore` → `task_manager_create_task` (内含 `osal_task_create_handle`) |
| `system_scrubber.cpp` | `xTaskCreate` → `task_manager_create_task` |

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

**动机**: 项目需支持 ARM / RISC-V / Xtensa 三工具链，统一 C17/C++17 标准。

**变更清单**:

| 项目 | 变更 |
|------|------|
| C 标准 | C11 → C17 (`-std=c17`, GCC 扩展开启 `CMAKE_C_EXTENSIONS ON`) |
| C++ 标准 | C++11 → C++17 (`-std=c++17`, GCC 扩展开启 `CMAKE_CXX_EXTENSIONS ON`) |
| C++ 特性 | 全量 `-fno-exceptions -fno-rtti` |
| ARM 工具链 | Docker: ARM GCC 14.2.1 / Windows 原生: ARM GCC 13.3.1 (STM32CubeCLT 1.20.0) |
| RISC-V 工具链 | Docker: RISC-V GCC 8.2.0 / Windows 原生: RISC-V GCC 15.2.0 (WCH MounRiver Studio GCC15) |
| Xtensa 工具链 | Xtensa GCC (ESP-IDF) |
| host 工具链 | MinGW 8.1.0 (host unit test) |
| 反汇编 | `CONFIG_BUILD_DISASM` → 自动生成 `build/<preset>/disasm/*.lst` |

**CMake 实践**:

```cmake
# CH32V307 CMakeLists.txt (RISC-V, GCC 8.2.0+/15.2.0)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# RISC-V 架构参数（GCC 8.2.0+/15.2.0 兼容）
set(CH307_ARCH_OPTS
    -march=rv32imac
    -mabi=ilp32
    -mcmodel=medany
    -msmall-data-limit=8
    -mno-save-restore
)

# C++ 裁剪: 全模块禁用 RTTI / 异常
target_compile_options(mini_tree PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)
```

### 第 7 轮: System 双后端 Kconfig 隔离

**动机**: 部分行业交付要求 100% 纯 C 编译，C++ 的 Singleton 和异常栈不被允许。

**架构**:

```
Kconfig SYSTEM_BACKEND
  ├── SYSTEM_CPP (默认) ──→ system_cpp/ ──→ mini_tree 静态库 (C++17)
  └── SYSTEM_C      ──→ system_c/  ──→ mini_tree 静态库 (C17)
```

**system_cpp/** (C++ 实现):
- Meyers Singleton 生命周期
- 完整 `mini_tree_pre_os_init()` / `mini_tree_start_tasks()` / `system_init_complete()` 入口
- 完整生命周期回调（EventBus seal / Task WDT 订阅）

**system_c/** (C 实现):
- 纯函数过程式 API
- 同名 `mini_tree` 静态库输出，用户工程无需改链接配置
- 移除 C++ `EventBus::getInstance().poll()`，裸机循环仅喂狗

**两后端共享 C API**:

```c
// 入口 (system_cpp 或 system_c 都提供)
void mini_tree_pre_os_init(void);          // Phase 1: Pre-OS Init
void mini_tree_start_tasks(void);          // Phase 2: Start Tasks
void system_init_complete(void);           // 标记 Phase 2 完成, seal EventBus
bool mini_tree_system_loop(void);          // 裸机主循环 (OSAL_NULL)
```

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
| **dtc-lite Linux DTS 兼容** | 低版本未实现 overlay / reg 分组 | 自 v1.6.0 起支持 `&label` overlay、`#address-cells`/`#size-cells` reg 分组、`/include/` 指令。详见 `docs/driver_guide.md` 第 7.3 节差异表 |
| **Scrubber CRC 基线固化** | 出厂后无法更新基线 | OTA 时可重算并更新 |

---

> 本项目衍生自一个 ESP32-S3 音频 DSP 工程，通用中间件逻辑经抽离、泛化后独立为此框架。

---

## 第 8 轮: 硅片级安全加固 (最终轮)

**动机**: 覆盖已知嵌入式软件失效模式中尚未被框架防御的三个死角: ISR 与任务对订阅者数组的读写踩踏、DMA 引擎因内存不对齐触发的总线错误、以及 C++ 静态初始化顺序惨案 (SIOF) 导致的 main() 前崩溃。

**变更清单**:

| 修复 | 涉及文件 | 影响 |
|------|----------|------|
| EventBus seal 封表 | `event_bus.hpp`, `event_bus.cpp`, `system_init.cpp/.c` | Phase 2 点火后冻结订阅表, ISR dispatch 遍历只读数组 |
| BufferPool 32 字节对齐 | `buffer_pool.c` | 动态分配超额申请 + BP_ALIGN_UP 确保 32 字节对齐, DMA 安全 |
| SIOF 防御 | `event_bus.cpp`, `system_init.cpp/.c` | `g_system_os_initialized` 标志 Phase 1 后置 true, post 静默丢弃早产事件 |

**设计要点**:

- **seal 封表** — 非加锁方案, 通过架构契约而非运行时互斥切断读写踩踏路径. seal 后 subscribe 返回 false, 相当于在架构层面禁止运行时动态路由. 对于 ISR post 场景, 这意味着 dispatch 遍历的订阅者数组在 vTaskStartScheduler 启动后是只读的.

- **BufferPool 对齐策略** — 池内存分配采用超额申请 + 地址向上取整, 而非 `aligned_alloc` / `posix_memalign`, 避免对 C 运行时库的依赖. 对静态池 (`use_static = true`) 场景, 调用者自行确保 `static_mem` 对齐.

- **SIOF 防御的轻量实现** — 使用 `extern bool` 全局标志而非更复杂的 `call_once` / `pthread_once` 模式, 零堆栈和锁开销. 考虑到框架现有的 `m_queue` 判空已能防止崩溃, 该标志更多是作为可维护性层面的双保险, 确保未来新增的 EventBus 方法天然具备点火前拦截能力.
