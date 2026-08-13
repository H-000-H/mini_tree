# 多核 AMP

> **AMP 是可选积木的一部分，按需自行使用**：默认单核即可完整开发；需要双核/异构时再启用，从核镜像与共享内存布局由你自行拼接。
>
> `CPU_CORES` / `AMP_MODE` 与 `hal_cpu_*`（目录 `hal/amp`）如何配合。
> **完整从核镜像与共享内存布局由平台工程提供**；本仓只给 HAL 契约与 OSAL spinlock 行为差异。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 双核 / AMP 板级工程师 |
| **前置** | [getting_started.md](getting_started.md) Kconfig · [device_tree_porting.md](device_tree_porting.md) |
| **相关** | [osal_switching.md](osal_switching.md) · [architecture.md](architecture.md) · [runtime_services.md](runtime_services.md) |

---

## 目录

1. [积木定位](#1-积木定位)
2. [Kconfig](#2-kconfig)
3. [HAL 契约](#3-hal-契约)
4. [推荐拓扑](#4-推荐拓扑)
5. [OSAL / 同步](#5-osal-同步)
6. [安全](#6-安全)
7. [验收](#7-验收)

---

## 1. 积木定位

AMP 属于**可选积木**（与安全类模块同类，见 [runtime_services.md](runtime_services.md) §5）：**默认不启用、按需自行使用**——不启用它，mini_tree 依旧完整可用。

| 项 | 说明 |
| :--- | :--- |
| 默认状态 | 单核（`CONFIG_CPU_CORES=1`）；设备模型 / VFS / OSAL / EventBus 等核心功能照常工作 |
| 启用方式 | 需要双核/异构时改 `CONFIG_CPU_CORES=2`（+ `CONFIG_AMP_MODE`） |
| 使用前提 | 平台侧自行提供：从核镜像、共享内存布局、核间通信（IPC）；本仓只给 HAL 契约与 OSAL 行为差异 |
| 参考实现 | [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore)（mini_tree 配套平台示例） |
| 不启用的影响 | **无**——单核配置是完整可用的基线 |

> 一句话：**AMP 是积木的一部分，按需自行使用**——不用它，mini_tree 依然完整；用它，从核与共享内存由你拼。

---

## 2. Kconfig

| 符号 | 含义 |
| :--- | :--- |
| `CONFIG_CPU_CORES` | `1` 单核（默认）；`2` 双核 |
| `CONFIG_AMP_MODE` | 依赖 `CPU_CORES > 1`；双核 AMP 时默认 `y` |

单核：无需实现从核启动；`hal_cpu_emergency_stop_all_cores` 主要关本核中断。
双核：平台必须实现从核入口，并保证共享资源协议明确。

---

## 3. HAL 契约

头：`hal/amp/hal_amp.h`（文件内宏名仍为 `HAL_CPU_H` 历史命名）。

| API | 用途 |
| :--- | :--- |
| `hal_cpu_secondary_startup` | 启动 / 释放从核（平台实现） |
| `hal_cpu_baremetal_entry` | 从核裸机入口（常由从核镜像调用） |
| `hal_cpu_get_id` | 当前核 ID |
| `hal_cpu_emergency_stop_all_cores` | 紧急停：单核关中断；双核还须挂起对端 |
| `hal_is_in_isr` / `hal_irq_*` | ISR 检测与 NVIC（Cortex-M inline；RISC-V 等平台覆盖） |

中间件 weak 默认为空/桩；**真机必须平台强符号**（至少 `emergency_stop` 与 `get_id` 有合理行为）。

---

## 4. 推荐拓扑

常见约定（可按 SoC 调整，但文档与 OSAL 按此假设）：

| 核 | 角色 |
| :--- | :--- |
| Core 0 | 跑 RTOS（FreeRTOS）+ mini_tree 主栈（probe、VFS、业务） |
| Core 1 | 裸机或轻循环：快路径、专用外设；经共享内存 / IPC 与 Core 0 通信 |

启动顺序建议：

1. Core 0：时钟、必要共享区初始化。
2. Core 0：`mini_tree_pre_os_init` → … → 调度器。
3. 在约定点调用 `hal_cpu_secondary_startup()`。
4. Core 1：从 `hal_cpu_baremetal_entry` 或复位向量进入自己的 loop。

本仓**不**内置核间消息协议；可用 EventBus（仅本核）+ 平台共享队列，或平台自研 IPC。

---

## 5. OSAL / 同步

- `CONFIG_OSAL_NULL` 下，AMP 时互斥等原语倾向 **原子 CAS**；单核可退化为关中断。
- Spinlock：`OSAL_SPINLOCK_IRQ_DISABLE` vs `ATOMIC` — 多核共享数据优先 atomic，见 [osal_switching.md](osal_switching.md)。
- **不要**假设另一核上的 `device_*` 锁对你可见；跨核只走明确的共享对象。

---

## 6. 安全

`safe_state` / `hal_cpu_emergency_stop_all_cores` / `hal_platform_safety`：故障时须能停**所有**会伤人的输出。
双核时务必实现「挂起对端」或等价静默，而不是只 `cpsid` 本核。

> 这些安全能力同属**可选积木**（见 [runtime_services.md](runtime_services.md) §5）：推荐启用，不启用不影响单核核心开发。

---

## 7. 验收

- [ ] `CPU_CORES=2` 时从核能独立跑通最小 loop
- [ ] 共享区无数据竞争（或有原子/锁协议）
- [ ] 注入故障后两核输出进入安全态
- [ ] 单核配置回退：不链从核也能正常启动

平台侧样例工程（链接脚本、从核镜像）不在本 shelf；可参考 [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore)（mini_tree 配套平台示例）。

---

## 相关文档

- [device_tree_porting.md](device_tree_porting.md) · [osal_switching.md](osal_switching.md) · [design_decisions.md](design_decisions.md)
- [runtime_services.md](runtime_services.md) · [todolist.md](todolist.md)（AMP 样例跟踪）
