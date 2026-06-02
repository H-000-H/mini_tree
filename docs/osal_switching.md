## 14. OSAL 后端切换注意事项（重要）

> 本章总结在 [mini-tree-example](https://github.com/H-000-H/mini-tree-example) (STM32F407ZGT6) 上切换 OSAL 后端时踩过的坑。
> 在其它平台/RTOS 版本上移植时同样适用——每一条都曾导致"LED 常量"级别的硬故障。

### 14.1 NVIC 优先级移位（FreeRTOS 常量亮的元凶）

**现象：** FreeRTOS `xPortStartScheduler()` 内部断言失败 → 系统卡死 → LED 常量。

**成因：** STM32F407 的 NVIC 只实现 4 位优先级（16 级），但 FreeRTOS
`configMAX_SYSCALL_INTERRUPT_PRIORITY` 在 8 位优先级假设下被写入寄存器。
实际硬件只取高 4 位，低 4 位被忽略。

```c
// ─── 错误写法（LED 常量）────────────────────────────────────
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    5
/* 硬件只取高 4 位: 5 & 0xF0 = 0 → 断言失败 */

// ─── 正确写法 ───────────────────────────────────────────────
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5 << 4)  /* = 80 = 0x50 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5
```

**通用公式：**

```c
/* PRIO_BITS: 器件参考手册 NVIC 章节, 不是想当然 */
/*   Cortex-M3/M4 通常 4 位 (16 级), M7 可能 8 位 (256 级) */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - NVIC_PRIO_BITS))
```

**排查清单：**

1. 查芯片参考手册确定 `NVIC_PRIO_BITS`（STM32F1/F2/F4 = 4, STM32H5/H7/H7R = 4, 大部分商用 M3/M4/M7 = 3~4；仅少部分非 ST 的 M7 实现 8 位）
2. `configMAX_SYSCALL_INTERRUPT_PRIORITY` 必须等于 `configLIBRARY_... << (8 - NVIC_PRIO_BITS)`
3. 调试器读出 `SHPR2/SHPR3` 寄存器确认写入值符合预期
4. 若串口可用，使能 `configASSERT` 看断言信息

### 14.2 向量表 — 处理函数名因 OS 而异

每种操作系统在异常向量表中使用的符号名不同。**不能直接写 PendSV_Handler 指望 OS 自动链接。**

| 异常 | FreeRTOS | RT-Thread | OSAL_NULL |
|------|----------|-----------|-----------|
| SVC | `vPortSVCHandler` | `Default_Handler`（不用） | `Default_Handler` |
| PendSV | `xPortPendSVHandler` | `PendSV_Handler`（rtt 移植层提供） | `Default_Handler` |
| SysTick | `xPortSysTickHandler`（由 FreeRTOS 提供） | 用户实现（内部调 `rt_tick_increase()`） | 用户实现（内部调 `osal_null_systick_handler()`） |

**向量表写法：**

```c
/* startup.c — 条件编译示例 */
__attribute__((section(".isr_vector")))
void* const g_pfnVectors[] = {
    /* ... 前 16 项系统异常 ... */

    [SVC_IRQn + 16]    =
#ifdef CONFIG_OSAL_FREERTOS
        vPortSVCHandler,
#else
        Default_Handler,
#endif

    [PendSV_IRQn + 16] =
#ifdef CONFIG_OSAL_FREERTOS
        xPortPendSVHandler,
#elif defined(CONFIG_OSAL_RTTHREAD)
        PendSV_Handler,     /* RT-Thread 移植层提供 */
#else
        Default_Handler,
#endif

    [SysTick_IRQn + 16] =
#ifdef CONFIG_OSAL_FREERTOS
        xPortSysTickHandler,
#else
        SysTick_Handler,    /* 用户提供 */
#endif
};
```

**切换 OS 后端时必须同步更新向量表中 SVC/PendSV/SysTick 三项，** 否则异常会落到 `Default_Handler` 死循环。

### 14.3 SysTick_Handler 实现差异

| 后端 | SysTick_Handler 做什么 | 谁提供 |
|------|----------------------|--------|
| FreeRTOS | FreeRTOS 内核内部使用 | `xPortSysTickHandler`（FreeRTOS 自带） |
| RT-Thread | 调用 `rt_interrupt_enter/leave` + `rt_tick_increase()` | 用户自己写 |
| OSAL_NULL | 调用 `osal_null_systick_handler()` | 用户自己写 |

**FreeRTOS 不需要用户写 SysTick_Handler，向量表直接指向 `xPortSysTickHandler`。**
RT-Thread 和裸机都需要用户提供：

```c
// OSAL_NULL — SysTick_Handler
void SysTick_Handler(void)
{
    osal_null_systick_handler();
}

// RT-Thread — SysTick_Handler
void SysTick_Handler(void)
{
    rt_interrupt_enter();
    rt_tick_increase();
    rt_interrupt_leave();
}
```

### 14.4 切换后端后必须清理构建缓存

**错误行为：** 只改 `.config` → `cmake --build build` → 链接旧目标文件 → 莫名链接错误/跑错 OS。

**原因：** CMake 不会因为 `.config` 变化自动重建所有目标文件。旧的目标文件（如链接了 FreeRTOS 的 `osal` 库）仍然存在，导致：

- "符号找不到"（如 `free_freertos_mem` 来自旧版本）
- "符号重复定义"（新老 OS 的调度器同时链接）
- 实际运行的是上一个 OS 的二进制

**正确做法：**

```bash
rm -rf build
cmake -B build -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm4f.cmake
cmake --build build
```

> 习惯上：每次改 `.config` 后先删 `build/` 再重新 configure。

### 14.5 链接依赖链 — Include 路径传递

FreeRTOS 内核头文件（`FreeRTOS.h`、`task.h` 等）需要通过 CMake 的 `target_link_libraries` 传递到依赖方。

**错误写法：**

```cmake
# system_c/CMakeLists.txt — 缺少 osal 依赖
target_link_libraries(system
    PUBLIC  core
    PRIVATE board
)
```

编译 `system_c/src/safe_state.c`（`#include "FreeRTOS.h"`）时找不到头文件 → `FreeRTOS.h: No such file or directory`。

**正确写法：**

```cmake
target_link_libraries(system
    PUBLIC  core
    PRIVATE board
            osal          # ← 必须有 osal, 才能继承 FreeRTOS include 路径
)
```

**通用规则：**

```
osal (包含 FreeRTOS/RT-Thread 的 include path)
  ↑
system (编译 safe_state.c 需要 FreeRTOS.h)
  ↑
用户主工程 (最终链接)
```

任何直接 `#include` 了 OS 头文件的编译单元，所在的 CMake target 都必须 `PRIVATE` 链接 `osal`。

### 14.6 HAL 延时 vs OSAL 延时

| 函数 | 上下文 | 精度 | 阻塞方式 | 可用场景 |
|------|--------|------|---------|---------|
| `hal_delay_us(n)` | 任意（含 ISR） | 1 μs | DWT_CYCCNT 忙等 | ISR、Phase 1 微秒级延时 |
| `hal_delay_ms(n)` | 任意 | ~1 ms | DWT_CYCCNT 忙等 | Phase 1、裸机主循环 |
| `osal_task_delay(n)` | 仅任务上下文 | 1 tick | 任务挂起（不占 CPU） | Phase 2 任务内 |
| `vTaskDelay(n)` | 仅 FreeRTOS 任务 | 1 tick | 任务挂起 | 同 osal_task_delay |
| `rt_thread_mdelay(n)` | 仅 RT-Thread 线程 | 1 tick | 线程挂起 | 同 osal_task_delay |

**要点：**

- **Phase 1（OS 启动前）只能用 `hal_delay_*`**，此时调度器还没跑，`osal_task_delay` 会死锁
- **ISR 中只能用 `hal_delay_us`**（短时忙等），不能用任何阻塞式延时
- **任务中优先用 `osal_task_delay`**，避免 DWT 忙等浪费 CPU
- `hal_delay_ms` 依赖 `hal_delay_init()` 开启 DWT_CYCCNT，Phase 1 前必须调用

```c
int main(void)
{
    platform_init();
    hal_delay_init();               // ← 必须！否则 hal_delay_* 卡死

    mini_tree_pre_os_init();

    // Phase 1 — 只能用 hal_delay_*
    hal_delay_ms(100);

    osal_task_create("work", 512, 2, work_task, NULL, 0);
    system_init_complete();
    vTaskStartScheduler();
}

void work_task(void* param)
{
    // Phase 2 任务 — 用 osal_task_delay
    while (1) {
        osal_task_delay(1000);      // 挂起 1s，不占 CPU
    }
}

void my_isr(void)
{
    hal_delay_us(10);               // ISR — 只能用忙等
}
```
