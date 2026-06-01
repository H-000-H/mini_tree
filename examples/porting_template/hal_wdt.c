/*
 * hal_wdt.c — 看门狗定时器移植模板
 *
 * 两种独立的看门狗类型：
 *   RTC_WDT: 来自独立时钟源的硬件看门狗。
 *            即使 CPU 总线停滞（SysTick 停止）也会触发。
 *            用作最后的安全网。
 *
 *   TWDT:    任务看门狗，监控订阅的任务是否定期喂狗。
 *            超时：核心转储 + 硬件复位。
 */

#include "hal_wdt.h"
#include <stdint.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  RTC 看门狗
 * ═══════════════════════════════════════════════════════════════════════════ */

bool hal_wdt_init_rtc(uint32_t timeout_ms)
{
    /*
     * TODO: 配置 RTC/独立看门狗定时器。
     *
     * ARM IWDG:
     *   IWDG->KR = 0x5555;  // 启用写入
     *   IWDG->PR = div;     // 预分频器
     *   IWDG->RLR = reload; // 周期
     *   IWDG->KR = 0xCCCC;  // 启动
     *   return true;
     *
     * ESP32:
     *   rtc_wdt_protect_off();
     *   rtc_wdt_set_time(RTC_WDT_STAGE0, timeout_ms * 1000);
     *   rtc_wdt_enable();
     *   rtc_wdt_protect_on();
     *   return true;
     */
    (void)timeout_ms;
    return false;
}

void hal_wdt_feed_rtc(void)
{
    /*
     * TODO: 复位 RTC/独立看门狗计数器。
     *
     * ARM IWDG:
     *   IWDG->KR = 0xAAAA;
     *
     * ESP32:
     *   rtc_wdt_feed();
     */
}

void hal_wdt_rtc_set_long_timeout(void)
{
    /*
     * TODO: 延长 RTC 看门狗超时时间（例如用于 OTA 更新）。
     */
}

void hal_wdt_rtc_restore_timeout(void)
{
    /*
     * TODO: 恢复 RTC 看门狗的正常超时时间。
     */
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  任务看门狗 (TWDT)
 * ═══════════════════════════════════════════════════════════════════════════ */

bool hal_wdt_init_twdt(uint32_t timeout_ms)
{
    /*
     * TODO: 初始化任务看门狗定时器。
     *
     * ESP-IDF:
     *   return esp_task_wdt_init(timeout_ms, true) == ESP_OK;
     *
     * ARM + FreeRTOS:
     *   // 使用硬件定时器在 timeout_ms 内未被喂狗时触发 NMI。
     *   // 没有标准的 ARM 等效方案 — 使用周期性定时器 ISR 实现，
     *   // 检查每个任务的喂狗标志。
     */
    (void)timeout_ms;
    return false;
}

bool hal_wdt_subscribe(void* task_handle)
{
    /*
     * TODO: 向 TWDT 订阅一个 FreeRTOS 任务句柄。
     */
    (void)task_handle;
    return false;
}

bool hal_wdt_unsubscribe(void* task_handle)
{
    /*
     * TODO: 从 TWDT 取消订阅一个任务句柄。
     */
    (void)task_handle;
    return false;
}

void hal_wdt_feed_twdt(void)
{
    /*
     * TODO: 为当前任务喂狗（复位）TWDT 定时器。
     */
}
