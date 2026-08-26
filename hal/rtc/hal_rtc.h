/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_rtc.h
 *@brief hal rtc 头文件
 *@author H-000-H
 *@details
 *   @note  set_alarm 会配置 ALRA 并 EnableIT_ALRA; 当前无完整 NVIC/ISR→callback 派发路径。
 *   @note  close 仅清软件状态, 不关闭 RTC 时钟 (日历持续运行)。
 *   @note  文件约定: 返回值用 int + status.h 错误码; 禁止 enum。
 */

#ifndef HAL_RTC_H
#define HAL_RTC_H

#include "compiler_compat.h"
#include "status.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 日历时间
     */
    struct hal_rtc_time
    {
        uint16_t year; /**< 年 (完整年份, 如 2026) */
        uint8_t month; /**< 月 1..12 */
        uint8_t day; /**< 日 1..31 */
        uint8_t hour; /**< 时 0..23 (24h) 或按 format_24h */
        uint8_t minute; /**< 分 0..59 */
        uint8_t second; /**< 秒 0..59 */
        uint8_t weekday; /**< 星期 1..7 */
    };

    /**
     * @brief RTC 硬件配置 (DTSI 直投)
     */
    struct hal_rtc_config
    {
        uintptr_t rtc; /**< RTC 寄存器基址 */
        uint32_t clk_source; /**< 时钟源 (LSE/LSI/HSE 分频等 LL 宏) */
        uint32_t async_prediv; /**< 异步预分频 */
        uint32_t sync_prediv; /**< 同步预分频 */
        uint32_t format_24h; /**< 1=24小时制, 0=12小时制 */
        int32_t irqn; /**< NVIC 中断号; -1=不注册 */
        uint32_t irq_priority; /**< NVIC 优先级 */
    };

    struct hal_rtc_dev;

    /** 闹钟回调类型 (ISR 上下文; 当前平台未完成派发) */
    typedef void (*hal_rtc_alarm_cb_t)(struct hal_rtc_dev* pdev, void* user);

    /**
     * @brief RTC 设备对象
     */
    struct hal_rtc_dev
    {
        struct hal_rtc_config cfg; /**< 配置 */
        int hw_open; /**< open 引用/状态 */
        hal_rtc_alarm_cb_t alarm_cb; /**< 闹钟回调 (寄存器路径已配, ISR 派发待补) */
        void* alarm_user; /**< 回调用户数据 */
    };

    /**
     * @brief 初始化 RTC 软件对象
     * @param[in] pdev RTC 设备对象指针
     * @param[in] cfg RTC 硬件配置 (DTSI 直投)
     * @return 成功返回 MINI_OK, pdev 或 cfg 为空返回 MINI_ERR_INVAL
     */
    int hal_rtc_init(struct hal_rtc_dev* pdev,
                     const struct hal_rtc_config* cfg) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 反初始化 RTC 软件对象
     * @param[in] pdev RTC 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_rtc_deinit(struct hal_rtc_dev* pdev) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 打开 RTC (引用计数 +1, 首次触发硬件初始化)
     * @param[in] pdev RTC 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_rtc_open(struct hal_rtc_dev* pdev) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 RTC (引用计数 -1)
     * @param[in] pdev RTC 设备对象指针
     * @note 仅清软件状态, 不关闭 RTC 时钟 (日历持续运行)
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_rtc_close(struct hal_rtc_dev* pdev) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 设置 RTC 日历时间
     * @param[in] pdev RTC 设备对象指针
     * @param[in] time 目标日历时间
     * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
     */
    int hal_rtc_set_time(struct hal_rtc_dev* pdev,
                         const struct hal_rtc_time* time) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 读取 RTC 当前日历时间
     * @param[in] pdev RTC 设备对象指针
     * @param[out] time 回传当前日历时间
     * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
     */
    int hal_rtc_get_time(struct hal_rtc_dev* pdev,
                         struct hal_rtc_time* time) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 配置闹钟 A 并使能中断位; callback 暂存但当前无完整 ISR 派发
     * @param[in] pdev RTC 设备对象指针
     * @param[in] alarm 闹钟时间
     * @param[in] cb 闹钟回调 (ISR 上下文, 当前未完成派发)
     * @param[in] user 回调用户数据
     * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
     */
    int hal_rtc_set_alarm(struct hal_rtc_dev* pdev, const struct hal_rtc_time* alarm,
                          hal_rtc_alarm_cb_t cb, void* user) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 取消闹钟 A
     * @param[in] pdev RTC 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_rtc_cancel_alarm(struct hal_rtc_dev* pdev) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 设置 RTC 唤醒定时器
     * @param[in] pdev RTC 设备对象指针
     * @param[in] seconds 唤醒周期 (秒)
     * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
     */
    int hal_rtc_set_wakeup_timer(struct hal_rtc_dev* pdev,
                                 uint32_t seconds) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 取消 RTC 唤醒定时器
     * @param[in] pdev RTC 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_rtc_cancel_wakeup_timer(struct hal_rtc_dev* pdev) MINI_WARN_UNUSED_RESULT;
    /**
     * @brief 强制停止 RTC (断电保护/紧急停机用)
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int hal_rtc_force_stop(void) MINI_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_RTC_H */
