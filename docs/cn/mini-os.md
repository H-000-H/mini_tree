# mini-os 内核（lib/mini-os）

> mini_tree 自研的最小实时内核（Cortex-M 专用，freestanding、无 libc 依赖），也是 OSAL 四后端中体积最小的 RTOS 后端。本文介绍其调度器、时间轮、同步原语、内存管理与移植层，以及它与 mini_tree 的集成接线。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 需要理解/调试内核行为、做板级接线或内核裁剪的开发者 |
| **前置** | [architecture.md](architecture.md)（分层）、[osal_switching.md](osal_switching.md)（后端切换）、[getting_started.md](getting_started.md)（Kconfig 双轨） |
| **源码** | `lib/mini-os/`（`src/` 内核 · `inc/` 头 · `arch/arm/cortex-m/port/` 移植层） |
| **License** | Apache-2.0（设计参考 FreeRTOS / RT-Thread / Zephyr / Linux） |

---

## 目录

1. [定位与特性总览](#1-定位与特性总览)
2. [调度器](#2-调度器)
3. [线程与同步原语](#3-线程与同步原语)
4. [定时器](#4-定时器)
5. [内存管理](#5-内存管理)
6. [移植层（port）](#6-移植层port)
7. [配置体系](#7-配置体系)
8. [与 mini_tree 的集成](#8-与-mini_tree-的集成)
9. [内存占用实测](#9-内存占用实测)
10. [独立构建](#10-独立构建)

---

## 1. 定位与特性总览

mini-os 是仓库内自研的最小 RTOS 内核，设计目标：

- **最小体积**：OSAL 四后端中 text/bss 最小（见 [memory_footprint.md](memory_footprint.md) §4）；
- **freestanding**：不依赖任何 libc（只用 `stddef.h` 等自足头文件），堆为自有实现；
- **Cortex-M 专用**：port 覆盖 M0/M0+/M3/M4/M7（RISC-V port 为空壳，ESP32 为 Xtensa 不可用）；
- **GCC/Clang 工具链**：内核使用 GNU 扩展（`__attribute__((constructor))` 等），Keil ARMCC 不能直接使用。

| 子系统 | 能力 |
| :--- | :--- |
| 调度 | 32 级抢占 + 就绪位图 O(1) + 同优先级链表轮转（时间片可选，默认关） |
| 线程 | 动/静态创建、删除、挂起/恢复、动态改优先级（带 PI 回滚）、退出回调、idle 钩子；detach/join 可选 |
| 同步 | 计数/二值信号量（可互转）、递归互斥锁 + 优先级继承（链式传播）、32 位事件组（可选） |
| 通信 | 定长消息队列（阻塞收发 + ISR 变体） |
| 定时器 | HARD（tick/ISR 上下文直接跑）+ SOFT（专用服务线程跑）；独立定时器时间轮 |
| 内存 | first-fit 堆（split + 相邻合并 + magic 防双重释放）+ 可选 slab；堆来自链接脚本 |
| 临界区 | PRIMASK 全屏蔽或 BASEPRI 阈值（`MINI_OS_IRQ_MAX_SYSCALL_PRIORITY`） |
| 工程 | 无 libc、C11 `_Generic` 原子兜底（GCC/Clang builtin 双路径） |

---

## 2. 调度器

核心实现在 `lib/mini-os/src/schedule.c`（约 470 行）。

### 2.1 就绪位图 + O(1) 选级

- 32 级优先级，**数字越小越优先**（同 RT-Thread 语义，与 FreeRTOS 相反）；
- 就绪位图 `g_priority` 是单个 32 位字，`mini_os_get_highest_priority()` 用 CTZ（计尾零）一条指令选出最高优先级，O(1)；
- 每级一张双向链表 `g_ready_running_list[MINI_OS_PRIORITY]`，同优先级线程在链表内轮转（哨兵自指约定：后继回卷时跳过哨兵）。

### 2.2 线程时间轮

线程延时 / 同步超时用分层时间轮（`s_wheel[MINI_OS_TICK_WHEEL]`，槽数为 2 的幂，默认 32）：

- 插入：`slot = (current + ticks) & MASK`，`round = (ticks - 1) >> CTZ(WHEEL)`（跨多圈时先数 round）；
- `mini_os_sync_wait_park()` 用 **双节点** 同时挂两处：`wait_node` 挂同步对象等待列表、`list_node` 挂时间轮；唤醒侧做双 unlink，避免二次唤醒；
- SysTick handler 的处理顺序：关中断 → 线程时间轮到期唤醒 → 时间片轮转（若开 `MINI_OS_TIME_SLICE`）→ tick 自增 → 定时器轮。

### 2.3 中断与上下文切换的优先级安排

| 异常 | 优先级 | 作用 |
| :--- | :--- | :--- |
| PendSV | `0xFF`（最低） | 上下文切换（`port.S`，PSP + 特权模式） |
| SysTick | `0xFE` | tick 驱动（时间轮 / 时间片 / 定时器轮） |

- ISR 内的唤醒**不主动切上下文**：`mini_os_schedule_yield_isr()` 内部自查就绪位图，仅当有更高优先级就绪才置位 PendSV；
- `*_isr` 变体 API（`mini_os_semaphore_post_isr` 等）均遵守此约定，OSAL 的 `osal_yield_from_isr()` 即转发该调用。

### 2.4 关键线程与构造函数优先级

| 实体 | 优先级 | 说明 |
| :--- | :--- | :--- |
| idle 线程 | `MINI_OS_PRIORITY - 1` | 固定最低级，附带 MSP 栈哨兵巡检 |
| SOFT 定时器服务线程 | `MINI_OS_PRIORITY - 2` | 比 idle 高一级（避免与 idle 抢最低级），惰性创建 |
| FPU 使能构造函数 | 100 | `0-100` 保留给系统构造函数，最先跑 |
| CPUID 探测构造函数 | 101 | 见 §6 |
| MSP 栈哨兵构造函数 | 102 | 见 §6 |
| idle 线程自初始化 | 105 | |
| 按名注册表（线程/信号量/互斥锁） | 110-112 | 仅 `MINI_OS_FIND_BY_NAME` 开启时 |
| 定时器模块自初始化 | 113 | |

---

## 3. 线程与同步原语

### 3.1 信号量（semaphore.c）

- 计数 / 二值信号量，二者可互相转换；`mini_os_semaphore_post_isr()` 为 ISR 安全变体。

### 3.2 互斥锁（mutex.c）—— 优先级继承（PI）

mini-os 的 PI 是 **per-thread 跟踪** 模型：

- 持锁者把调用时的优先级存进 `base_priority`，所持每把 mutex 链入线程的 `hold_list`（经 `mutex->hold_node`）；
- 有效优先级 = `min(base_priority, 各持锁 mutex 上最高等待者的优先级)`；
- **链式传播**：A 等 B 的锁、B 又等 C 的锁时，提升沿 `wait_mutex` 回指针向上传播，深度由 `MINI_OS_MUTEX_PI_CHAIN_MAX` 限制（防环）；
- 线程动态改优先级（`mini_os_thread_set_priority`）会做 PI 回滚，避免与继承状态冲突。

其他要点：

- mutex 内嵌 binary semaphore（`count = max = 1`），支持递归锁（`is_recuring` + depth，可选）；
- `mini_os_mutex_delete()` 走 `kill_waiters`：把等待者 `wait_done = FALSE`（上层收到 TIMEOUT 语义）并清其 `wait_mutex` 回指针，防止 use-after-free。

### 3.3 事件组（event.c，可选）

- 32 位标志，OR / WHOLE（全部置位）等待语义，可配自动清零；
- 开关 `MINI_OS_EVENT`（自身默认关，但 `OSAL_EVENT`（默认 y）会 select 它，见 §7）；
- 关闭时 `event.h`/`event.c` 编译为空，且 `event.c` 直接从编译单元列表摘除（连空对象都不产生）。

---

## 4. 定时器

实现见 `lib/mini-os/src/timer.c`（约 450 行），核心设计是 **独立定时器时间轮**（`s_timer_wheel`，不碰 TCB），与 §2.2 的线程时间轮解耦：

| 类型 | 回调上下文 | 说明 |
| :--- | :--- | :--- |
| HARD 定时器 | tick / ISR 上下文直接跑 | 回调必须极短（遵守 [fast_path.md](fast_path.md) 红线） |
| SOFT 定时器 | 专用服务线程跑 | 回调挂 `s_soft_pending`，服务线程消费 |

服务线程细节：

- 静态 TCB + 静态栈（`MINI_OS_TIMER_THREAD_STACK_SIZE`，默认 512 B），**惰性创建**——第一个 SOFT 定时器 start 时才拉起（构造函数阶段调度器尚未启动，无法建线程）；
- 优先级 `MINI_OS_PRIORITY - 2`：永不让位业务线程，也不与 idle 共享最低级。

时间轮插入的特殊规则：目标 slot 恰为当前服务 slot 时**头插**并修正 round，防止同一 tick 内双触发；回调执行前先 re-arm（周期）或 disarm（单次），因为回调可能 stop/delete 自己，返回后不再触碰 timer 结构。

---

## 5. 内存管理

实现见 `lib/mini-os/src/memory.c`（约 840 行）+ `inc/mem_heap.h`：

- **分配模型**：`malloc/free` 风格 —— first-fit 查找 + split（剩余 ≥ 块头 + 16 字节才切）+ **相邻空闲块合并**（free 时全表扫描，O(n) 但碎片率更低）；
- **防双重释放**：块头 magic 双值 —— `0xA5A5A5A5`（ALLOC 态）/ `0x5A5A5A5A`（FREE 态），重复 free / 越界踩 magic 立即可查；
- **O(1) 空闲量查询**：`free_size` 维护为增量值；
- **堆来源**：链接脚本 `lib/mini-os/mini-os-heap.ld` 提供 `__mini_os_heap_start` / `__mini_os_heap_end`，堆位于 bss 之后、MSP 栈之前——**不占 bss**（对 [memory_footprint.md](memory_footprint.md) §4 的 bss 口径比较很重要）；
- **可选 slab**：定长类 16/32/64/128/256（`MINI_OS_SLAB_LONG` 再加 512），页大小 2 KiB（2 的幂 ≤ 64 KiB）；页可从堆按 `1/MINI_OS_SLAB_PROPORTION`（默认 1/4）划出，或用 `MINI_OS_SLAB_STATIC` 指定独立静态区；超出最大类的请求直走 free list。

> 与 mini_tree 其他后端不同：`osal_calloc/osal_free`（mini-os 后端）走 mini-os 自有堆而非 libc，因此 RT-Thread/FreeRTOS 后端的 `s_rtt_heap`/`ucHeap` 式 bss 大数组在这里不存在。

---

## 6. 移植层（port）

`lib/mini-os/arch/arm/cortex-m/port/`（`port.c` 约 160 行 C + `port.S` 约 510 行汇编；`arch/risc-v/port` 为空壳）。

### 6.1 SVC 回调机制

- `g_svc_cb` / `g_svc_arg` 两个全局变量由 **port.S 直接读取**（非 static），`mini_os_svc_set_callback()` 安装；
- `mini_os_svc_get_num()` 从压栈帧恢复 SVC 立即数：Thumb SVC 指令 2 字节宽，压栈 PC 指向下一条指令，`[pc-2] = 0xDF` 操作码、`[pc-1] = imm8`。

### 6.2 CPUID 探测（fail-fast）

port 汇编是核特定的，配错核会直接破坏上下文。启动构造函数（优先级 101）读 `SCB->CPUID`（0xE000ED00）bits[15:4] 的 part number 并与 `MINI_OS_ARCH` 比对：

| part number | 核 |
| :--- | :--- |
| `0xC20` / `0xC60` | M0 / M0+（ARMv6-M 孪生，同组接受） |
| `0xC23` | M3 |
| `0xC24` | M4 |
| `0xC27` | M7 |

不匹配 → 死循环停机（宁可 fail-fast 也不调试损坏的 PendSV）。

### 6.3 FPU 与栈哨兵

- **FPU 使能**（M4F/M7 且 `MINI_OS_USE_FPU`，默认开）：构造函数（优先级 100）置 CPACR CP10/CP11 全访问 + `dsb/isb`，必须先于任何线程运行；PendSV 对 s16-s31 做惰性保存；
- **MSP 栈溢出哨兵**（`MINI_OS_STACK_OVERFLOW_CHECK`，默认关）：构造函数（优先级 102）把 magic `0x060815` 写到 `__mini_os_heap_end`（堆与 MSP 栈的交界、溢出第一受害者），idle 线程每轮巡检，不一致即停机。

### 6.4 临界区

两种方式（编译期选择）：PRIMASK 全屏蔽，或 BASEPRI 阈值（只屏蔽不高于 `MINI_OS_IRQ_MAX_SYSCALL_PRIORITY` 的中断）。OSAL 槽位池临界区用的 `mini_os_irq_save/restore` 可嵌套。

---

## 7. 配置体系

每个选项走同一条**三层配置链**（实现样板见 `inc/mini_config.h`）：

1. `CONFIG_<NAME>` —— mini_tree Kconfig 生成（`config.h`）；
2. `MINI_OS_<NAME>` —— 外部构建系统预定义（命令行 `-D` 或父工程）；
3. 内置默认值。

> **注意**：特性开关恒定义为 `1`/`0`，判断必须用 `#if` 而非 `#ifdef`（`#ifdef` 对关闭的选项也为真）。

### 7.1 Kconfig 暴露面（`Kconfig.mini_tree`，均 `depends on OSAL_MINI_OS`）

| 选项 | 类型 / 默认 | 说明 |
| :--- | :--- | :--- |
| `MINI_OS_PRIORITY` | int / 32 | 优先级数（小=高），需为 8 的倍数；上限 32（位图是单 32 位字，≥32 走 UB） |
| `MINI_OS_DEFAULT_SYSTICK` | int / 1000 | tick 频率 Hz（默认 1 tick = 1 ms） |
| `MINI_OS_CPU_CLOCK_HZ` | int / 72000000 | CPU 主频，推导 SysTick reload |
| `MINI_OS_TICK_WHEEL` | int / 32 | 线程时间轮槽数（2 的幂） |
| `MINI_OS_THREAD_MIN_STACK_SIZE` | int / 256 | 线程最小栈（字节） |
| `MINI_OS_DEFAULT_IDLE_STACK_SIZE` | int / 256 | idle 线程栈 |
| `MINI_OS_TIMER_THREAD_STACK_SIZE` | int / 512 | SOFT 定时器服务线程栈（≥最小栈、8 的倍数） |
| `MINI_OS_TIME_SLICE` | bool / n | 同优先级时间片轮转（默认严格优先级） |
| `MINI_OS_EVENT` | bool / n | 32 位事件组（`OSAL_EVENT` 默认 select 它） |
| `MINI_OS_THREAD_DETACH` | bool / n | detach/join（绑定同一开关，每 TCB 增回收字段） |
| `MINI_OS_FIND_BY_NAME` | bool / n | 线程/信号量/互斥锁按名注册表 |
| `MINI_OS_LONG_TIME` | bool / n | 64 位 tick（附加回绕计数器） |
| `MINI_OS_STACK_OVERFLOW_CHECK` | bool / n | MSP 栈哨兵（需 mini-os-heap.ld） |
| `MINI_OS_USE_FPU` | bool / y | FPU 上下文保存（仅 CM4F/CM7 可见，硬浮点下不应关） |
| `MINI_OS_SPINLOCK`(+`_ATOMIC`/`_YIELD`/`_NUM`) | bool / y | header-only 自旋锁（关闭则 osal 退化为关中断兜底）；原子模式仅 SMP |
| `ARCH` | (无 prompt) | mini-os 架构 ID（0=M0/M0+ 1=M3 2=M4 3=M7），由 `PLATFORM_ARM_*` 自动派生，**不应手工设置** |

---

## 8. 与 mini_tree 的集成

### 8.1 选择后端

`CONFIG_OSAL_MINI_OS=y`（`make menuconfig` 或手改 `.config` 后重配）。约束：

- `depends on !PLATFORM_RISCV && !PLATFORM_ESP32` —— 仅 Cortex-M；
- `select USB_TUSB_OS_NONE` —— TinyUSB 不跑在 mini-os 上（USB 栈暂无 mini-os 后端）；
- `OSAL_EVENT`（默认 y）自动 select `MINI_OS_EVENT`。

### 8.2 板级接线（必须）

| 接线点 | 要求 |
| :--- | :--- |
| `SysTick_Handler` | 转 `mini_os_systick_handler()` |
| `PendSV_Handler` | 转 `pendsv_handler()`（**小写**，符号在 port.S） |
| 链接脚本 | `#include` `lib/mini-os/mini-os-heap.ld`，提供 `__mini_os_heap_start` / `__mini_os_heap_end` |
| 启动流程 | 必须遍历 `.init_array`（GCC/Clang 默认满足）—— 内核依赖构造函数自初始化（堆/注册表/idle/哨兵） |

### 8.3 OSAL 映射要点（`osal/src/osal_mini_os.c`）

| 主题 | 语义 |
| :--- | :--- |
| 优先级 | mini-os 数字越小越优先（同 RT-Thread，**与 FreeRTOS 相反**），OSAL 约定每后端用所属内核原生语义 |
| 错误码 | `MINI_OS_ERR_*` 在可见 `config.h`/`status.h` 时与 `OSAL_ERR_*` 数值一致，零开销直通；仅 `MINI_OS_ERR_AGAIN` 映射为 `OSAL_ERR_TIMEOUT` |
| ISR 模式 | `*_isr` 不主动切换；`osal_yield_from_isr()` 转发 `mini_os_schedule_yield_isr()` |
| 对象池 | 互斥锁/信号量静态内嵌内核对象 + `osal_pool` 槽位池，池临界区用 `mini_os_irq_save/restore` |
| 调度启动 | `osal_scheduler_start()` 先惰性引导内核（`schedule_init` + idle 线程 + SysTick）再启动调度器 |
| 调度冻结 | mini-os 无全局挂起 API，`osal_sched_freeze()` 退化为关中断（同 `osal_null` 单向冻结语义） |

### 8.4 构建方式

根构建经 `lib/CMakeLists.txt` 在 `OSAL_BACKEND=MINI_OS` 分支 `add_subdirectory(lib/mini-os)`；mini-os 自己的 CMakeLists 声明 `project(... C ASM)`（这也是仓库内唯一不依赖根工程启用 ASM 的内核库，对比：rtthread 曾因缺 `enable_language(ASM)` 丢弃 `context_gcc.S`，已修复）。事件组源文件按 `.config` 的 `CONFIG_MINI_OS_EVENT`/`CONFIG_OSAL_EVENT` 条件编入，关闭时连对象文件都不产生。

---

## 9. 内存占用实测

完整测量方法与四后端对比见 [memory_footprint.md](memory_footprint.md) §4（arm-none-eabi-gcc 13.3.1，最小固件 + `--gc-sections`）。mini-os 行摘录（text/data/bss，字节）：

| 配置 | newlib-nano | 完整 newlib |
| :--- | :--- | :--- |
| mini-os C | 14245 / 120 / 2620 | 38776 / 1772 / 2664 |
| mini-os C++ | 14381 / 128 / 3016 | 38912 / 1780 / 3064 |

- 四后端中 **text 最小**（比 FreeRTOS 少 ~3 KB，比 RT-Thread 少 ~3.6 KB，nano 口径）；
- **bss 最小且不随堆配置膨胀**：堆来自链接区不计 bss（FreeRTOS `ucHeap` 8 KiB / RT-Thread `s_rtt_heap` 32 KiB 均计入 bss），剔除可配堆后框架 bss 3016 B（C++）仍显著小于其余后端；
- 新增开销主要来自 `SYSTEM_CPP`（+136 text / +396 bss，nano 口径）。

---

## 10. 独立构建

mini-os 可脱离 mini_tree 单独构建（自 `project()`，自带默认 `-mcpu=cortex-m3`，可用 `MINI_OS_MCU` 覆盖）：

```sh
cd lib/mini-os
cmake --preset Debug
cmake --build --preset Debug
```

产物为静态库 `libmini-os.a`。独立构建时无 `.config`，事件组由 `mini_config.h` 内置默认兜底编入。注意内核使用 GNU 扩展，仅支持 GCC/Clang 工具链。

---

## 相关文档

- [memory_footprint.md](memory_footprint.md) —— 四后端内存基准（§4）
- [osal_switching.md](osal_switching.md) —— OSAL 后端切换与语义差异
- [fast_path.md](fast_path.md) —— HARD 定时器 / ISR 回调红线
- [patterns.md](patterns.md) —— mini_tree 关键机制（xtask/时间片等裸机侧对照）
- `lib/mini-os/README.md` —— 内核官方特性清单与三层配置说明
