# 内存与 flash 基准

> 编译产物大小与段分布（工具链相关）。生产构建务必打开 `CONFIG_SYS_LOG_LEVEL=0`（关日志）/ `CONFIG_BUILD_SIZE=1`（编大小）/ `CONFIG_BUILD_SIZE_REPORT=1`（出报告）；`CONFIG_BUILD_NO_LTO=0`（开 LTO，强烈推荐）。
>
> 报告脚本：`tools/build_size.py`。段布局用 `--format=html` 看分布，或 `--format=baseline` 看基线对照。详见 [getting_started.md](getting_started.md) §4.3。

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
| `err_section` | 错误符号表（`ERR_SECTION_BASE`）；`CONFIG_ERR_SECTION=1` 时单独放置——独立 ROM 区或 RAM 镜像 |
| `*.noinit` | WDT/RTC 等不被初始化的 RAM |
| `.log_*` | 日志注册表（关日志后移除） |

> WDT 与 `safe_state` 同属 `system`，在 `CONFIG_WDT=1` / `CONFIG_SAFE_STATE=1` 时编入；烧录后与中断、`err_section` 需保证物理不被覆盖。

---

## 2. 控制项

| Kconfig | 作用 |
| :--- | :--- |
| `CONFIG_SYS_LOG_LEVEL` | 0 = 关日志（默认编译保留） |
| `CONFIG_BUILD_SIZE` | 1 = 编大小（去掉调试信息） |
| `CONFIG_BUILD_SIZE_REPORT` | 1 = 出报告 |
| `CONFIG_BUILD_NO_LTO` | 0 = 开 LTO（推荐默认） |
| `CONFIG_ERR_SECTION` | 1 = 错误符号单独段（见 §1 表） |
| `CONFIG_WDT` / `CONFIG_SAFE_STATE` | WDT / safe_state 编入（见 §1 表） |

---

## 3. 基准

> 单位 KiB。示例数字，随重构与新积木会变化（周期性重测）。

| 配置 | text | rodata | data | bss | flash 合计 | 说明 |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| 最小（仅 `osal` + 框架） | 6.2 | 1.1 | 0.3 | 2.4 | 7.6 | `CONFIG_OSAL_NULL` + 空板 |
| + 设备模型 | 11.8 | 2.0 | 0.6 | 4.1 | 13.8 | `board/` 全部 |
| + 一个 VFS 设备（uart） | 15.3 | 2.6 | 0.8 | 5.2 | 18.7 | `vfs/uart` |
| + FreeRTOS 后端 | 19.1 | 3.3 | 1.1 | 6.9 | 23.5 | `CONFIG_OSAL_FREERTOS` |
| + WDT + safe_state | 20.4 | 3.5 | 1.2 | 7.3 | 24.9 | `CONFIG_WDT` + `CONFIG_SAFE_STATE` |

> 上表为 GCC `-Os` + LTO 估算。开日志（`CONFIG_SYS_LOG_LEVEL>0`）各档增 ~3–8 KiB `rodata`/`text`；关日志最划算。
>
> 口径说明：本表为 **flash 合计**（text+rodata+data），与 [CHANGELOG.md](../../CHANGELOG.md) 中"全库 85.3→28.0 KB / 默认最小 ≈ 2.8 KB"的**静态 RAM（bss + data）下限**叙事口径不同（后者是历史压缩成果、且只计 RAM），二者不可直接比较；最新 RAM 下限以本表 `bss`/`data` 列为准。

---

## 4. 调度方案对比（最小固件实测）

> 实测：`arm-none-eabi-gcc 14.2.1`，`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections -fdata-sections` + `--gc-sections`；最小固件 = `startup` + `main`（system 层）+ 链接 `mini_tree` 全库（含 RTOS 内核），链接脚本仿 STM32F4。单位 B，`RAM 合计 = data + bss`。

| 调度方案 | system 后端 | text | data | bss | RAM 合计 |
| :--- | :--- | ---: | ---: | ---: | ---: |
| 全裸 `while`（`XTASK_NONE`） | 无 | 86 | 0 | 0 | 0 |
| 协调式 `XTASK_COOP` | C | 31240 | 1744 | 944 | 2688 |
| 协调式 `XTASK_COOP` | C++ | 31240 | 1744 | 944 | 2688 |
| 抢占式 `XTASK_PREEMPT` | C | 31368 | 1744 | 1388 | 3132 |
| 抢占式 `XTASK_PREEMPT` | C++ | 31368 | 1744 | 1388 | 3132 |
| FreeRTOS | C | 34312 | 1752 | 1304 | 3056 |
| FreeRTOS | C++ | 34696 | 1752 | 1656 | 3408 |
| RT-Thread | C | 40228 | 1892 | 1164 | 3056 |
| RT-Thread | C++ | 40612 | 1892 | 1516 | 3408 |
| ThreadX | C | 34120 | 1752 | 940 | 2692 |
| ThreadX | C++ | 34504 | 1752 | 1292 | 3044 |
| uC/OS-II | C | 32992 | 1744 | 872 | 2616 |
| uC/OS-II | C++ | 33376 | 1744 | 1224 | 2968 |
| uC/OS-III | C | 33448 | 1744 | 1404 | 3148 |
| uC/OS-III | C++ | 33832 | 1744 | 1756 | 3500 |

> 口径：全裸（`XTASK_NONE`）下 `OSAL_NULL_TASK_CPP` 由 Kconfig 自动关闭（`depends on !XTASK_NONE`），且 osal/system 层依赖 xtask 接口（`osal_null.h` 无条件 include `xtask.h`），无实现时无法链接，固件退化为最小闭包（startup + 主循环手动轮询），不含 system/osal 层；RTOS 后端 `text` 已含各自内核；数字含全库（board 设备模型等），**相对差**更有效。裸机调度三态（`XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`）由 `Kconfig.mini_tree` 的 choice 选择，CMake 据此注入 `MINI_TREE_XTASK_*` 宏决定编译 `xtask_coop.c` 或 `xtask_preempt.c`；抢占式与协调式对外 API 完全一致（`xscheduler_task_create`/`x_scheduler_poll`/`xscheduler_start`），调用方无感切换。

结论：
1. 全裸最省（86 B text，零 RAM）；代价是调度逻辑全部自写。
2. 裸机 xtask（coop/preempt，~31 KB text）比最小 RTOS 内核（uC/OS-II ~33 KB）还省 ~1.7 KB，且 **无独立任务栈**（run-to-completion，任务栈复用主循环栈）；preempt 的 bss 多出任务池（8×48 B + 位图/链表头 ≈ 444 B）。
3. RTOS 内核开销：uC/OS-II < uC/OS-III < ThreadX < FreeRTOS < RT-Thread（text 33.0 → 40.2 KB）。
4. C/C++ system 后端：裸机（coop/preempt）完全一致；RTOS 下 C++ 比 C 约多 +300~380 B text、+350~440 B bss。**选 C 后端最省**。
5. 每任务额外成本：RTOS 需 TCB + 独立任务栈（栈按应用配置另计）；xtask 仅静态 TCB（coop 28 B / preempt 48 B 池槽），无栈。

---

## 5. 裁剪建议

1. 关日志（`CONFIG_SYS_LOG_LEVEL=0`）——单条 `LOG_*` 宏即占空间，关掉省最多。
2. 开 LTO（`CONFIG_BUILD_NO_LTO=0`）——链接期合并重复、去死代码。
3. 仅选 `CONFIG_OSAL_NULL` 后端（裸机）时最省，但需自己实现调度。
4. 不要编入不用的 VFS / HAL：依赖由 CMake 源集合决定，未引用即不进二进制。
5. `err_section` 仅在确有独立 ROM 区 / 诊断需求时开 `CONFIG_ERR_SECTION=1`。

---

## 相关文档

- [getting_started.md](getting_started.md) · [design_decisions.md](design_decisions.md)
