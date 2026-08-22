/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file safe_state.h
 *@brief safe state 头文件
 *@author H-000-H
 *@details
 *   safe_state — 不可恢复安全状态与 Bootloop 防护接口
 *   Bootloop 退避: 连续异常启动 ≥5 次永久锁死, 防止 SPI Flash 物理烧穿
 *   enter_safe_state() 永不返回 (Task 上下文); NMI 掉电场景用 nmi_emergency_stamp()
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Bootloop 退避检测: 每次异常启动时调用
     * @return 连续异常 ≥ 5 次返回 true (永久锁死, 拒绝 Flash 写入)
     * @note SPI Flash 物理烧穿防御
     */
    bool safe_state_check_bootloop(void);
    /**
     * @brief 清除 Bootloop 计数 (正常冷启动/上电时调用)
     */
    void safe_state_clear_bootloop(void);

    /**
     * @brief 进入不可恢复的安全状态 (永不返回)
     * @param[in] reason 进入安全状态的原因
     * @note 仅可从 Task 上下文调用; NMI/ISR 请用 safe_state_nmi_emergency_stamp()
     */
    void enter_safe_state(const char* reason) __attribute__((noreturn));

    /**
     * @brief BOD NMI 紧急标记 (掉电保护)
     * @note 由平台实现 (须置于 IRAM); 严禁 printf/mutex/RTOS API/Flash 访问
     */
    void safe_state_nmi_emergency_stamp(void);

#ifdef __cplusplus
}
#endif
