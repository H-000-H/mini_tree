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
