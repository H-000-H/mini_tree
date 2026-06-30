/* SPDX-License-Identifier: Apache-2.0 */
/*
 * osal_tick.h — RTOS tick 类型定义
 *
 * osal_tick_t 由 Kconfig CONFIG_OSAL_* 在编译期选定后端类型
 * FreeRTOS 用 TickType_t, RT-Thread 用 rt_tick_t, 裸机用 uint32_t
 * 调用方无需关心底层类型, 直接传递给 timeout_to_ticks 等接口
 */
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
