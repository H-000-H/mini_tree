/* SPDX-License-Identifier: Apache-2.0 */
/*
 * compiler_inline — static inline 组合宏 (无 VFS 依赖, 供头文件循环 include 安全使用)
 */
#ifndef COMPILER_INLINE_H
#define COMPILER_INLINE_H

/** @brief static + always_inline 组合宏 */
#if defined(__GNUC__) || defined(__clang__)
#define COMPAT_STATIC_INLINE static inline __attribute__((always_inline))
#else
#define COMPAT_STATIC_INLINE static inline
#endif

#endif /* COMPILER_INLINE_H */
