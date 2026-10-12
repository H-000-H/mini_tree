#include "mbedtls_config_boot.h"
void mbedtls_port_init(void)
{
#ifdef MBEDTLS_PLATFORM_MEMORY_C
    mbedtls_platform_set_calloc_free(mini_os_calloc, mini_os_free);
#endif

#ifdef MBEDTLS_PLATFORM_TIME_ALT
    mbedtls_platform_set_time(mini_os_get_tick);
#endif
}
