#ifndef COMPILER_COMPAT_H
#define COMPILER_COMPAT_H

/* ── 编译器兼容抽象层 ──
 *
 * 统一 GCC / Clang / ARMCLANG (AC6) 的 __attribute__ 与内置函数差异。
 */

#define COMPAT_ALIGNED(n) __attribute__((aligned(n)))
#define COMPAT_WEAK __attribute__((weak))
#define COMPAT_TRAP()     __builtin_trap()
#define COMPAT_CTZ(x)     __builtin_ctz(x)

/* ── RAM_EXEC: 将函数置于 RAM 执行 ──
 *
 * 将高频中断或控制环函数放入 .ram_code 段, 在启动时由用户 linker script
 * 搬运到 TCM 或 SRAM, 消除 Flash Cache Miss 导致的延迟抖动。
 *
 * 用法:
 *   RAM_EXEC void motor_foc_isr(void) { ... }
 *
 * 确认芯片有足够的 RAM/ITCM, 并在 .ld 中添加:
 *
 *   .ram_code : {
 *       *(.ram_code*)
 *   } > ITCM AT> FLASH
 *
 *   _sram_code = ADDR(.ram_code);
 *   _eram_code = ADDR(.ram_code) + SIZEOF(.ram_code);
 *   _ram_code_flash = LOADADDR(.ram_code);
 *
 *   startup 中: memcpy(&_sram_code, &_ram_code_flash, _eram_code - _sram_code);
 */
#define RAM_EXEC  __attribute__((section(".ram_code")))

#endif /* COMPILER_COMPAT_H */
