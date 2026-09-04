/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file compiler_compat_poison.h
 *@brief compiler compat poison 头文件
 *@author H-000-H
 *@details
 *   compiler_compat_poison — 禁止堆分配与 stdio 输出的 GCC poison 层
 *   在标准头之后 include, 将 malloc/free/printf 等标记为毒死符号,
 *   嵌入式全栈禁止动态内存与标准 IO; 豁免宏: ALLOW_HEAP_ALLOC / ALLOW_STDIO_OUTPUT。
 */

#ifndef COMPILER_COMPAT_POISON_H
#define COMPILER_COMPAT_POISON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 须在 <stdio.h> / <stdlib.h> 等标准头之后 include（通常由 osal.h 末尾引入）。
 *
 * 豁免须在任意 #include 之前定义:
 *   ALLOW_HEAP_ALLOC   — calloc / free / malloc / realloc
 *   ALLOW_STDIO_OUTPUT — vprintf / my_printf_output
 *
 * 典型豁免: printf_output.c, osal_freertos.c, osal_null.c, osal_rtthread.c
 *
 * 注意: 不 poison system — Xtensa/ESP-IDF 头文件宏参数名会冲突.
 *
 * 内存 API: 项目代码须用 MINI_MEM_SET / MINI_MEM_COPY / MINI_MEM_MOVE
 * (compiler_compat.h), 禁止直接调用 memset/memcpy/memmove; 官方 SDK 目录除外.
 */
#if defined(__GNUC__)

#pragma GCC poison gets popen fopen fclose fread fwrite fseek ftell rewind tmpfile remove rename printf fprintf sprintf vsprintf asprintf dprintf strcpy strcat strdup strndup memset memcpy memmove

#if !defined(ALLOW_HEAP_ALLOC)
#pragma GCC poison malloc calloc realloc free
#ifdef __cplusplus
#pragma GCC poison new delete typeid dynamic_cast try catch throw
#endif
#endif

#if !defined(ALLOW_STDIO_OUTPUT)
#pragma GCC poison vprintf my_printf_output
#endif

#endif /* __GNUC__ */

#endif /* COMPILER_COMPAT_POISON_H */
