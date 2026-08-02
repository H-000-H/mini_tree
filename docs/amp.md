# 多核 AMP / Heterogeneous Multi-Core AMP

> **异构多核（AMP）也是一个可拼接的可选积木**：默认单核即可开发；需要双核/异构时按 Kconfig 启用，并自行拼接平台侧的从核镜像与共享内存布局。
> **Heterogeneous multi-core (AMP) is also an assemblable optional brick**: single-core is the default for development; enable dual-core/hetero via Kconfig when needed and assemble the secondary-core image & shared-memory layout on the platform side.
>
> `CPU_CORES` / `AMP_MODE` 与 `hal_cpu_*`（目录 `hal/amp`）如何配合 / How `CPU_CORES`, `AMP_MODE` and `hal_cpu_*` (in `hal/amp`) fit together.
> **完整从核镜像与共享内存布局由平台工程提供**；本仓只给 HAL 契约与 OSAL spinlock 行为差异。
> **The full secondary-core image and shared-memory layout are provided by the platform project**; this repo only defines the HAL contract and OSAL spinlock behavior differences.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 双核 / AMP 板级工程师 / Dual-core & AMP board engineers |
| **前置 / Prereq** | [getting_started.md](getting_started.md) Kconfig · [porting_guide.md](porting_guide.md) |
| **相关 / Related** | [osal_switching.md](osal_switching.md) · [architecture.md](architecture.md) · [runtime_services.md](runtime_services.md) |

---

## 目录 / Contents

1. [Kconfig](#1-kconfig)
2. [HAL 契约 / HAL Contract](#2-hal-契约--hal-contract)
3. [推荐拓扑 / Recommended Topology](#3-推荐拓扑--recommended-topology)
4. [OSAL / 同步 / Synchronization](#4-osal--同步--synchronization)
5. [安全 / Safety](#5-安全--safety)
6. [验收 / Acceptance](#6-验收--acceptance)

---

## 1. Kconfig

| 符号 / Symbol | 含义 / Meaning |
| :--- | :--- |
| `CONFIG_CPU_CORES` | `1` 单核（默认）/ single-core (default)；`2` 双核 / dual-core |
| `CONFIG_AMP_MODE` | 依赖 `CPU_CORES > 1`；双核 AMP 时默认 `y` / Depends on `CPU_CORES > 1`; `y` by default in AMP |

单核：无需实现从核启动；`hal_cpu_emergency_stop_all_cores` 主要关本核中断。
Single-core: no secondary startup needed; `hal_cpu_emergency_stop_all_cores` mostly disables local interrupts.
双核：平台必须实现从核入口，并保证共享资源协议明确。
Dual-core: the platform must implement the secondary entry and a clear shared-resource protocol.

---

## 2. HAL 契约 / HAL Contract

头 / Header：`hal/amp/hal_amp.h`（文件内宏名仍为 `HAL_CPU_H` 历史命名 / the internal guard keeps the legacy `HAL_CPU_H` name）。

| API | 用途 / Use |
| :--- | :--- |
| `hal_cpu_secondary_startup` | 启动 / 释放从核（平台实现）/ Start/release the secondary core (platform) |
| `hal_cpu_baremetal_entry` | 从核裸机入口（常由从核镜像调用）/ Bare-metal entry on the secondary core (called by its image) |
| `hal_cpu_get_id` | 当前核 ID / Current core ID |
| `hal_cpu_emergency_stop_all_cores` | 紧急停：单核关中断；双核还须挂起对端 / Emergency stop: disable IRQs on single-core; must also suspend the peer on dual-core |
| `hal_is_in_isr` / `hal_irq_*` | ISR 检测与 NVIC（Cortex-M inline；RISC-V 等平台覆盖）/ ISR detection & NVIC (Cortex-M inline; overridden on RISC-V etc.) |

中间件 weak 默认为空/桩；**真机必须平台强符号**（至少 `emergency_stop` 与 `get_id` 有合理行为）。
Middleware weak stubs default to no-ops; **real hardware needs platform strong symbols** (at least `emergency_stop` and `get_id` behaving sensibly).

---

## 3. 推荐拓扑 / Recommended Topology

常见约定（可按 SoC 调整，但文档与 OSAL 按此假设）：
Common convention (adjustable per SoC; docs & OSAL assume this):

| 核 / Core | 角色 / Role |
| :--- | :--- |
| Core 0 | 跑 RTOS（FreeRTOS / RT-Thread）+ mini_tree 主栈（probe、VFS、业务）/ Runs the RTOS + mini_tree main stack (probe, VFS, business) |
| Core 1 | 裸机或轻循环：快路径、专用外设；经共享内存 / IPC 与 Core 0 通信 / Bare-metal or light loop: fast path, dedicated peripherals; communicates via shared memory / IPC |

启动顺序建议 / Suggested boot order：

1. Core 0：时钟、必要共享区初始化 / clocks and required shared-region init.
2. Core 0：`mini_tree_pre_os_init` → … → 调度器 / scheduler.
3. 在约定点调用 `hal_cpu_secondary_startup()` / call at the agreed point.
4. Core 1：从 `hal_cpu_baremetal_entry` 或复位向量进入自己的 loop / enters its own loop from `hal_cpu_baremetal_entry` or the reset vector.

本仓**不**内置核间消息协议；可用 EventBus（仅本核）+ 平台共享队列，或平台自研 IPC。
This repo does **not** ship an inter-core message protocol; use EventBus (local core only) + a platform shared queue, or a custom IPC.

---

## 4. OSAL / 同步 / Synchronization

- `CONFIG_OSAL_NULL` 下，AMP 时互斥等原语倾向 **原子 CAS**；单核可退化为关中断。
  Under `CONFIG_OSAL_NULL`, AMP prefers **atomic CAS** for mutex-like primitives; single-core can fall back to IRQ disable.
- Spinlock：`OSAL_SPINLOCK_IRQ_DISABLE` vs `ATOMIC` — 多核共享数据优先 atomic，见 [osal_switching.md](osal_switching.md)。
  Spinlock: `OSAL_SPINLOCK_IRQ_DISABLE` vs `ATOMIC` — prefer atomic for multi-core shared data, see [osal_switching.md](osal_switching.md).
- **不要**假设另一核上的 `device_*` 锁对你可见；跨核只走明确的共享对象。
  **Never** assume `device_*` locks on the other core are visible to you; cross-core traffic goes through explicit shared objects.

---

## 5. 安全 / Safety

`safe_state` / `hal_cpu_emergency_stop_all_cores` / `hal_platform_safety`：故障时须能停**所有**会伤人的输出。
On fault, all hazard-capable outputs must be stopped: `safe_state` / `hal_cpu_emergency_stop_all_cores` / `hal_platform_safety`.
双核时务必实现「挂起对端」或等价静默，而不是只 `cpsid` 本核。
On dual-core, implement "suspend the peer" or an equivalent silent state — not just `cpsid` on the local core.

> 这些安全能力同属**可选积木**（见 [runtime_services.md](runtime_services.md) §5）：推荐启用，不启用不影响单核核心开发。
> These safety capabilities are likewise **optional bricks** (see [runtime_services.md](runtime_services.md) §5): recommended, but optional for single-core core development.

---

## 6. 验收 / Acceptance

- [ ] `CPU_CORES=2` 时从核能独立跑通最小 loop / secondary core runs a minimal loop at `CPU_CORES=2`
- [ ] 共享区无数据竞争（或有原子/锁协议）/ no data races on shared regions (or an atomic/lock protocol)
- [ ] 注入故障后两核输出进入安全态 / both cores reach a safe output state on injected faults
- [ ] 单核配置回退：不链从核也能正常启动 / single-core fallback: boots fine without the secondary core

平台侧样例工程（链接脚本、从核镜像）不在本 shelf；见各 SoC 仓库。
Platform sample projects (link scripts, secondary images) live outside this shelf; see the per-SoC repos.

---

## 相关文档 / Related Documents

- [porting_guide.md](porting_guide.md) · [osal_switching.md](osal_switching.md) · [design_decisions.md](design_decisions.md)
- [runtime_services.md](runtime_services.md) · [todolist.md](todolist.md)（AMP 样例跟踪 / AMP sample tracking）
