# 待优化

- **MPU/PMP 内存保护** — 为每个驱动分配内存白名单，越权写入即触发 MemManage Fault 停机。
- **Power Management** — 缺少 suspend/resume 框架，OSAL 层已预留接口，待实现。
- **64-bit 架构适配** — 当前仅在 32-bit 平台验证，RISC-V 64 / ARMv8-A 需适配。
- **BufferPool 池组** — 单池上限 32 个 buffer（`uint32_t` 位图限制）。需要大于 32 的组件需自行管理多个池。内置池组机制，内部用多字位图扩展容量，对外保持 `bp_alloc/bp_free` 接口不变。
- **IAR 工具链支持** — 当前仅适配了 GCC / ARMCLANG，未验证 IAR。`__attribute__`、内联汇编、段声明等需要 IAR 等效写法兼容。作者对 IAR 不熟悉，未来可能拓展。
- **上位机 QT** —— 当前只有 Kconfig 的图形化界面太土了。因为本人不会 QT 所以写不出上位机，在未来学会 QT 之后会回来弥补缺少上位机的这一个缺陷。