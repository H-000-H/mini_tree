# mini_tree 关键机制解剖

> 八个贯穿全框架的关键机制：它们**怎么工作、为什么这样设计、怎么用、常见坑**。本文只讲机制本身，不涉及具体外设与板级细节。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 需要写驱动、写应用或改中间件的工程师 |
| **前置** | 已读 [architecture.md](architecture.md)（分层与启动时序） |
| **相关** | [fast_path.md](fast_path.md)（红线）· [osal_switching.md](osal_switching.md)（OSAL 后端切换）· [driver_guide.md](driver_guide.md)（驱动编写）· [runtime_services.md](runtime_services.md) |

---

## 目录

1. [编译期注册链（pre_execution）](#1-编译期注册链pre_execution)
2. [两段式点火——为什么顺序不可变](#2-两段式点火为什么顺序不可变)
3. [编译期 probe 表（DRIVER_REGISTER + dtc-lite）](#3-编译期-probe-表driver_register--dtc-lite)
4. [单一时基与协调式调度（xtask）](#4-单一时基与协调式调度xtask)
5. [ISR 长事务模式（VIRQ 上下半部 + 无锁通道）](#5-isr-长事务模式virq-上下半部--无锁通道)
6. [SPSC 无锁通道与双缓冲（algorithm/buffer）](#6-spsc-无锁通道与双缓冲algorithmbuffer)
7. [设备生命周期状态机（dev_lifecycle）](#7-设备生命周期状态机dev_lifecycle)
8. [非阻塞状态机（应用层写法）](#8-非阻塞状态机应用层写法)

---

## 1. 编译期注册链（pre_execution）

### 机制

`core/include/compiler_compat.h` 定义：

```c
#define pre_execution(x) __attribute__((constructor((x) + 100)))
```

`pre_execution(N)` 生成一个 **GCC/Clang 的 constructor 函数**，在 `main()` 之前按优先级自动执行。数字 `N` 越大执行越早。全框架的静态初始化都走这条链，**没有手写的 init 调用表，也没有运行时扫描**：

| 优先级 | 注册点 | 初始化内容 |
| :---: | :--- | :--- |
| `170` | `interrupt/interrupt.c` | 全局下半部 poller（FIFO + pending_drain） |
| `161` | `time_slice/task/xtask_preempt.c` | N+1 抢占式调度器（分组优先级 + CLZ 定位，可延迟/休眠/抢占，无就绪时精确 WFI） |
| `160` | `time_slice/task/xtask_coop.c` | 协调式调度器 `g_scheduler`（默认） |
| `152` | `osal/src/osal_null.c` | 裸机队列池 |
| `151` | `osal/src/osal_null.c` | 裸机信号量池 |
| `150` | `osal/src/osal_null.c` | 裸机互斥锁池 |

**设计意图**：让"池、表、队列"这类基础设施在任何业务代码触碰之前就绪；constructor 优先级数字越大越先跑，天然形成依赖排序（poller 池 > 调度器 > 各类 OSAL 池）。

### 常见坑

- 不要在 `pre_execution` 函数里调用 `device_*`、`event_bus_post` 等运行时 API——此时 `device_tree_init` 尚未执行，设备表还是空态。
- 同一翻译单元里两个 constructor 的先后由编译期优先级决定，跨翻译单元的**同级** constructor 顺序未定义，不要依赖。

---

## 2. 两段式点火——为什么顺序不可变

### 机制

启动分四个阶段（C API 见 `system_c/include/system_init.h`）：

| 阶段 | API | 典型工作 |
| :---: | :--- | :--- |
| 1 | `mini_tree_pre_os_init()` | 关全局中断、EventBus、safe_state、可选 WDT、`device_tree_init` |
| — | （可选）业务/平台准备 | 静态配置、额外注册 |
| 2 | `mini_tree_start_tasks()` | `board_driver_probe_all`、TWDT、Flash Scrubber |
| 3 | `system_init_complete()` | 释放全局中断 |
| 4 | 调度或裸机循环 | `vTaskStartScheduler` / `rt_system_scheduler_start` / `mini_tree_system_loop` |

C++ 侧 `mini_tree::system_pre_os_init()` / `system_start_tasks()` 与之对应，最后同样调 `system_init_complete()`。

### 为什么顺序不可变（论证）

1. **`device_tree_init` 必须先于一切设备访问**：运行时实例表（`device` / 递归互斥锁池 / `dev_lifecycle`）是静态数组，但锁必须逐个 `osal_mutex_create_static_recursive` 创建；任何 `device_*` 调用前这些必须就绪。
2. **第一阶段必须关全局中断**：probe 过程中 `device_open` 会真正使能外设中断（NVIC），而此刻 VIRQ 表 / 下半部 work 可能尚未注册完整。先关中断，保证"中断使能"只发生在所有 ISR 依赖就绪之后；`system_init_complete()` 才统一释放。
3. **EventBus 必须先建**：probe 失败路径会调用 `device_ops_unregister` → `event_bus_post(EVENT_SYS_DEVICE_REMOVED, ...)`，事件队列必须已经存在。
4. **probe 放第二阶段而不是第一阶段**：probe 会 open 设备、走日志、失败时按 criticality 触发 `OSAL_PANIC`（需要 `printf_output` 与 safe_state 已就绪）；这些依赖都在第一阶段结尾才备齐。
5. **中断使能放在调度器之前**：RTOS 路径下，先开中断再 `vTaskStartScheduler`，否则调度器启动瞬间的中断没有任务上下文可以承接。

### 常见坑

- 在阶段 1~2 之间（全局中断关闭期间）调用 `osal_delay_ms` 依赖 tick 中断，会死等——`osal_null` 后端虽有 tick hang 检测兜底（见 §4），但 RTOS 后端无此保护。
- 不要在阶段 1 里 probe 设备：此时 `board_driver_probe_all` 依赖的日志/安全子系统尚未初始化。

---

## 3. 编译期 probe 表（DRIVER_REGISTER + dtc-lite）

### 机制

`board/include/driver.h` 定义：

```c
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn)                                         \
    int board_driver_probe_##name(struct device* pdev) { return probe_fn(pdev); }                  \
    int board_driver_remove_##name(struct device* pdev) { return remove_fn(pdev); }
```

数据流：

```text
驱动 .c 里写 DRIVER_REGISTER(x, "compat,vendor", probe, remove)
  → dtc-lite 编译期扫描该宏
  → 生成 probe/remove 函数表 + board_probe_order() + board_dev_find_* 系列
board_driver_probe_all()
  → 按依赖拓扑顺序取 device → 直接取编译期函数指针调用
  → 失败按 criticality 分级处理
```

关键点：

- **运行时零 strcmp**：compatible 字符串在编译期就映射为函数指针，运行时只是查表取地址。
- **3 趟 deferred probe**：`board_driver_probe_all` 最多跑 3 趟；驱动返回 `VFS_ERR_DEFER`（phandle 依赖未就绪）则下趟重试；`deferred` 不再减少视为 **stall**，相关设备被永久 `DEVICE_STATUS_DISABLED`。
- **失败分级**（`handle_probe_failure`）：`DEVICE_CRIT_FATAL` → `OSAL_PANIC` 安全停机；`DEVICE_CRIT_WARNING` → 告警；`DEVICE_CRIT_IGNORE` → 静默。依赖失败的设备通过 `disable_dependents` 级联禁用。
- 无驱动的无名节点静默禁用；有名节点无驱动按 criticality 处理。

### 为什么编译期而不是运行期

- **省 flash**：省掉 compat 字符串比较的代码与匹配表。
- **确定性**：probe 顺序由 DTS 依赖拓扑在编译期定死，不依赖初始化顺序。
- **可审计**：生成物是普通 C 数组，`generated/board/mini_tree/*` 可以直接看。

### 常见坑

- 驱动 `.c` 忘记写 `DRIVER_REGISTER` → 生成表里没有该函数 → 设备被标记 `DISABLED`，日志会提示 "no generated probe"。
- `probe` 返回 `VFS_ERR_DEFER` 但不能在 3 趟内解决 → stall → 永久禁用；应确保依赖在 probe 顺序上靠前。
- remove 标准序列（`driver.h` 注释）必须按序：`dev_lc_remove_start` → `device_ops_unregister` → `dev_lc_remove_drain` → teardown → `dev_lc_remove_finish`（机制见 §7）。

---

## 4. 单一时基与协调式调度（xtask）

### 机制

裸机后端（`CONFIG_OSAL_NULL`）下，全系统只有一个时基源：`x_scheduler.tick_count`。`xscheduler_start()` 按"chosen 显式覆盖优先，否则 SysTick 默认"两级选择 tick 源：

```text
① DTS 显式配 chosen TIM（CHOSEN_SCHEDULER_TIM）→ 显式覆盖，走通用 TIM + VIRQ
  → xscheduler_start(): device_open → 取 hal_tim_device → VIRQ(tim,0) 注册
  → scheduler_tim_isr_top(): 清 update flag + x_scheduler_tick(+tick_delay)   ← ISR 内，仅此而已

② 未配 chosen → 默认 SysTick（Cortex-M 架构标准件，零配置）
  → hal_systick_init(DTC_GEN_TICK_RATE_HZ) 配置 SysTick（频率走 DTS，基址写死）
  → SysTick_Handler → hal_systick_irq_handler() + x_scheduler_tick(+tick_delay)  ← ISR 内，仅此而已

非 ARM（RISC-V）无 SysTick，hal_systick_init 返回 NOTSUPP，RISC-V 板必须在 DTS 配 chosen。
→ osal_time_ms() 直接读 g_scheduler.tick_count                     ← 全局统一时钟
```

任务模型（`time_slice/task/xtask.h`）：

- `x_task`：侵入式链表节点，字段 `name` / `xTask_cb` / `period` / `next_running` / `is_running`。
- `xscheduler_task_create(sched, task, name, cb, period_ms)`：尾插链表，`next_running = 当前tick + period`。
- `x_task_run()`（主循环 `x_scheduler_poll()` 调用）：遍历链表，`is_running` 为假才进入；`(int32_t)(now - next_running) >= 0`（**有符号比较防 uint32 回绕**）判定到期 → 执行回调 → `next_running = now + period`。

### 为什么这么设计

- **固定时基不漂移**：`next_running = now + period`，任务执行耗时不会被计入下一周期，长期无累积漂移。
- **is_running 是重入保护而非使能开关**：注释明确"非运行态才允许进入"，防止同一任务在回调内再次进入；到期与否都复位，避免未到期分支把任务卡死在 running。
- **单一全局时基**：裸机 `osal_time_ms()`、`osal_delay_ms()`、调度器 tick、下半部轮询共用 `g_scheduler.tick_count`，语义一致；切到 RTOS 后 `osal_time_ms()` 换成 RTOS tick，业务代码零改动。
- **裸机 delay 有防死锁兜底**：`osal_delay_ms` 用 WFI 忙等 + tick hang 检测（连续 10000 次无 tick 前进即退出），防止时基未启动时硬死锁。

### 任务周期与时间预算

协调式调度下所有回调**串行**执行，因此必须满足：

```text
Σ(所有任务回调最坏执行时间) ≤ 最小任务周期
```

| 任务周期 | 建议回调预算 | 用途示例 |
| :---: | :---: | :--- |
| 1 ms | ≤ 100 µs | 高速采样、PWM 服务 |
| 5 ms | ≤ 1 ms | 控制环、按键扫描 |
| 20 ms | ≤ 5 ms | 状态机推进、协议轮询 |
| 100 ms | ≤ 20 ms | 慢速外设巡检、看门狗喂狗 |

预算超支时优先：**缩短回调内阻塞**（改状态机，见 §8）→ 拆任务 → 换 `CONFIG_OSAL_FREERTOS` 抢占式（`osal_switching.md`）。抢占式 `xtask_preempt.c`（`CONFIG_XTASK_PREEMPT`）为实验性未完工，生产环境走 RTOS。

### 常见坑

- 回调里调用 `osal_delay_ms`（忙等）会拖死所有周期任务，只允许在初始化阶段或短时序使用。
- 回调必须尽快返回，不能 `while(1)` 死循环。
- `xscheduler_start()` 必须在 `mini_tree_start_tasks()` 之后调用（注释明确：VFS 设备已 probe）。

---

## 5. ISR 长事务模式（VIRQ 上下半部 + 无锁通道）

### 机制

`interrupt/interrupt.h` 提供 **VIRQ 虚拟中断号 + 下半部工作队列一体化**。

**VIRQ 编号**：按 block 划分，块大小 `VIRTUAL_IRQ_BLOCK_SIZE = 8`（2 的幂），`VIRQ(block, idx)` 计算虚拟中断号：

```text
VIRTUAL_IRQ_BLOCK_TABLE(X)  → system / tim / gpio / adc / uart / spi / i2c / i2s / user
```

**注册与分发**：

```c
interrupt_virtual_register(VIRQ(tim, 0), scheduler_tim_isr_top, NULL, &ctx);
// top_half 返回 VFS_IRQ_ENTRY_BOTTOM(非零) → dispatch 自动 submit 下半部
interrupt_virtual_dispatch(virq_num);   // ISR 内调用
```

**下半部工作项**（`struct bottom_half_work`）用三个原子位实现**合并 + 补跑**：

```text
pending   已在队列或正在执行
executing 正在执行（仅消费者写）
rerun     fn() 执行期间再次 trigger → 结束后补跑，事件不丢失
```

**消费侧两种适配**：

| 路径 | 结构 | 唤醒方式 |
| :--- | :--- | :--- |
| 裸机（`CONFIG_OSAL_NULL`） | `bottom_half_poller`：fifo + `pending_drain` 标志 | 主循环 `interrupt_bottom_half_poll()`；ISR 置位，主循环先清标志再 `run_pending`，期间新 ISR 重新置位——防丢唤醒 |
| RTOS | `bottom_half_task`：fifo + 二值信号量 | 专用任务 `osal_sem_wait` 阻塞等待，ISR 侧 `post_from_isr` 唤醒 |

### 完整模式（ISR 里长事务怎么做）

```text
ISR（top_half，必须轻量）
  ├─ 读硬件标志 / 清中断
  ├─ 无锁取数（写 SPSC FIFO，见 §6）
  └─ return VFS_IRQ_ENTRY_BOTTOM  → dispatch submit 下半部 work
主循环 / bottom_half_task（下半部，可以重）
  └─ 协议解析、数据处理、驱动回调
```

### 为什么这么设计

- ISR 里禁止 `printf` / 互斥锁 / `malloc` / 无界工作（红线见 [fast_path.md](fast_path.md)）；把重活推到线程上下文，中断延迟只由 top_half 决定。
- `pending/executing/rerun` 三态用于**合并高频触发、避免重复入队**：执行期间再次触发只置 `rerun`，`bottom_half_run_pending` 结束后补跑。注意 FIFO 满时 submit 会失败并丢弃 work（见下方常见坑），并非绝对不丢失。
- 裸机与 RTOS 共用同一套 `bottom_half_submit_from_isr` / FIFO，只是唤醒机制不同。

### 常见坑

- ISR 里禁止 `osal_mutex_lock`（返回 `OSAL_ERR_ISR`）；临界区用 `osal_spinlock`。
- `bottom_half_run_pending` 必须在线程上下文调用，ISR 内调用直接返回。
- FIFO 满时 submit 失败返回 false，work 被丢弃——队列深度 `BOTTOM_HALF_QUEUE_DEPTH`（2 的幂）要按最坏中断频率设计。

---

## 6. SPSC 无锁通道与双缓冲（algorithm/buffer）

### 机制（`algorithm/buffer/buffer.h`）

`struct fifo_spsc`：**严格单生产者单消费者**的无锁环形 FIFO。

```c
struct fifo_spsc {
    fifo_data_type* buf;          /* 32 字节对齐 */
    uint16_t size, mask;          /* size 必须 2 的幂；mask = size-1 */
    ATTR_ALIGN(64) uint16_t w_ptr; /* 写指针，独占缓存行 */
    ATTR_ALIGN(64) uint16_t r_ptr; /* 读指针，独占缓存行 */
};
```

- **无锁**：读写只用 `__atomic` 的 acquire/release 内存序（`FIFO_LOAD_ACQUIRE` / `FIFO_STORE_RELEASE`）。
- **缓存行隔离**：`w_ptr` / `r_ptr` 各自 `ATTR_ALIGN(64)`，避免伪共享（Cortex-M7/A、ESP32 双核等 Cache Line 为 32/64 字节）。
- **uint16_t 溢出即计数**：`已用空间 = w - r`（无符号回绕），指针不裁剪，物理下标用 `w & mask` 映射。
- **内存序意图**：写者先写 `buf[w&mask]` 再 release 发布 `w_ptr`；读者 acquire 读 `w_ptr` 再读 `buf`——意图是让"数据先于指针可见"。该依赖 acquire/release 语义，需留意所用编译器/架构对该内存序的实际支持。

### 谁写谁读

| 场景 | 生产者 | 消费者 |
| :--- | :--- | :--- |
| ISR → 主循环 | ISR top_half | 主循环（下半部 poll） |
| DMA → CPU | DMA 完成中断 | 业务任务 |
| 下半部队列 | `interrupt.h` 内部 | `bottom_half_run_pending` |

### 双缓冲（`double_buffer_spsc`）

读写分离、swap 切换：**DMA 采集与 CPU 处理并行**。`double_buffer_write_data/read_data` 各自独占一侧，DMA 写满一块后交换——ADC / I2S DMA 下半部（`g_adc_dma_bottom_half_work`）是典型用法。

### 常见坑

- **违反 SPSC 是未定义行为**：多生产者会丢数据/破坏内存序，多消费者会重复消费。需要多对多就上 OSAL 队列（带锁）。
- `fifo_init` 强制 `size` 为 2 的幂（`(size & (size-1)) != 0` 直接返回），传错值静默失败。
- `fifo_data_type` 是 `uintptr_t`：既能存 16 位 ADC 采样值，也能存下半部 work 指针（`interrupt.h` 正是这么复用的）。
- 块读写（`fifo_write_block/read_block`）处理了环形回绕的跨边界 memcpy，`len` 超过空闲空间时按 `free_len` 截断并返回实际长度。

---

## 7. 设备生命周期状态机（dev_lifecycle）

### 机制（`board/src/dev_lifecycle.c`）

`struct dev_lifecycle` 是 **CAS 哨兵版无锁状态机**，管理 open 计数与在途 I/O 计数：

```text
opens       open 引用计数
io_active   活跃 I/O 计数
state       UNINITIALIZED → LIVE → REMOVING → (RESET)
```

- `open_begin` / `io_begin`：CAS 循环递增；遇 `-1` 哨兵（teardown 已锁定）或非 `LIVE` 直接拒绝。
- `remove_drain`（teardown 排空，两阶段 CAS）：
  1. CAS `opens` 0→`-1`（`DEV_LC_LOCKED`）；失败说明仍有 open，`osal_delay_ms(1)` 重试等待归零；
  2. **opens 一旦锁定即保持 `-1` 不回滚**，在 `state == REMOVING` 门控下反复 CAS `io_active` 0→`-1` 直至归零（失败仅重试 io_active）。
- 设计意图（源码注释）：状态机门控（`state == REMOVING` 是 drain 入口前提，期间 `open_begin`/`io_begin` 均检查 `state == LIVE`，故不会新增计数）+ 单调锁定（opens 不回滚到 0，避免短暂暴露窗口）+ 内存序（ACQUIRE/RELEASE/ACQ_REL）+ 无 ABA 考虑（`-1` 终态仅由 `remove_finish` 复位，单调操作通常不会再现同值）。drain 退出时两计数器均稳定 `-1`，并发 open/io 见 `-1` 会拒绝。此逻辑依赖代码评审，未做形式化验证。

### 使用序列（驱动 remove 标准流程，`driver.h` 注释）

```c
dev_lc_remove_start(device_lc(pdev));      // state → REMOVING
device_ops_unregister(pdev);               // REMOVED + 广播 EVENT_SYS_DEVICE_REMOVED + 持锁清 ops（防 TOCTOU）
dev_lc_remove_drain(device_lc(pdev), OSAL_WAIT_FOREVER);  // 原子轮询，无持锁
... teardown ...
dev_lc_remove_finish(device_lc(pdev));     // RESET
```

### 为什么这么设计

- **热插拔/移除安全**：卸载时设计上等到没有在途 open/I/O 才退出 drain，且**不持锁等待**（原子轮询，避免持锁阻塞其他线程导致死锁）。
- **`device_ops_unregister` 持锁斩断 ops**：`device_write` 等 VFS 入口在锁内做 check-then-act，卸载方持锁置空 `ops`，阻断 TOCTOU（线程 A 已通过 status 检查，线程 B 同时卸载 → NULL 解引用 → HardFault）。

### 常见坑

- `remove_drain` 超时返回 `VFS_ERR_TIMEOUT`；`OSAL_WAIT_FOREVER` 时若某个 open 永不释放会永久等待——业务代码必须保证 open/io 成对。
- `dev_lc_open_begin` 返回语义：首次 open 返回 1，重复 open 返回 0，失败返回负错误码，别把"重复 open"当错误。

---

## 8. 非阻塞状态机（应用层写法）

### 背景

裸机后端（`CONFIG_OSAL_NULL`）下 `osal_task_create` 返回 `OSAL_ERR_NOTSUPP`——**裸机没有 OS 任务**。`osal_null.c` 头注释明确：

> 复杂任务必须走状态机和任务切换（日常就 OS 吧省心省力，除非内存紧张或对效率要求极高）。

所以裸机下"等 500ms 再做某事"不能靠阻塞延时，而要：

```text
记录时间戳 → 轮询 (now - start) → 到期推进状态
```

### 骨架

```c
typedef enum { S_IDLE, S_WAIT_DELAY, S_DONE } my_state_t;

static my_state_t  st;
static uint32_t    t_start;

void my_task_cb(x_task* t)          /* 注册到 xtask，周期 5ms */
{
    switch (st)
    {
    case S_IDLE:
        st = S_WAIT_DELAY;
        t_start = osal_time_ms();
        break;
    case S_WAIT_DELAY:
        if ((osal_time_ms() - t_start) >= 500U)   /* uint32 相减，溢出安全 */
            st = S_DONE;
        break;
    case S_DONE:
        /* 单次完成；需要重复就回 S_IDLE */
        st = S_IDLE;
        break;
    }
}
```

要点：

- **用 `osal_time_ms()` 记时间戳轮询**，不用 `osal_delay_ms` 阻塞——回调不阻塞，其他周期任务不受影响。
- `(now - start)` 用无符号相减，**天然处理 uint32 回绕**（49.7 天后不炸）。
- 复杂任务拆成多个状态 + 每周期推进一小步；这同时满足 §4 的时间预算。

### RTOS 后端的差异

切到 `CONFIG_OSAL_FREERTOS` / `RTTHREAD` 后，`osal_delay_ms` 是**真正的休眠**（任务挂起、让出 CPU），可以放心阻塞；但同一套状态机代码在裸机/RTOS 都能跑，属于"可移植的最低公共分母"写法。

### 常见坑

- 裸机下在 ISR 或 tick 回调里 `osal_delay_ms` 忙等——拖死整个时基。
- 状态机忘记在终态复位，任务只执行一次后永远空转。
- 不要用 `volatile` 解决状态变量同步——周期任务同一线程串行执行，普通 `static` 即可。

---

## 相关文档

- [architecture.md](architecture.md)（分层与启动时序） · [fast_path.md](fast_path.md)（ISR/热路径红线）
- [osal_switching.md](osal_switching.md)（OSAL 后端切换） · [driver_guide.md](driver_guide.md)（驱动编写与 remove 生命周期）
- [runtime_services.md](runtime_services.md)（EventBus / VIRQ / BufferPool） · [design_decisions.md](design_decisions.md)（设计动机）
