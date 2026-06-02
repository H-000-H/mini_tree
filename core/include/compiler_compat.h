#ifndef COMPILER_COMPAT_H
#define COMPILER_COMPAT_H

/* ── 编译器兼容抽象层 ──
 *
 * 统一 GCC / Clang / ARMCLANG (AC6) 的 __attribute__ 与内置函数差异。
 */

#define COMPAT_ALIGNED(n) __attribute__((aligned(n)))
#define COMPAT_WEAK(func) __attribute__((weak))
#define COMPAT_TRAP()     __builtin_trap()
#define COMPAT_CTZ(x)     __builtin_ctz(x)

#endif /* COMPILER_COMPAT_H */
