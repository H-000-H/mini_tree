/* SPDX-License-Identifier: Apache-2.0 */
/*
 * hal_stm32f407.c — STM32F407 mini_tree HAL 移植
 *
 * 映射 hal_if 接口到 CMSIS 寄存器 / STM32Cube 外设。
 * 无 WS2812 / pulse engine — 本板未使用该硬件。
 */
#include "hal_platform_safety.h"
#include "hal_amp.h"
#include "hal_wdt.h"
#include "hal_flash.h"

#include <stddef.h>
#include "compiler_compat.h"
#include "compiler_compat_poison.h"

void hal_pwm_force_stop_all(void)
{
}

void hal_cpu_emergency_stop_all_cores(void)
{
}

bool hal_wdt_init_rtc(uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(timeout_ms);
    return false;
}

void hal_wdt_feed_rtc(void) {}
void hal_wdt_rtc_set_long_timeout(void) {}
void hal_wdt_rtc_restore_timeout(void) {}

bool hal_wdt_init_twdt(uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(timeout_ms);
    return false;
}

bool hal_wdt_subscribe(void* task_handle)
{
    COMPAT_IGNORE_RESULT(task_handle);
    return false;
}

bool hal_wdt_unsubscribe(void* task_handle)
{
    COMPAT_IGNORE_RESULT(task_handle);
    return false;
}

void hal_wdt_feed_twdt(void) {}

bool hal_flash_read(uint32_t addr, uint8_t* buf, size_t len)
{
    if (!buf || len == 0) return false;

    COMPAT_MEM_COPY(buf, (const void*)addr, len);
    return true;
}

uint32_t hal_flash_get_app_addr(void)
{
    return 0;
}

uint32_t hal_flash_get_app_size(void)
{
    return 0;
}

void hal_platform_critical_hardware_lock(void)
{
    hal_pwm_force_stop_all();
}

void hal_platform_nmi_emergency_stamp(void)
{
}
