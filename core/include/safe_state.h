#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bootloop 退避防护 (SPI Flash 物理烧穿防御).
 *   safe_state_check_bootloop(): 每次异常启动时调用.
 *     连续 ≥ 5 次 → 永久安全锁死, 拒绝一切 Flash 写入.
 *   safe_state_clear_bootloop(): 正常冷启动/上电时调用, 计数器归零.
 */
bool safe_state_check_bootloop(void);
void safe_state_clear_bootloop(void);

/*
 * 进入不可恢复的安全状态 (IEC 61508 §7.4.3 / ISO 13485 §7.3.3)
 *
 * 本函数从 Task 上下文调用, 不能用于 NMI / ISR.
 * BOD NMI 等不可屏蔽中断请使用 safe_state_nmi_emergency_stamp().
 *
 * 本函数永不返回.
 */
void enter_safe_state(const char* reason) __attribute__((noreturn));

/*
 * BOD NMI 紧急标记 (IEC 61508 §7.4.3.2 掉电保护)
 *
 * 由平台实现 (如 soc_port_mcu 或 stm32_hal_port),
 * 平台实现本身必须置于 IRAM 中.
 *
 * 严禁: printf / mutex / FreeRTOS API / Flash 访问
 */
void safe_state_nmi_emergency_stamp(void);

#ifdef __cplusplus
}
#endif
