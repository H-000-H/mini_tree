/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file printf_output.c
 *@brief printf output 实现
 *@author H-000-H
 *@details
 *   printf_output — printf 风格格式化输出实现
 *   通过 ALLOW_STDIO_OUTPUT 启用标准 stdio, my_printf_output() 转调 vprintf
 *   作为受控的格式化输出 sink, 供需要 printf 语义的模块回调
 */

#define ALLOW_STDIO_OUTPUT

#include "printf_output.h"

#include "compiler_compat.h"
#include <stdarg.h>
#include <stdio.h>

/**
 * @brief vprintf 输出
 * @param[in] fmt 格式
 * @param ... 参数
 */
void my_printf_output(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
