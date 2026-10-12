/**
 * @file mini_panic.h
 * @author H-000-H
 * @brief Panic + 关键断言: 输出原因 → 硬件安全关断 → 死循环等看门狗复位
 * @note  MINI_PANIC = 无条件 panic; MINI_CRITICAL_ASSERT = 条件断言
 * @note  板级必须提供 system_safety_hardware_shutdown(reason) 强符号
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINI_PANIC_H
#define MINI_PANIC_H

#include "compiler_compat.h"
#include "log.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief panic 互锁 (weak, 默认空实现, 板级可覆盖)
 * @note 板级可在此喂硬件看门狗、切断执行器供电
 */
void mini_panic_interlock(void);

/**
 * @brief 硬件安全关断 (weak, 默认 trap, 板级可覆盖)
 */
void safety_hardware_shutdown(void);

/**
 * @brief 板级硬件安全关断 (必须由板级强制实现)
 * @param[in] reason 关断原因 (用于日志/黑匣子)
 */
void system_safety_hardware_shutdown(const char* reason);

#ifdef __cplusplus
}
#endif

/**
 * @brief Panic
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @details 输出致命原因 -> 板级硬件安全关断 -> 驻留死循环等看门狗复位
 */
#undef MINI_PANIC
#define MINI_PANIC(fmt, ...)                                                                                                                         \
    do                                                                                                                                               \
    {                                                                                                                                                \
        mini_log_default_output("[FATAL ERROR] " fmt "\r\n", ##__VA_ARGS__);                                                                        \
        system_safety_hardware_shutdown("MINI_PANIC");                                                                                               \
        while (1)                                                                                                                                    \
        {                                                                                                                                            \
            ;                                                                                                                                        \
        }                                                                                                                                            \
    } while (0)

/**
 * @brief 关键断言
 * @param[in] cond 条件
 * @param[in] fmt 格式化字符串
 * @param ... 可变参数
 * @details cond 为假时: 输出关键原因 -> 板级硬件安全关断 -> 驻留死循环等看门狗复位
 */
#undef MINI_CRITICAL_ASSERT
#define MINI_CRITICAL_ASSERT(cond, fmt, ...)                                                                                                         \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(cond))                                                                                                                                 \
        {                                                                                                                                            \
            mini_log_default_output("[1 FAILED] %s:%d: " fmt "\r\n", __FILE__, __LINE__, ##__VA_ARGS__);                                            \
            system_safety_hardware_shutdown("MINI_CRITICAL_ASSERT");                                                                                 \
            while (1)                                                                                                                                \
            {                                                                                                                                        \
                ;                                                                                                                                    \
            }                                                                                                                                        \
        }                                                                                                                                            \
    } while (0)

#endif /* MINI_PANIC_H */
