/* SPDX-License-Identifier: Apache-2.0 */
/*
 * printf_output — printf 风格格式化输出实现
 *
 * 通过 ALLOW_STDIO_OUTPUT 启用标准 stdio, my_printf_output() 转调 vprintf
 * 作为受控的格式化输出 sink, 供需要 printf 语义的模块回调
 */
#define ALLOW_STDIO_OUTPUT

#include <stdarg.h>
#include <stdio.h>

#include "compiler_compat.h"
#include "printf_output.h"

void my_printf_output(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
