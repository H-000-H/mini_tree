# Changelog

## [Unreleased]

### mini-tree-example 示例工程加固

- **`syscalls.c`** — `_sbrk` 堆栈碰撞检测：`heap_end + incr > &_estack - 4096` 时返回 `ENOMEM`，留 4KB 安全余量防止 newlib `malloc` 踩进栈
- **`stm32f407zgt6.ld`** — CCM 64KB 上线：新增 `.ccm (NOLOAD)` 段，`__attribute__((section(".ccm")))` 可将 FreeRTOS `ucHeap`、关键任务栈或 ISR 数据放入零等待 SRAM
- **`hal_stubs.c`** — 栈溢出钩子加固：`cpsid i` 锁死全局中断后再 `for (;;);`，任务栈溢出后系统立即冻结，不给 ISR 继续破坏的机会

### RT-Thread 端口修复

- **`lib/rtthread/libcpu/arm/cortex-m7/context_gcc.S`** — HardFault_Handler FPU 现场修复：原实现仅压入占位 flag，未保存实际 FPU 寄存器 (d8-d15 / s16-s31)。对齐 CM4F 实现，增加 `TST lr, #0x10` 检查 EXC_RETURN bit4，FPU 活跃时 `VSTMDBEQ` 保存 d8-d15，再按 flag → exec_return 顺序压栈。修复后 FPU 异常现场可被 `rt_hw_hard_fault_exception` 完整捕获。
- **`lib/rtthread/libcpu/arm/cortex-m4/cpuport.c`** — `rt_interrupt_enter/leave` 中断嵌套计数器修复：原实现为空桩（仅 hook + log），`rt_interrupt_nest` 全局计数器永不递增。补入 `extern volatile rt_atomic_t rt_interrupt_nest`，enter 做 `rt_atomic_add`，leave 做 `rt_atomic_sub`，与 `irq.c` 通用弱实现保持一致。CM7/CM3/RISC-V 端口无此问题（均走通用实现）。

### Keil 工具链重构: ARMCC v5 → ARMCLANG (AC6)

- **`Makefile`**: `TOOLCHAIN=keil5` 从 ARMCC v5 (`armcc`/`armasm`) 切换至 ARMCLANG AC6 (`armclang`)，与 keil6 共享同一 LLVM/Clang 后端
- **`tools/gen_uvprojx.py`**: `.uvprojx` 生成目标从 ARMCC v5 改为 AC6，`MiscControls` 使用 GCC 风格 flags (`-std=c17 -Wall`)
- **AC6 原生支持** — 无需 ARMCC v5 专用 workaround：
  - GNU `.S` 汇编文件（`context_gcc.S` 等）不再跳过
  - `enum class` / C++17 全量支持，`lifecycle.cpp`、`system_runtime.cpp` 恢复编译
  - `__attribute__((constructor))` 原生支持
  - `__atomic_*` 内置函数原生支持，删除 `compat/armcc/stdatomic.h` 存根
  - FreeRTOS GCC 端口（GCC 内联汇编）可编译
- **构建验证** — CM3+RTTHREAD / CM4F+FREERTOS / CM7+NULL 三种组合均通过 ARMCLANG 编译

### Makefile 构建改进

- **默认目标修复**: 新增 `.DEFAULT_GOAL := all`，`make` 裸命令正确构建全部库而非仅 config.h
- **AC6 标准降级**: ARMCLANG 6.x 不支持 `-std=c23`，Kconfig 标准覆盖时自动降级 c23→c17、c++23→c++17
- **自动检测**: keil5 工具链自动定位 `C:/Keil_v5/ARM/ARMCLANG/bin/armclang.exe` 和 `armar.exe`

### dtc-lite.py 优化

- **ROM 安全**: probe/remove 函数指针表添加 `const`，从 `.data` 移至 `.rodata`，防 RAM 篡改
- **增量构建**: 新增 `_write_if_changed()` 防抖写入，内容不变时不碰磁盘时间戳，消除雪崩重编
- **Kconfig 隔离**: extern 声明添加 `__attribute__((weak))`，被 Kconfig 裁剪的驱动不会导致链接器 Undefined Reference
- **扫描性能**: `_scan_drivers()` 跳过 >1MB 的大文件，避免 SDK 巨型文件被整吞

### OSAL & RT-Thread 修复

- **`osal_rtthread.c`**: 适配 RT-Thread v5.x 内核 API。修复信号量/互斥量 count 访问、时基单位转换（OS ticks → RT-Thread `RT_TICK_PER_SECOND`）、OP和MQ默认初始化宏
- **`osal.h`**: ARMCC v5 C++ 模式 `UINT32_MAX` 缺失兼容处理（AC6 切换后移除）
- **`osal_freertos.c` / `osal_null.c`**: 同步信号量 count 类型适配

### 构建脚本硬化 — 全量类型注解 + argparse + 原子写入

所有 `tools/` Python 脚本统一遵循两条安全基线：

**原子写入**：生成 `.h`/`.c` 文件采用先写 `.tmp` 再 `shutil.move()` 替换的模式，防止中断留下残缺文件。写入前通过 `_write_if_changed()`/`_needs_update()` 做内容比对，内容不变时不动磁盘，保护 Makefile 时间戳避免雪崩重编。

**显式类型注解**：每个函数/方法的参数、返回值、局部变量均显式标注类型，不依赖自动推导。

| 脚本 | 注解规模 |
|------|---------|
| `genconfig.py` | 76 处 |
| `post_build_crc.py` | 98 处 |
| `gen_uvprojx.py` | 155 处 |
| `p2s.py` | 70 处 |
| `menuconfig.py` | 15 处 |
| `dtc-lite.py` | ~1200 处 |

可在 CI 中通过 `mypy tools/ --strict` 做静态类型检查。

各脚本具体变更：

- **`tools/genconfig.py`**: `argparse` 替换裸 `sys.argv`，`pathlib.Path` 替换 `os.path`，新增 `_atomic_write()` 和 `_needs_update()` 内容比对防抖
- **`tools/dtc-lite.py`**: `argparse` 替换裸 `sys.argv`，`pathlib.Path` 替换 `os.path`，新增 `_atomic_write()`，移除未使用 `tempfile`/`OrderedDict` 导入
- **`tools/post_build_crc.py`**: `pathlib.Path` 替换 `os.path`，新增 `_atomic_write()` + `_needs_update()`，提取 `_compute_crc()`/`_replace_in_file()`/`_write_new_header()` 辅助函数
- **`tools/gen_uvprojx.py`**: 155 处显式类型注解（`scan_src()` → `List[str]`、`discover_sources()` → `Dict[str, List[str]]` 等）
- **`tools/p2s.py`**: 70 处显式类型注解（`run_make()` → `int`、`show_combinations()` → `None` 等）
- **`tools/menuconfig.py`**: 15 处显式类型注解（`main()` → `int` 等）

### C 修复

- **`core/include/system_log.hpp`**: 新增 `DRV_LOGV` 宏 — `osal_log(OSAL_LOG_DEBUG, tag, fmt, ...)`，修复 board_driver.c 编译错误
- **`core/include/compiler_compat.h`**: `COMPAT_WEAK` 从函数式宏 `#define COMPAT_WEAK(func) __attribute__((weak)) func` 重构为属性宏 `#define COMPAT_WEAK __attribute__((weak))`，消除 implicit-int 冲突；同步更新 `osal_freertos.c`、`osal_rtthread.c`、`osal_null.c` 共 6 处调用点

### 文档新增 (已合并至此文件，源文件已删除)

日志系统分为 **SYS_LOG（系统日志）** 和 **DRV_LOG（驱动日志）** 两层：

```
SYS_LOG — 应用/框架层，Kconfig 可选择后端
  ├── CONFIG_SYS_LOG_USE_OSAL   → osal_log()
  ├── CONFIG_SYS_LOG_USE_ESP    → ESP_LOGI/LOGW/LOGE
  └── CONFIG_SYS_LOG_USE_PRINTF → printf（默认，无外部依赖）

DRV_LOG — 驱动层，固定走 osal_log()
  ├── DRV_LOGE / DRV_LOGW       → Error/Warning 同时推入 production_log 黑匣子
  └── DRV_LOGI / DRV_LOGD / DRV_LOGV → Info/Debug/Verbose 仅 osal_log()
```

- SYS_LOG 后端由 Kconfig 编译期选择，运行时不可切换
- DRV_LOG 固定在 `osal_log()` 之上，`DRV_LOGE`/`DRV_LOGW` 额外推入黑匣子用于故障事后分析
- `DRV_LOGV` 可在 `NDEBUG` 时被编译器优化剔除
- 各模块统一包含 `system_log.hpp`，不直接调用 `printf` 或 `osal_log`

### genconfig.py 修复

- **magic number**: `_C_SPACES_INDENT` 改为 `self._indent` 单源，整树一致
- **STRING 引号**: `int` 类型值误加引号导致 `make` 比较失败，按 Kconfig 类型严格处理
- **copy2 时间戳**: `shutil.copy2` 保留 mtime 导致增量构建防抖误判，改用 `shutil.move` 避免时间戳传播
