# mini_tree 架构总览

> 本文档从宏观视角描述 mini_tree 的层次划分、核心数据流、启动时序与安全架构。

---

## 目录

1. [三层降维解耦拓扑](#1-三层降维解耦拓扑)
      - [构建系统的架构独立性](#14-构建系统的架构独立性)
2. [模块职责与依赖关系](#2-模块职责与依赖关系)
3. [启动时序 (两段式点火)](#3-启动时序-两段式点火)
4. [核心数据流](#4-核心数据流)
5. [配置系统](#5-配置系统)
6. [安全架构 (7 层防御)](#6-安全架构-7-层防御)
7. [跨平台验证矩阵](#7-跨平台验证矩阵)

---

## 1. 三层降维解耦拓扑

mini_tree 在物理上将"芯片 SDK / RTOS"与"应用业务"隔离：

```
┌──────────────────────────────────────────────────────────────┐
│                    用户应用层 (Host Apps)                      │
│   main.cpp / 业务 Service / 外设控制 / 算法控制流             │
│   ─── 仅依赖 mini_tree 接口库，不直接接触芯片 SDK 符号    │
├──────────────────────────────────────────────────────────────┤
│              mini_tree 核心中间件层 (Pure Middleware)          │
│  ┌───────────┬───────────────┬────────────────────────────┐  │
│  │ core/     │ board/        │ system_(c/cpp)/             │  │
│  │ EventBus  │ VFS 设备树    │ 两段式点火 / 看门狗 /       │  │
│  │ BufferPool│ 驱动 Probe    │ 闪存巡检 / 生命周期         │  │
│  └───────────┴───────────────┴────────────────────────────┘  │
├──────────────────────────────────────────────────────────────┤
│              OSAL 操作系统抽象层 (OS Abstraction)              │
│      osal_freertos.c  │  osal_rtthread.c  │  osal_null.c     │
│  统一接口: Task/Queue/Mutex/Spinlock/Time/Log/ISR 上下文     │
├──────────────────────────────────────────────────────────────┤
│              微内核与硬件芯片生态层 (Low-Level Engine)         │
│      FreeRTOS 内核    │  RT-Thread 内核    │  裸机 SysTick    │
│      (lib/FreeRTOS)   │  (lib/RT-Thread)   │  (无 RTOS)      │
└──────────────────────────────────────────────────────────────┘
```

### 层间契约

| 依赖方向 | 约束 |
|---------|------|
| 应用层 → 中间件层 | 应用只链接 `mini_tree` 接口库，不感知 OSAL 后端选择 |
| 中间件层 → OSAL | RTOS 调用走 `osal_*()`，不直接调用 FreeRTOS/RT-Thread API |
| OSAL → 内核 | OSAL 后端编译时通过 Kconfig 选且仅选一个，输出同名 `osal` 库 |
| 中间件层 → 硬件 | 不直接操作寄存器，通过 `hal/` 接口委派给平台实现 |

### 1.4 构建系统的架构独立性

传统单片机开发重度依赖 IDE 工程文件管理代码依赖。本架构采用不同的构建策略：

- **传统 IDE 的局限**：源文件管理、编译宏定义被绑定在不可读的 XML/私有格式文件中，导致多平台移植困难，且难以接入现代 CI/CD 自动化流程。
- **本架构的解法 (Kbuild 思想)**：构建权上收至 `CMakeLists.txt`，配合 `Kconfig` 菜单配置。代码只负责实现逻辑，由外部构建系统决定物理裁剪。

---

## 2. 模块职责与依赖关系

```
    ┌──────────┐   ┌──────────┐   ┌────────────┐
    │  core    │◄──│  board   │◄──│ system_cpp │
    │ EventBus │   │ VFS 设备树│   │ 或 system_c│
    │BufferPool│   │ 驱动 Probe│   │ 点火/监控  │
    │ 日志/黑盒│   └────┬─────┘   └─────┬──────┘
    └────┬─────┘        │                │
         │              │                │
    ┌────▼─────┐        │                │
    │   osal   │◄───────┼────────────────┘
    │ 三后端   │        │
    └────┬─────┘        │
         │              │
    ┌────▼─────┐   ┌────▼─────┐
    │   hal    │   │ bus/vfs/ │
    │ 平台分支 │◄──│ drivers  │
    │+dummy兜底│   └──────────┘
    └──────────┘
```

### 核心模块说明

| 模块 | 路径 | 职责 | 关键暴露 |
|------|------|------|---------|
| **osal** | `osal/` | OS 抽象层 — Task/Queue/Mutex/Spinlock/Time/Log (null / freertos / rtthread 三后端) | `osal.h` 统一 C 接口 |
| **core** | `core/` | EventBus 总线, BufferPool 无锁内存池, 日志, printf 输出 | `event_bus.hpp`, `buffer_pool.h` |
| **board** | `board/` | 设备树 DTS / dtc-lite 编译期解析, VFS 设备框架, Probe 引擎, dev_lifecycle, safety 停机 | `device.h`, `driver.h`, `board_config.h` |
| **bus** | `bus/` | 总线层 — dma / spi / uart 主从总线抽象 | `bus/include/` |
| **vfs** | `vfs/` | VFS 设备框架 — spi / gpio / uart 客户端 | `spi_vfs.c`, `uart_vfs.c`, `vfs-gpio.c` |
| **drivers** | `drivers/` | 外设驱动 — flash (w25q64) 等 | `w25q64_spi_drv.c` |
| **hal** | `hal/` | 硬件抽象实现层 — cpu / gpio / spi / uart / soc / system, 平台分支 (stm32 / ch32 / esp32) + `hal_if_dummy.c` 兜底 | `hal_*.h` |
| **system_cpp** | `system_cpp/` | C++ 两段式点火, RTC/TWDT 看门狗, Flash scrubber 巡检, 生命周期, task_manager, safe_state | `system_init.hpp` |
| **system_c** | `system_c/` | C 版 system 平替 (与 system_cpp 二选一, 均编入 `libmini_tree.a`) | `system_init.h` |
| **algorithm** | `algorithm/` | 通用算法组件 (环形 FIFO 等) | — |
| **lib** | `lib/` | 公共基础库 | — |

---

## 3. 启动时序 (两段式点火)

mini_tree 采用两段式点火架构，在 RTOS 启动前后分别完成不同级别的初始化。

```
main() 入口 (厂商启动壳: STM32 CubeMX / CH32 WCH / ESP32 app_main)
    │
    ├─ HAL_Init()                       芯片级: 时钟/RCC/FPU/中断向量
    ├─ SystemClock_Config()             时钟树配置
    ├─ pre_execution(MX_*_Init)         外设初始化钩子 (SPI/USART 等)
    │
    ├─ mini_tree_pre_os_init()          [Phase 1] C 包装 → MiniTree::System_Pre_OS_Init()
    │   ├── IRQ_DISABLE                 关全局中断, 阻断 ISR 抢跑未就绪状态
    │   ├── safe_state_check_bootloop() 启动循环保护 (≥5 次崩溃 → 永久锁死)
    │   ├── system_wdt_init_rtc()       RTC 硬件看门狗 (独立 32kHz 时钟)
    │   ├── device_tree_init()          设备树节点表初始化 (dtc-lite 编译期生成)
    │   ├── event_bus_init()            EventBus 队列创建 + post(EVENT_SYS_BOOT)
    │   └── g_system_os_initialized=true  SIOF 防御标志就绪
    │
    ├─ board_register_all_drivers()     兼容入口 (编译期 dtc-lite 已收录 probe 表)
    │   └─ 用户服务 init() 钩子点       (Phase 1 与 Phase 2 之间)
    │
    ├─ mini_tree_start_tasks()          [Phase 2] C 包装 → MiniTree::System_Start_Tasks()
    │   ├── event_bus_start()           EventBus 分发任务启动
    │   ├── board_driver_probe_all()    驱动探测 (设备树 ←→ HAL 匹配, 级联初始化/回滚)
    │   ├── system_wdt_init(3000)       TWDT 任务看门狗 (3 秒未喂狗)
    │   ├── system_scrubber_init/start  Flash 位腐烂巡检 (极低优先级任务)
    │   ├── safe_state_clear_bootloop() 启动循环计数器清除
    │   ├── event_bus_post(EVENT_SYS_READY)
    │   ├── event_bus_seal()            封表: 此后 subscribe() 返回 false
    │   └─ [AMP] hal_cpu_secondary_startup()  释放副核运行裸机
    │   └─ 用户任务创建钩子点           (Phase 2 与调度器之间)
    │
    ├─ system_init_complete()           释放全局中断 (IRQ_ENABLE)
    │
    ├─ OSAL_NULL? ──→ while(1) mini_tree_system_loop()   裸机: 喂狗轮询
    └─ 否则 ──────→ vTaskStartScheduler()                RTOS: 启动调度器
                     (CH32V307 封装为 task_rtos_main(), ESP32-S3 封装为 app_rtos_start())
```

### 阶段对比

| 阶段 | 时机 | 可访问资源 | 失败后果 |
|------|------|-----------|---------|
| Phase 1 | OS 启动前 | 仅 CPU + 栈 + BSS/Data | 未达安全状态则 Bootloop 防护介入 |
| Phase 2 | OS 启动后 | 多任务 + IPC + Queue | 单任务失败不影响已有服务 |

---

## 4. 核心数据流

### 4.1 EventBus 发布订阅

```
发布者                          EventBus                          订阅者
   │                              │                                 │
   │  osal_queue_send(evt)        │                                 │
   │ ─────────────────────────►   │                                 │
   │                              ├─ 封表后只读? ──→ 跳过 subscribe │
   │                              ├─ snapshot 订阅者列表 (持有锁)    │
   │                              ├─ SIOF 防御前? ──→ 静默丢弃      │
   │                              ├─ ISR 上下文? ──→ 直接分发        │
   │                              │  否则 ──→ Queue 投递分发任务     │
   │                              │                                 │
   │                              │  osal_queue_send(evt)           │
   │                              │ ───────────────────────────►   │
   │                              │                                 │
   │                              │◄── osal_queue_receive(evt) ───│

**安全加固**:
- **seal() 封表**: Phase 2 点火完成后冻结订阅者数组, 此后 subscribe() 调用返回 false.
  确保 ISR 中 dispatch 遍历的数组为只读静态表, 根除中断/任务读写踩踏.
- **SIOF 防御**: `g_system_os_initialized` 标志在 Phase 1 完成后置 true.
  防止 C++ 全局构造函数在 main() 前偷跑 post(), 静默丢弃而非崩溃.
```

### 4.2 BufferPool 无锁分配

```
分配请求
    │
    ├─ __builtin_ctz(bitmap)    前导零扫描 → 第一个空闲位
    ├─ CAS (原子比较交换)        竞争安全
    │   ├─ 成功 → 返回块索引
    │   └─ 失败 → 重试 (自旋)
    │
释放: bitmap |= (1u << index)   原子 OR, ISR 安全

**安全加固**:
- **DMA 对齐**: 池内存基址强制 32 字节对齐 (`BP_ALIGN_UP` + 超额分配),
  消除 DMA 引擎对奇数地址触发总线错误的风险. I2S/SPI DMA 地址安全.
```

### 4.3 中断延迟模型

EventBus 的 ISR → 订阅者通知路径:

```
硬件中断到达
    │
    ├─ ISR 入口 (平台 HAL)          0 µs
    ├─ osal_queue_send(m_queue)      ~1-3 µs  (FreeRTOS SMP 队列写入)
    ├─ dispatch 任务唤醒             上下文切换取决于当前任务优先级
    ├─ snapshot 锁定 + 拷贝           ~2 µs (24 订阅者, ARM CM4F 168MHz)
    ├─ 遍历匹配回调并执行             取决于回调数 × 匹配时间
    └─ 订阅者 callback(event)        应用层决定
```

关键约束:
- `osal_queue_send` 在 ISR 中从不阻塞, 复杂度 O(1) (固定深度环形队列)
- dispatch 任务优先级为框架内最高 (FreeRTOS: 30, RT-Thread: 1), 确保事件队列快速排空
- 订阅者回调在 dispatch 任务上下文中执行, 不得阻塞 (参见 EventBus WARNING 文档)

### 4.4 设备树 Probe 流程

```
board.dts
    │
    ├─ dtc-lite.py (编译期)      Kahn 拓扑排序
    │   ├─ depends-on 解析       BFS 依赖图
    │   └─ → board_driver.c     .rodata 结构数组
    │
    ├─ DRIVER_REGISTER(drv)      构造函数链接
    │   └─ → .driver_init_array  段
    │
    └─ board_probe_all()         运行时 Probe
        ├─ 级联初始化 (依赖顺序)
        ├─ 失败时联动回滚
        └─ 全失败 → enter_safe_state()
```

---

## 5. 配置系统

```
Kconfig 源文件 (.config)
    │
    ├─ kconfig_gui.py (图形化)   用户交互配置
    │
    ├─ genconfig.py (编译期)     Kconfig → config.h
    │   └─ → build/generated/kconfig/config.h
    │
    ├─ CMakeLists.txt 条件编译    add_subdirectory / compile_options
    │
    └─ 代码中 #ifdef CONFIG_*    编译期分支
```

### 配置菜单结构

| 顶层菜单 | 子选项 | 说明 |
|---------|--------|------|
| Platform | PLATFORM_ARM_CM3/CM4F/CM7/RISCV/POSIX | 目标 MCU 架构 |
| OSAL Backend | OSAL_FREERTOS / OSAL_RTTHREAD / OSAL_NULL | RTOS 选择 |
| System Backend | SYSTEM_CPP (C++17) / SYSTEM_C (C17) | system 实现语言 |
| System Log | SYS_LOG_USE_OSAL / ESP / PRINTF | 日志后端 |
| Build Options | BUILD_DISASM | 构建期反汇编 .lst 输出 |

---

## 6. 安全架构 (10 层防御)

| 层级 | 名称 | 触发条件 | 响应行为 |
|------|------|---------|---------|
| **L1** | Bootloop 防护 | 连续崩溃 ≥ 5 次 | RTC_DATA_ATTR 持久计数器，永久锁 Flash 写入 |
| **L2** | RTC 硬件看门狗 | CPU/总线卡死 | 独立 32kHz 时钟驱动，物理电源复位 |
| **L3** | Task WDT | 3 秒未喂狗 | Core Dump + 硬件复位 |
| **L4** | 栈水位监控 | 剩余 < 512 字节 | 两级预警 → 超限中断闭锁 |
| **L5** | Flash Scrubber | CRC 校验失配 | 后台逐页巡检 → 检测 Bit-Rot → 安全状态 |
| **L6** | EventBus SIOF 防御 | C++ 全局构造早产 | `g_system_os_initialized` 标志在 Phase 1 末尾置 true. `post()` 检查该标志, 未点火则静默丢弃 |
| **L7** | EventBus seal 封表 | ISR 读写踩踏 | Phase 2 末尾冻结订阅表, 禁止运行时 `subscribe()`. dispatch 遍历只读数组 |
| **L8** | Safe State | OSAL_PANIC / 服务 init 失败 | 关中断 + 锁调度器 + 死循环等维修 |
| **L9** | 反汇编审查 | CONFIG_BUILD_DISASM | 构建期指令级验证原子操作 |
| **L10** | **函数指针 ROM 固化** | 内存溢出 / 栈踩踏 | 编译期 `dtc-lite.py` 将 `s_probe_fns` 等核心分发数组置于 `.rodata` 只读段。物理上阻断裂表篡改和函数指针劫持，符合 IEC 61508 软件安全规约。 |

### 安全状态机

```
正常运行 ◄───── 喂狗/WDT 刷新
    │
    ├─ OSAL_PANIC() ──────► Safe State (关中断, 死循环)
    ├─ SIOF 拦截 ─────────► 静默丢弃 (不崩溃)
    ├─ seal 封表后 subscribe ─► 返回 false
    ├─ Task WDT 超时 ────► Core Dump → 硬件复位
    ├─ RTC WDT 超时 ─────► 物理电源复位
    ├─ Bootloop ×5 ──────► 永久锁 Flash → Safe State
    ├─ Scrubber CRC 失配 ─► Safe State (标记损坏页)
    └─ 栈超限 ───────────► 中断闭锁 → 硬件复位
```

## 7. 跨平台验证矩阵

> 本架构以 ARM Cortex-M 与 RISC-V RV32 为通用基准。主开发与验证环境为 **Linux**（原生 / Docker），同时作为通用中间件，**Windows 原生编译同样可用**。ESP32（Xtensa LX）作为异构架构通过原生 Linux / Windows 工具链接入（ESP-IDF 官方双端支持，不走 Docker），不参与下表通用基准验证。
>
> 下表"工具链"列标注的是 Linux 环境版本（ARM GCC 14.2.1 / RISC-V GCC 8.2.0 WCH）。Windows 原生编译使用 ARM GCC 13.3.1 (STM32CubeCLT 1.20.0) / RISC-V GCC 15.2.0 (MounRiver GCC15)，参考实现验证表见下方。

| 系统后端 | OSAL 后端 | 架构 | 工具链 | 状态 |
|---------|-----------|------|--------|------|
| system_cpp | FreeRTOS | ARM Cortex-M3 | ARM GCC 14.2.1 | 通过 |
| system_cpp | FreeRTOS | ARM Cortex-M4F | ARM GCC 14.2.1 | 通过 |
| system_cpp | FreeRTOS | ARM Cortex-M7 | ARM GCC 14.2.1 | 通过 |
| system_cpp | RT-Thread | ARM Cortex-M4F | ARM GCC 14.2.1 | 通过 |
| system_cpp | NULL | ARM Cortex-M3 | ARM GCC 14.2.1 | 通过 |
| system_cpp | FreeRTOS | RISC-V RV32 | RISC-V GCC 8.2.0 (WCH) | 通过 |
| system_c | FreeRTOS | ARM Cortex-M3 | ARM GCC 14.2.1 | 通过 |
| system_c | NULL | ARM Cortex-M3 | ARM GCC 14.2.1 | 通过 |
| system_c | FreeRTOS | RISC-V RV32 | RISC-V GCC 8.2.0 (WCH) | 通过 |
| host (MinGW) | — | POSIX | MinGW 8.1.0 | 通过 |

**参考实现验证 (Heterogeneous-Multicore 异构多核项目)**:

| 节点 | 架构 | OSAL | 工具链 | 平台 | 结果 |
|------|------|------|--------|------|------|
| STM32F407ZGT6 | ARM Cortex-M4F | NULL | ARM GCC 14.2.1 (系统包) | Linux native (WSL) | ✓ 85/85, FLASH 3.12%, RAM 6.70% |
| STM32F407ZGT6 | ARM Cortex-M4F | NULL | ARM GCC 13.3.1 (CubeCLT 1.20.0) | Windows native | ✓ FLASH 2.80%, RAM 6.71% |
| CH32V307 | RISC-V RV32 | FreeRTOS | RISC-V GCC (WCH 工具链) | Linux native (WSL) | ✓ 76/76, FLASH 16.05%, RAM 33.26% |
| CH32V307 | RISC-V RV32 | FreeRTOS | RISC-V GCC 15.2.0 (MounRiver GCC15) | Windows native | ✓ FLASH 15.55%, RAM 33.63% |
| ESP32-S3 | Xtensa LX7 | ESP-IDF | Xtensa GCC 16.1.0 (ESP-IDF v6.2) | Linux native (WSL) | ✓ 1186/1186, bin 319KB, 70% free |
| ESP32-S3 | Xtensa LX7 | ESP-IDF | Xtensa GCC (ESP-IDF v5.5.2) | Windows native | ✓ bin 304KB, 70% free |

### 工具链版本

> 下表区分各平台实际版本。Linux native (WSL) 为主开发环境，Docker 镜像内置版本用于 CI 复现。Windows 原生版本为兼容性验证实测值，作为通用中间件同样可用。`find_program` 三端自动探测，任一可用版本均可编译。

| 工具链 | Linux native (WSL) | Docker 版本 | Windows 原生版本 (实测) | C 标准 | C++ 标准 | 备注 |
|--------|-------------------|------------|----------------------|--------|---------|------|
| ARM GCC | 14.2.1 (系统包) | 14.2.1 | 13.3.1 (STM32CubeCLT 1.20.0) | C17 | C++17 | ARM Cortex-M 通用基准 |
| ARM Clang | — | 18+ | — | C17 | C++17 | ARM Cortex-M 备选 |
| RISC-V GCC | (WCH 工具链) | 8.2.0 (WCH) | 15.2.0 (MounRiver GCC15) | C17 | C++17 | RISC-V 通用基准 (WCH MounRiver) |
| MinGW | — | 8.1.0 | 8.1.0 | C17 | C++17 | Host 构建验证 |
| Xtensa GCC | 16.1.0 (ESP-IDF v6.2) | — | (ESP-IDF v5.5.2) | C17 | C++17 | ESP32 异构架构，走原生 Linux/Windows（不走 Docker） |

> **统一标准**: 所有工具链统一使用 C17 / C++17 标准（兼容 RISC-V GCC 8.2.0，避免 C23/C++23 在 WCH 工具链上的兼容性问题）。
