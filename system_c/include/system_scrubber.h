#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool system_scrubber_init(void);
bool system_scrubber_start(void);
bool system_scrubber_is_running(void);

#ifdef __cplusplus
}
#endif
