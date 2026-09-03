# 内存与 flash 基准

> 编译产物大小与段分布（工具链相关）。裁剪与大小开关见 §2，优化建议见 §5；段布局可用编译器 map 文件 / `--gc-sections` 报告核对。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 优化体积 / 评估成本 |
| **相关** | [getting_started.md](getting_started.md)（构建与度量）· [design_decisions.md](design_decisions.md)（裁剪偏好） |

---

## 1. 段布局

| 段 | 说明 |
| :--- | :--- |
| `text` | 代码与只读常数 |
| `rodata` | 只读常数 |
| `data` | 已初始化全局变量 |
| `bss` | 未初始化全局变量（不占 flash） |
| `err_section` | 错误符号表（`ERR_SECTION_BASE`，见 `error_symbols.ld`）——独立 ROM 区或 RAM 镜像 |
| `*.noinit` | WDT/RTC 等不被初始化的 RAM |
| `.log_*` | 日志注册表（关日志后移除） |

> WDT 与 `safe_state` 同属 `system`（由 `CONFIG_SYSTEM_WDT` / `CONFIG_SAFETY_SHUTDOWN` 控制）；烧录后与中断、`err_section` 需保证物理不被覆盖。

---

## 2. 控制项

| Kconfig | 作用 |
| :--- | :--- |
| `CONFIG_SYSTEM_WDT` | 框架看门狗（默认开） |
| `CONFIG_SAFETY_SHUTDOWN` | 安全停机回调（默认关） |
| `CONFIG_SYS_LOG_USE_PRINTF` / `_OSAL` / `_ESP` | `SYS_LOG*` 日志后端选择（关日志最省） |
| `CONFIG_PRODUCTION_LOG` | 黑匣子故障记录（默认关） |
| `CONFIG_EVENT_BUS` / `CONFIG_SYSTEM_CMD` / `CONFIG_SYSTEM_SCRUBBER` | 可选功能总开关（默认关） |
| `CONFIG_BUILD_DISASM` | 反汇编 post-build（默认开，按需关） |

---

## 3. 基准

> 单位 KiB。示例数字，随重构与新积木会变化（周期性重测）。

| 配置 | text | rodata | data | bss | flash 合计 | 说明 |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| 最小（仅 `osal` + 框架） | 6.2 | 1.1 | 0.3 | 2.4 | 7.6 | `CONFIG_OSAL_NULL` + 空板 |
| + 设备模型 | 11.8 | 2.0 | 0.6 | 4.1 | 13.8 | `board/` 全部 |
| + 一个 VFS 设备（uart） | 15.3 | 2.6 | 0.8 | 5.2 | 18.7 | `vfs/uart` |
| + FreeRTOS 后端 | 19.1 | 3.3 | 1.1 | 6.9 | 23.5 | `CONFIG_OSAL_FREERTOS` |
| + WDT + safe_state | 20.4 | 3.5 | 1.2 | 7.3 | 24.9 | `CONFIG_SYSTEM_WDT` + `CONFIG_SAFETY_SHUTDOWN` |

> 上表为 GCC `-Os` + LTO 估算。开日志（`CONFIG_SYS_LOG_LEVEL>0`）各档增 ~3–8 KiB `rodata`/`text`；关日志最划算。
>
> 口径说明：本表为 **flash 合计**（text+rodata+data），与 [CHANGELOG.md](../../CHANGELOG.md) 中"全库 85.3→28.0 KB / 默认最小 ≈ 2.8 KB"的**静态 RAM（bss + data）下限**叙事口径不同（后者是历史压缩成果、且只计 RAM），二者不可直接比较；最新 RAM 下限以本表 `bss`/`data` 列为准。

---

## 4. 调度方案对比（最小固件实测）

> 实测：`arm-none-eabi-gcc 13.3.1`（Windows，旧于旧版 14.2.1/Linux——旧编译链验证可编过），`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections -fdata-sections` + `--gc-sections`；最小固件 = `startup`（向量表 + Reset_Handler）+ `main`（system 层标准启动序列）+ 链接 `mini_tree` 全库（含 RTOS 内核，`--start-group` 解决循环引用），链接脚本仿 STM32F4（FLASH 1 MiB / RAM 128 KiB）。单位 B，`RAM 合计 = data + bss`。分两套 libc 口径：**newlib-nano**（`--specs=nano.specs`，最小体积常规选择）与**完整 newlib**（旧表口径）；`.config` 基线为仓库当前默认（事件总线/WDT/OSAL 日志开），未引用模块（lwIP/USB 等）经 `--gc-sections` 不进闭包。绝对值随工具链与基线配置漂移，**相对差**更有效。

### 4.1 newlib-nano（推荐口径）

| 调度方案 | system 后端 | text | data | bss | RAM 合计 |
| :--- | :--- | ---: | ---: | ---: | ---: |
| 全裸 `while`（`XTASK_NONE`） | 无 | 134 | 0 | 512 | 512 |
| 协调式 `XTASK_COOP` | C | 11153 | 116 | 4400 | 4516 |
| 协调式 `XTASK_COOP` | C++ | 11237 | 120 | 4432 | 4552 |
| 抢占式 `XTASK_PREEMPT` | C | 11565 | 116 | 4848 | 4964 |
| 抢占式 `XTASK_PREEMPT` | C++ | 11649 | 120 | 4880 | 5000 |
| mini-os | C | 14245 | 120 | 2620 | 2740 |
| mini-os | C++ | 14381 | 128 | 3016 | 3144 |
| FreeRTOS | C | 17492 | 108 | 12176 | 12284 |
| FreeRTOS | C++ | 17628 | 116 | 12528 | 12644 |
| RT-Thread | C | 17953 | 272 | 35084 | 35356 |
| RT-Thread | C++ | 18057 | 280 | 35480 | 35760 |

### 4.2 完整 newlib（旧表口径）

| 调度方案 | system 后端 | text | data | bss | RAM 合计 |
| :--- | :--- | ---: | ---: | ---: | ---: |
| 全裸 `while`（`XTASK_NONE`） | 无 | 134 | 0 | 512 | 512 |
| 协调式 `XTASK_COOP` | C | 35732 | 1768 | 4448 | 6216 |
| 协调式 `XTASK_COOP` | C++ | 35820 | 1772 | 4480 | 6252 |
| 抢占式 `XTASK_PREEMPT` | C | 36148 | 1768 | 4896 | 6664 |
| 抢占式 `XTASK_PREEMPT` | C++ | 36228 | 1772 | 4928 | 6700 |
| mini-os | C | 38776 | 1772 | 2664 | 4436 |
| mini-os | C++ | 38912 | 1780 | 3064 | 4844 |
| FreeRTOS | C | 42068 | 1764 | 12224 | 13988 |
| FreeRTOS | C++ | 42204 | 1772 | 12576 | 14348 |
| RT-Thread | C | 48984 | 1924 | 35136 | 37060 |
| RT-Thread | C++ | 49088 | 1932 | 35528 | 37460 |

### 4.3 口径与结论

**堆口径（bss 不可直接横比的原因）**：

- FreeRTOS：堆为静态数组 `ucHeap[CONFIG_FREERTOS_HEAP_SIZE]`（默认 8192），**计入 bss**；
- RT-Thread：堆为静态数组 `s_rtt_heap[CONFIG_RTT_HEAP_SIZE]`（默认 32×1024，见 `osal_rtthread.c`），**计入 bss**；
- mini-os：堆为链接期区域（`__mini_os_heap_start`→`__mini_os_heap_end`，bss 末尾到栈顶），**不计入 bss**——剩余 RAM 全归堆；
- 裸机 xtask：无堆。

剔除可配堆后的框架 bss（nano / C++）：coop 4432 · mini-os 3016 · FreeRTOS 4336 · RT-Thread 2712。全裸行的 bss 512 为链接脚本 `._user_heap_stack` 的最小堆占位，非真实占用；其余各行同样包含。

范围说明（沿旧表）：全裸（`XTASK_NONE`）下 `OSAL_NULL_TASK_CPP` 由 Kconfig 自动关闭（`depends on !XTASK_NONE`），且 osal/system 层依赖 xtask 接口（`osal_null.h` 无条件 include `xtask.h`），无实现时无法链接，固件退化为最小闭包（startup + 主循环手动轮询），不含 system/osal 层；RTOS 后端 `text` 已含各自内核；数字含全库（board 设备模型等），**相对差**更有效。裸机调度三态（`XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`）由 `Kconfig.mini_tree` 的 choice 选择，CMake 据此注入 `MINI_TREE_XTASK_*` 宏决定编译 `xtask_coop.c` 或 `xtask_preempt.c`；抢占式与协调式对外 API 完全一致（`xscheduler_task_create`/`x_scheduler_poll`/`xscheduler_start`），调用方无感切换。

结论：

1. 全裸最省（134 B text）；代价是调度逻辑全部自写。
2. 裸机 xtask（coop 11.2 KB）是最省的"带调度"方案，且**无独立任务栈**（run-to-completion，任务栈复用主循环栈）；preempt 的 bss 多出任务池（448 B）。
3. RTOS 内核 text：**mini-os（14.4 KB）< FreeRTOS（17.6 KB）< RT-Thread（18.1 KB）**；mini-os 比 FreeRTOS 省 ~3.2 KB、比 RT-Thread 省 ~3.7 KB(mini-os未做smp mpu等部件做完差距不大都在17kb到18kb左右内核源码就这么大不太好压了除非主动裁剪功能)。
4. RTOS 内核 text：**mini-os（14.4 KB）< FreeRTOS（17.6 KB）< RT-Thread（18.1 KB）**，mini-os 当前省～3.2–3.7 KB；**该差距主要来自功能集差异**——mini-os 未实现 SMP/MPU/ 内存保护等部件，补齐后预计与 FreeRTOS/RT-Thread 同级（17–18 KB 区间）。完整内核的 text 本体在此量级属正常，进一步压缩只能靠裁剪功能，调 `CONFIG_RTT_HEAP_SIZE`/`CONFIG_FREERTOS_HEAP_SIZE` 等可对齐。
5. C/C++ system 后端：RTOS 下 C++ 比 C 约 +100~140 B text、+350~400 B bss；裸机几乎一致（+84 B text / +32 B bss）。**选 C 后端最省**。
6. **libc 的影响（4.1 vs 4.2）**：完整 newlib 比 nano 普遍 **+24.6 KB text、+~1.65 KB data**（stdio 结构），bss 仅 +~48 B；RT-Thread 例外多 ~6.4 KB（其 kservice 配置为复用 libc 格式化 `RT_KLIBC_USING_LIBC_VSNPRINTF`，拉入完整 vfprintf）。libc 为常量开销，不影响后端间相对比较；追求最小体积用 `--specs=nano.specs`。
7. 每任务额外成本：RTOS 需 TCB + 独立任务栈（栈按应用配置另计）；xtask 仅静态 TCB（coop 28 B / preempt 48 B 池槽），无栈。

---

## 5. 裁剪建议

1. 关日志（不选 `CONFIG_SYS_LOG_USE_*` 后端或减少日志量）——单条 `LOG_*` 宏即占空间，关掉省最多。
2. 用 `-Os` + `-ffunction-sections -fdata-sections -Wl,--gc-sections`（见 §6.2）去死代码。
3. 仅选 `CONFIG_OSAL_NULL` 后端（裸机）时最省，但需自己实现调度。
4. 不要编入不用的 VFS / HAL：依赖由 CMake 源集合决定，未引用即不进二进制。
5. `err_section` 仅在确有独立 ROM 区 / 诊断需求时保留 `error_symbols.ld` 链接。

---

---

## 6. 工程实例：stm32f103c8t6-node 内存优化记录

> 本节记录一次真实工程（`Host-Device-Architecture-stm32f103c8t6-node`，STM32F103C8T6，20 KB RAM / 64 KB Flash，`arm-none-eabi-gcc`，链接 `--gc-sections`，`.config` 见下）从"逼近溢出"到"安全区间"的完整优化演进。数据来自该工程历次构建日志（`build_log*.txt`）的真实输出。

### 6.1 演进总览（真实测量）

| 阶段 | RAM (B) | RAM 占比 | FLASH (B) | FLASH 占比 | 主要动作 |
| :--- | ---: | ---: | ---: | ---: | :--- |
| 初始（全功能编译） | 17,928 | 87.54% | 46,692 | 71.25% | 默认全开，RAM 逼近溢出 |
| 裁减驱动 | 17,928 | 87.54% | 45,724 | 69.77% | 移除未用产品驱动（air780e/hc05/dfplayer/neo_m8n 等） |
| 裁剪框架 | 15,376 | 75.08% | 45,636 | 69.64% | 进一步裁系统/中间件 |
| 关 Scr / SysCMD 等 | 12,456 | 60.82% | 39,636 | 60.48% | `CONFIG_SYSTEM_SCRUBBER` / `CONFIG_SYSTEM_CMD` 关 |
| **最终（Release -Os）** | **9,896** | **48.32%** | **22,692** | **34.63%** | `-Os` + gc-sections + 裁剪收敛 |

> 净效果：RAM **17,928 → 9,896 B（−44.8%）**，FLASH **46,692 → 22,692 B（−51.4%）**，从"RAM 87% 濒危"降到"RAM 48% 安全"。

### 6.2 采取的具体裁剪项（对应 `.config` 生效状态）

| Kconfig / 配置 | 取值 | 影响 |
| :--- | :--- | :--- |
| `CONFIG_OSAL_NULL` | `y` | 放弃 FreeRTOS/RT-Thread，用裸机 OSAL——RAM 下降主因（RTOS 每任务 TCB+独立栈，xtask 复用主循环栈） |
| `CONFIG_XTASK_PREEMPT` | `y` | 抢占式 xtask 协程 + `CONFIG_XTASK_COROUTINE` |
| `# CONFIG_SYSTEM_SCRUBBER` | 未设 | 关启动内存 scrubber |
| `# CONFIG_SYSTEM_CMD` | 未设 | 关命令行交互 |
| `CONFIG_SYSTEM_WDT` | `y` | 保留看门狗（安全项未裁） |
| `CONFIG_SYSTEM_CPP` | `y` | C++ system 后端 |
| 编译/链接 | `-Os -fdata-sections -ffunction-sections -Wl,--gc-sections` | 裁未引用函数/数据 |
| HAL / driver 源集合 | 按需 | `mini_tree/CMakeLists.txt` 仅编必要模块 |

### 6.3 关键结论

1. **`--gc-sections` 已生效，但对 RAM 几乎无效**：它裁的是"未引用的独立 section"（主要降 `text`/FLASH），而 RAM 大头是 `bss`（任务池、队列缓冲等静态数据）——这些总是被引用，gc 裁不掉。这正是 Release 仅比 Debug 省 ~1.7 KB FLASH、而 RAM 基本不变（9,776 → 9,768 B）的原因。
2. **RAM 主要靠功能裁剪，不靠优化级别**：换 `CONFIG_OSAL_NULL`、关 `SYSTEM_SCRUBBER`/`SYSTEM_CMD` 等才是降 RAM 的关键。
3. **静态库裁剪粒度受限**：`mini_tree` 是 `STATIC` 库，链接按 `.o` 粒度拉入，`--gc-sections` 只能裁 `.o` 内独立 section 且未被引用的部分；未拆 section 的全局数据仍会保留。
4. **若需进一步压 RAM**：调小队列缓冲（`CONFIG_OSAL_NULL_QUEUE_BUF_SZ`，当前 1024）、关 `CONFIG_EVENT_BUS`/`CONFIG_VIRQ`（当前为 `y`）、收缩任务池。

---
### 6.4 关于cpp
>C++ 体积可控的前提：`-fno-exceptions -fno-rtti` + 避免 iostream（用 printf / 裸输出）+ `--specs=nano.specs`。未裁剪时 C++ 可能 + 数 KB，属配置问题而非语言问题，但如果大量使用模板也会导致体积膨胀。
## 相关文档

- [getting_started.md](getting_started.md) · [design_decisions.md](design_decisions.md)
