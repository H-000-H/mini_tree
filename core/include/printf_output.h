#ifndef PRINTF_OUTPUT_H
#define PRINTF_OUTPUT_H

#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" 
{
#endif

void my_printf_output(const char *fmt, ...) COMPAT_FMT_PRINTF(1, 2);

#ifdef __cplusplus
}
#endif

#endif /* PRINTF_OUTPUT_H */
