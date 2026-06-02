# Changelog

## [Unreleased]

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

### genconfig.py 修复

- **magic number**: `_C_SPACES_INDENT` 改为 `self._indent` 单源，整树一致
- **STRING 引号**: `int` 类型值误加引号导致 `make` 比较失败，按 Kconfig 类型严格处理
- **copy2 时间戳**: `shutil.copy2` 保留 mtime 导致增量构建防抖误判，改用 `shutil.move` 避免时间戳传播
