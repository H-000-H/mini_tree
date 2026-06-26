#ifndef OSAL_TICK_H
#define OSAL_TICK_H

#include <stdint.h>

/* RTOS tick 类型 — 由 Kconfig OSAL 后端在编译期选定, 调用方无需强转 */
#if defined(CONFIG_OSAL_FREERTOS)

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#else
#include "FreeRTOS.h"
#endif
typedef TickType_t osal_tick_t;

#elif defined(CONFIG_OSAL_RTTHREAD)

#include <rtthread.h>
typedef rt_tick_t osal_tick_t;

#else

typedef uint32_t osal_tick_t;

#endif

#endif /* OSAL_TICK_H */
